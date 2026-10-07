# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Select the current native archive from its owning superbuild's generated CPack contract."""

from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from audit_build import FALSE, read_cache

CPACK_QUERY = r"""
include("${DE_PACKAGE_CONFIG}")
if(NOT CPACK_GENERATOR STREQUAL "External")
  message(FATAL_ERROR "The current native package requires the External generator")
endif()
set(result "{}")
foreach(key IN ITEMS DIRECTORY FILE_NAME)
  if(NOT CPACK_PACKAGE_${key})
    message(FATAL_ERROR "Missing current CPACK_PACKAGE_${key}")
  endif()
  set(value "${CPACK_PACKAGE_${key}}")
  string(REPLACE "\\" "\\\\" value "${value}")
  string(REPLACE "\"" "\\\"" value "${value}")
  string(REPLACE "\n" "\\n" value "${value}")
  string(REPLACE "\r" "\\r" value "${value}")
  string(REPLACE "\t" "\\t" value "${value}")
  string(JSON result SET "${result}" "${key}" "\"${value}\"")
endforeach()
file(WRITE "${DE_PACKAGE_RESULT}" "${result}\n")
"""


def application_build(owner: Path) -> Path:
    """Require the outer owner and its bound application; no app-child admission or scanning."""
    cache = read_cache(owner / "CMakeCache.txt")
    if cache.get("DE_SUPERBUILD", "").upper() in FALSE:
        msg = "Use --build with the owning superbuild (for example out/release), not its app child"
        raise ValueError(msg)
    application = owner / "app"
    child = read_cache(application / "CMakeCache.txt")
    if (
        "DE_SUPERBUILD" not in child
        or child["DE_SUPERBUILD"].upper() not in FALSE
        or Path(child.get("DE_SUPERBUILD_BINARY", "")).resolve() != owner
        or (owner / "build-identity.json").read_bytes()
        != (application / "build-identity.json").read_bytes()
    ):
        msg = "The application is not bound to the supplied package owner"
        raise ValueError(msg)
    return application


def cpack_values(config: Path) -> dict[str, str]:
    """Evaluate the trusted generated configuration with CMake, preserving its path syntax."""
    if not config.is_file():
        msg = f"Missing current native package metadata: {config}"
        raise ValueError(msg)
    cmake = shutil.which("cmake")
    if cmake is None:
        msg = "CMake is required to read the current generated native package configuration"
        raise ValueError(msg)
    with tempfile.TemporaryDirectory(prefix="docenhance-cpack-query-") as temporary:
        directory = Path(temporary)
        script = directory / "query.cmake"
        result = directory / "result.json"
        script.write_text(CPACK_QUERY, encoding="utf-8")
        subprocess.run(
            [
                cmake,
                f"-DDE_PACKAGE_CONFIG={config}",
                f"-DDE_PACKAGE_RESULT={result}",
                "-P",
                str(script),
            ],
            capture_output=True,
            text=True,
            check=True,
            timeout=30,
        )
        values: dict[str, str] = json.loads(result.read_bytes())
    return values


def current_package(owner: Path) -> Path:
    """Select exactly the generated current archive, regardless of other retained distributions."""
    application_build(owner)
    outer = cpack_values(owner / "CPackConfig.cmake")
    inner = cpack_values(owner / "app/CPackConfig.cmake")
    if outer != inner or Path(outer["DIRECTORY"]) != owner / "packages":
        msg = "Outer and application CPack metadata must agree on the owner's packages directory"
        raise ValueError(msg)
    name = outer["FILE_NAME"]
    if not name or name in {".", ".."} or any(c in name for c in "/\\\r\n\0"):
        msg = "Current native package basename is not confined"
        raise ValueError(msg)
    archive = Path(outer["DIRECTORY"]) / (name + ".tar.gz")
    if not archive.is_file():
        msg = f"Missing current native archive: {archive}; run cmake --workflow --preset release"
        raise ValueError(msg)
    return archive
