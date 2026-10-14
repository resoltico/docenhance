# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Owned subprocess groups, bounded waits and scoped termination unwinding."""

from __future__ import annotations

import contextlib
import os
import signal
import subprocess
import threading
import time
from dataclasses import dataclass
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Callable, Iterator
    from pathlib import Path
    from types import FrameType
    from typing import IO

REAP_RACE_SECONDS = 0.1
MIN_CAPTURE_BYTES = 2


@dataclass(frozen=True)
class CapturePolicy:
    """A bounded diagnostic stream and a cooperating workspace stop check."""

    log_limit_bytes: int
    monitor: Callable[[], None]
    interval: float = 5


def _capture_bounded(source: IO[bytes], destination: IO[bytes], limit: int) -> None:
    """Retain startup and final diagnostics without storing repetitive progress forever."""
    head_limit = limit // 2
    tail_limit = limit - head_limit
    head = 0
    tail = bytearray()
    total = 0
    while data := os.read(source.fileno(), 65536):
        total += len(data)
        first = data[: max(0, head_limit - head)]
        if first:
            destination.write(first)
            destination.flush()
            head += len(first)
        tail.extend(data[len(first) :])
        if len(tail) > tail_limit:
            del tail[: len(tail) - tail_limit]
    if total > limit:
        destination.write(f"\n[engine output omitted: {total - limit} bytes]\n".encode("ascii"))
    destination.write(tail)
    destination.flush()


def _capture_or_drain(
    pipe: IO[bytes],
    stream: IO[bytes],
    limit: int,
    failures: list[OSError | ValueError | RuntimeError],
) -> None:
    """Report a capture failure while keeping the pipe live until group shutdown."""
    try:
        _capture_bounded(pipe, stream, limit)
    except (OSError, ValueError, RuntimeError) as exc:
        failures.append(exc)
        with contextlib.suppress(OSError):
            while os.read(pipe.fileno(), 65536):
                pass


def _wait_monitored(
    process: subprocess.Popen[bytes],
    command: list[str],
    timeout: float,
    monitor: Callable[[], None],
    interval: float,
) -> int:
    """Check owned work while the subprocess runs; a refusal unwinds its process group."""
    deadline = time.monotonic() + timeout
    while True:
        monitor()
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(command, timeout)
        try:
            return process.wait(timeout=min(interval, remaining))
        except subprocess.TimeoutExpired:
            continue


def interrupted(_signum: int, _frame: FrameType | None) -> None:
    """Keep repeated termination ignored until the owning outer scope finishes cleanup."""
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    msg = "Subprocess execution interrupted"
    raise InterruptedError(msg)


@contextlib.contextmanager
def termination_unwind() -> Iterator[None]:
    """Nested scopes reuse the owning POSIX handler through all outer cleanup."""
    if os.name == "posix" and signal.getsignal(signal.SIGTERM) is interrupted:
        yield
        return
    previous = signal.signal(signal.SIGTERM, interrupted) if os.name == "posix" else None
    try:
        yield
    finally:
        if previous is not None:
            signal.signal(signal.SIGTERM, previous)


@contextlib.contextmanager
def startup_interrupts() -> Iterator[None]:
    """Record callable soft signals until construction returns an owned child."""
    handlers: dict[int, Callable[[int, FrameType | None], object]] = {}
    pending: list[tuple[int, FrameType | None]] = []

    def record(signum: int, frame: FrameType | None) -> None:
        if not pending:
            pending.append((signum, frame))

    signals: list[int] = [signal.SIGINT]
    if os.name == "posix":
        signals.append(signal.SIGTERM)
    try:
        for signum in signals:
            previous = signal.getsignal(signum)
            if callable(previous):
                handlers[signum] = previous
                signal.signal(signum, record)
        yield
    finally:
        for signum, previous in handlers.items():
            signal.signal(signum, previous)
    if pending:
        signum, frame = pending[0]
        handlers[signum](signum, frame)


def kill_group(process: subprocess.Popen[bytes]) -> None:
    """Finish this owned POSIX group even when its direct child has already exited."""
    if os.name == "posix":
        signal_group(process, vars(signal)["SIGKILL"])
    elif process.poll() is None:
        process.kill()


def signal_group(process: subprocess.Popen[bytes], signum: int) -> bool:
    """Reap before group lookup; permission denial is never evidence of absence."""
    process.poll()
    try:
        vars(os)["killpg"](process.pid, signum)
    except ProcessLookupError:
        return False
    except PermissionError as denied:
        # Darwin can report EPERM for a group containing only an unreaped zombie.
        if process.poll() is None:
            try:
                process.wait(timeout=REAP_RACE_SECONDS)
            except subprocess.TimeoutExpired as incomplete:
                raise denied from incomplete
        try:
            vars(os)["killpg"](process.pid, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            # Reaping can precede Darwin's group removal. Only native ESRCH confirms absence;
            # a persistent denial or a surviving group keeps cleanup unconfirmed.
            time.sleep(REAP_RACE_SECONDS)
            try:
                vars(os)["killpg"](process.pid, 0)
            except ProcessLookupError:
                return False
        if signum:
            raise
    return True


def group_exists(process: subprocess.Popen[bytes]) -> bool:
    """Observe the owned group identity while nested runners finish their cleanup."""
    return signal_group(process, 0)


def stop_process(process: subprocess.Popen[bytes], *, graceful: bool) -> None:
    """Allow nested runners to unwind, then finish the owned group and reap the direct child."""
    if graceful and os.name == "posix":
        if not group_exists(process):
            process.wait()
            return
        signal_group(process, signal.SIGTERM)
        deadline = time.monotonic() + 10
        while group_exists(process) and time.monotonic() < deadline:
            time.sleep(min(0.05, max(0, deadline - time.monotonic())))
    kill_group(process)
    process.wait()


@contextlib.contextmanager
def owned_process(
    command: list[str],
    env: dict[str, str],
    output: IO[bytes] | IO[str] | tuple[IO[bytes], IO[bytes]] | int | None,
    *,
    cwd: Path | None = None,
    graceful: bool = False,
) -> Iterator[subprocess.Popen[bytes]]:
    """Own only the launched session; Windows cleanup covers its direct child."""
    with termination_unwind():
        process = None
        try:
            with startup_interrupts():
                process = subprocess.Popen(
                    command,
                    stdout=output[0] if isinstance(output, tuple) else output,
                    stderr=output[1] if isinstance(output, tuple) else subprocess.STDOUT,
                    env=env,
                    cwd=cwd,
                    start_new_session=os.name == "posix",
                )
            yield process
        finally:
            if process is not None:
                try:
                    stop_process(process, graceful=graceful)
                except OSError:
                    # Group cleanup remains unconfirmed; still join our unreaped direct child.
                    if process.poll() is None:
                        process.kill()
                    process.wait()
                    raise


def bounded_process(
    command: list[str],
    env: dict[str, str],
    log: Path | None,
    timeout: float,
    *,
    graceful: bool = False,
) -> int:
    """Wait with a deadline and retained log, or inherit the supervisor's output streams."""
    output = log.open("wb") if log is not None else contextlib.nullcontext(None)
    with output as stream, owned_process(command, env, stream, graceful=graceful) as process:
        return process.wait(timeout=timeout)


def bounded_capture(
    command: list[str], env: dict[str, str], log: Path, timeout: float, policy: CapturePolicy
) -> int:
    """Run an owned engine while capping logs and checking its workspace."""
    if policy.log_limit_bytes < MIN_CAPTURE_BYTES or policy.interval <= 0:
        message = "Invalid bounded capture policy"
        raise ValueError(message)
    with log.open("wb") as stream:
        reader: threading.Thread | None = None
        source: IO[bytes] | None = None
        failures: list[OSError | ValueError | RuntimeError] = []

        def check_capture() -> None:
            if failures:
                message = "Engine output capture failed"
                raise OSError(message) from failures[0]
            policy.monitor()

        try:
            with owned_process(command, env, subprocess.PIPE) as process:
                if process.stdout is None:
                    message = "Owned process has no output pipe"
                    raise RuntimeError(message)
                source = process.stdout
                reader = threading.Thread(
                    target=_capture_or_drain,
                    args=(source, stream, policy.log_limit_bytes, failures),
                    daemon=True,
                )
                reader.start()
                result = _wait_monitored(process, command, timeout, check_capture, policy.interval)
        finally:
            if reader is not None:
                reader.join(timeout=10)
                if reader.is_alive():
                    message = "Engine output reader did not finish"
                    raise RuntimeError(message)
            if source is not None:
                source.close()
        if failures:
            message = "Engine output capture failed"
            raise OSError(message) from failures[0]
        return result
