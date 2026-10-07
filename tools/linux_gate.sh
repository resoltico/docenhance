#!/bin/sh
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
# This entry point is development-only and runs inside the isolated Linux gate container.
set -eu
cd /source
# The verifier grants each exact source directory to Git per call and ignores inherited Git config.
if [ -e /host-deps/acquisition.active ]; then
    echo "Host dependency cache has an active acquisition claim; inspect it before retrying" >&2
    exit 1
fi
if [ ! -d .cache/deps/sources ] && [ -d /host-deps/sources ]; then
    mkdir -p .cache/deps
    cp -a /host-deps/. .cache/deps/
fi
# Acquisition is explicit and separate from the offline CMake workflow; all sources remain locked.
cmake -P cmake/AcquireDependencies.cmake
python tools/check_all.py
# GCC/libstdc++ supplies Linux's release compiler; the pinned clang-tidy also runs on every TU.
CC=gcc CXX=g++ cmake --workflow --preset release
python tools/package_smoke.py --build out/release
