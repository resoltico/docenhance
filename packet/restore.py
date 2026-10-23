"""Restore one immutable qualified product artifact; never execute a product."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import selectors
import shutil
import stat
import subprocess
import sys
import time
import zipfile

MIB = 1024**2
ARTIFACT = 12529461168
ZIP_BYTES = 17125546
ZIP_SHA = "0cdfedd1748d4cd25c02d061c2fe418f268ce8871cb18a734691864188d8f4a8"
RUN = 40010346336
HEAD = "5bc5b45b1ecd107740c85a0aadfa594af8e878d7"


def epoch(value):
    return (
        value.st_dev,
        value.st_ino,
        value.st_mode,
        value.st_size,
        value.st_mtime_ns,
        value.st_ctime_ns,
    )


def receipt(path, deadline=None):
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_size > 51 * MIB:
        raise ValueError("Receipt requires a bounded regular file")
    value = hashlib.sha256()
    count = 0
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(descriptor, "rb") as stream:
        if epoch(os.fstat(stream.fileno())) != epoch(before):
            raise ValueError("Receipt epoch changed before reading")
        while data := stream.read(65536):
            count += len(data)
            if count > before.st_size or deadline is not None and time.monotonic() >= deadline:
                raise ValueError("Receipt content exceeds admitted size/clock")
            value.update(data)
        if epoch(os.fstat(stream.fileno())) != epoch(before):
            raise ValueError("Receipt epoch changed while reading")
    if epoch(path.lstat()) != epoch(before) or count != before.st_size:
        raise ValueError("Receipt pathname/size changed")
    return {"path": str(path), "bytes": count, "sha256": value.hexdigest()}


def guard(owner, deadline):
    if time.monotonic() >= deadline:
        raise TimeoutError("One restoration clock exhausted")
    if shutil.disk_usage(owner).free < 3 * 1024**3:
        raise RuntimeError("Restoration free space below 3 GiB")
    charged = 0
    count = 0
    for root, directories, files in os.walk(owner, followlinks=False):
        for path in [Path(root), *(Path(root) / n for n in files)]:
            info = path.lstat()
            if not (stat.S_ISREG(info.st_mode) or stat.S_ISDIR(info.st_mode)):
                raise ValueError("Nonordinary restoration owner")
            if not isinstance(getattr(info, "st_blocks", None), int):
                raise ValueError("Unknown allocated blocks")
            charged += max(info.st_size, info.st_blocks * 512)
            count += 1
        if any((Path(root) / n).is_symlink() for n in directories):
            raise ValueError("Symlink restoration directory")
        if count > 4096 or charged > 128 * MIB:
            raise ValueError("Restoration owner exceeds finite entries/128 MiB")
    return charged


def safe_member(info):
    name = info.filename
    if name != info.orig_filename or "\\" in name or not name or len(name.encode("utf-8")) > 1024:
        raise ValueError("Ambiguous ZIP name")
    if any(ord(c) < 32 or ord(c) == 127 for c in name):
        raise ValueError("Control character in ZIP name")
    path = PurePosixPath(name)
    if path.is_absolute() or any(p in {"", ".", ".."} for p in name.rstrip("/").split("/")):
        raise ValueError("Unsafe ZIP path")
    if not name.isascii() and not info.flag_bits & 0x800:
        raise ValueError("Undeclared UTF-8 ZIP name")
    mode = stat.S_IFMT(info.external_attr >> 16)
    allowed = {0, stat.S_IFDIR} if info.is_dir() else {0, stat.S_IFREG}
    if mode not in allowed or info.flag_bits & 1 or info.compress_type not in {0, 8}:
        raise ValueError("Nonordinary or unsupported ZIP member")
    if info.file_size > 51 * MIB or info.is_dir() and info.file_size:
        raise ValueError("ZIP member exceeds 51 MiB")
    return name


def restore_archive(archive_path, owner, manifest, deadline):
    """Verify every member before copying only original producer files to a new owner."""
    guard(owner, deadline)
    before = archive_path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_size > 20 * MIB:
        raise ValueError("ZIP identity is not a bounded regular file")
    original = receipt(archive_path, deadline)
    if {k: original[k] for k in ("bytes", "sha256")} != manifest["zip"]:
        raise ValueError("Original ZIP differs from exact manifest")
    if manifest["decoded_bytes"] > 404 * MIB:
        raise ValueError("Manifest decoded allowance exceeds original bound")
    seen = set()
    decoded = 0
    with zipfile.ZipFile(archive_path) as archive:
        infos = archive.infolist()
        if len(infos) > 1024:
            raise ValueError("ZIP exceeds 1024 members")
        observed_files = {}
        observed_dirs = []
        for info in infos:
            guard(owner, deadline)
            name = safe_member(info)
            canonical = name.rstrip("/")
            if canonical in seen:
                raise ValueError("Duplicate ZIP member")
            seen.add(canonical)
            if info.is_dir():
                observed_dirs.append(name)
                continue
            digest = hashlib.sha256()
            count = 0
            with archive.open(info) as stream:
                while data := stream.read(65536):
                    if time.monotonic() >= deadline:
                        raise TimeoutError("ZIP verification clock exhausted")
                    count += len(data)
                    decoded += len(data)
                    if count > 51 * MIB or decoded > manifest["decoded_bytes"] + MIB:
                        raise ValueError("Actual ZIP decode exceeds allowance")
                    digest.update(data)
            observed_files[name] = {"bytes": count, "sha256": digest.hexdigest()}
        if observed_files != manifest["files"] or sorted(observed_dirs) != manifest["directories"]:
            raise ValueError("ZIP member manifest differs")
        if decoded != manifest["decoded_bytes"]:
            raise ValueError("Decoded aggregate differs from original")
        for name in seen:
            if any(str(parent) in observed_files for parent in PurePosixPath(name).parents):
                raise ValueError("ZIP file/directory collision")
        rows = []
        for name, expected in observed_files.items():
            if not name.startswith("performance-work/"):
                continue  # Historical measurement remains only in the immutable original ZIP.
            guard(owner, deadline)
            target = owner / "originals" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(name) as source, target.open("xb") as sink:
                count = 0
                digest = hashlib.sha256()
                while data := source.read(65536):
                    if time.monotonic() >= deadline:
                        raise TimeoutError("Restoration write clock exhausted")
                    count += len(data)
                    if count > expected["bytes"]:
                        raise ValueError("Restored member exceeds exact size")
                    digest.update(data)
                    sink.write(data)
            if {"bytes": count, "sha256": digest.hexdigest()} != expected:
                raise ValueError("Restored bytes differ from original")
            mode = 0o755 if name in {r["member"] for r in manifest["products"].values()} else 0o644
            target.chmod(mode)
            row = {"member": name, "local": receipt(target, deadline), "mode": mode}
            if {k: row["local"][k] for k in ("bytes", "sha256")} != expected:
                raise ValueError("Restored file readback differs")
            rows.append(row)
    if epoch(archive_path.lstat()) != epoch(before):
        raise ValueError("Original ZIP epoch changed")
    guard(owner, deadline)
    by_name = {row["member"]: row for row in rows}
    products = {role: by_name[value["member"]] for role, value in manifest["products"].items()}
    for role, value in manifest["products"].items():
        if {k: products[role]["local"][k] for k in ("bytes", "sha256")} != {
            k: value["original"][k] for k in ("bytes", "sha256")
        }:
            raise ValueError("Product does not match original qualification binary")
    return {
        "complete": True,
        "artifact": {
            "id": manifest["artifact_id"],
            "run_id": manifest["run_id"],
            "head_sha": manifest["head_sha"],
            "zip": original,
        },
        "files": rows,
        "products": products,
        "producer_results": by_name[manifest["producer_results"]],
        "scope": manifest["scope"],
        "all_original_zip_members_verified": len(seen),
        "decoded_bytes": decoded,
        "SUT_invocations": 0,
        "new_product_builds": 0,
        "historical_trial_files_restored": False,
    }


def download(owner, deadline):
    sys.path.insert(0, str(Path(os.environ["GITHUB_WORKSPACE"]) / "tools"))
    from process_execution import owned_process

    env = dict(os.environ)
    for key in ("GH_HOST", "GH_REPO", "GH_DEBUG"):
        env.pop(key, None)
    program = shutil.which("gh")
    if not program:
        raise ValueError("GitHub CLI unavailable")
    argv = [
        str(Path(program).resolve(strict=True)),
        "api",
        "--hostname",
        "github.com",
        f"repos/resoltico/docenhance/actions/artifacts/{ARTIFACT}/zip",
    ]
    selector = selectors.DefaultSelector()
    counts = {"stdout": 0, "stderr": 0}
    try:
        with (
            (owner / "original.zip").open("xb") as output,
            (owner / "download.stderr").open("xb") as errors,
        ):
            with owned_process(argv, env, (subprocess.PIPE, subprocess.PIPE)) as child:
                try:
                    for stream, label in ((child.stdout, "stdout"), (child.stderr, "stderr")):
                        selector.register(stream, selectors.EVENT_READ, label)
                    while selector.get_map():
                        guard(owner, deadline)
                        for key, _ in selector.select(
                            min(0.25, max(0, deadline - time.monotonic()))
                        ):
                            chunk = os.read(key.fileobj.fileno(), 65536)
                            if not chunk:
                                selector.unregister(key.fileobj)
                                continue
                            limit = ZIP_BYTES if key.data == "stdout" else 65536
                            keep = max(0, limit - counts[key.data])
                            part = chunk[:keep]
                            (output if key.data == "stdout" else errors).write(part)
                            counts[key.data] += len(part)
                            if len(part) != len(chunk):
                                raise ValueError("Transport output exceeds exact allowance")
                    if child.wait(timeout=max(0.001, deadline - time.monotonic())) != 0:
                        raise RuntimeError("Original artifact download failed; no retry")
                finally:
                    selector.close()
                    for stream in (child.stdout, child.stderr):
                        if stream is not None:
                            stream.close()
    finally:
        selector.close()
    return {"argv": argv, "counts": counts, "owned_child_closed": True, "no_retry": True}


def main():
    start = time.monotonic()
    deadline = start + 120
    owner = Path(os.environ["GITHUB_WORKSPACE"]) / "restored-products"
    owner.mkdir()  # A pre-existing owner is never reused or deleted.
    result = {"complete": False, "SUT_invocations": 0, "new_product_builds": 0}
    try:
        manifest = json.loads(Path(__file__).with_name("artifact-manifest.json").read_text())
        if (manifest["artifact_id"], manifest["run_id"], manifest["head_sha"], manifest["zip"]) != (
            ARTIFACT,
            RUN,
            HEAD,
            {"bytes": ZIP_BYTES, "sha256": ZIP_SHA},
        ):
            raise ValueError("Manifest does not name the exact qualified04 artifact")
        guard(owner, deadline)
        transport = download(owner, deadline)
        result = restore_archive(owner / "original.zip", owner, manifest, deadline)
        result["transport"] = transport
        result["elapsed_seconds"] = time.monotonic() - start
        with (owner / "restoration.json").open("x") as stream:
            json.dump(result, stream, indent=2)
            stream.write("\n")
        guard(owner, deadline)
    except BaseException as exc:
        result["complete"] = False
        result["error"] = repr(exc)
        result["notes"] = list(getattr(exc, "__notes__", ()))
        # Revoke any provisional positive result if its final owner guard failed.
        if (owner / "restoration.json").exists():
            with (owner / "restoration.json").open("w") as stream:
                json.dump(result, stream, indent=2)
                stream.write("\n")
        with (owner / "restoration-failure.json").open("x") as stream:
            json.dump(result, stream, indent=2)
            stream.write("\n")
        raise


if __name__ == "__main__":
    main()
