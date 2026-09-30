#!/bin/sh
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
# This entry point is development-only and runs inside the isolated Linux gate container.
set -eu
cd /source
# Host-owned worktree and copied verified caches retain host UIDs. Trust only these explicit paths.
git config --global --add safe.directory /source
if [ ! -d .cache/deps/sources ] && [ -d /host-deps/sources ]; then
    mkdir -p .cache/deps
    cp -a /host-deps/. .cache/deps/
fi
for dependency in .cache/deps/sources/*; do
    if [ -d "$dependency" ]; then
        git config --global --add safe.directory "/source/$dependency"
    fi
done
# Acquisition is explicit and separate from the offline CMake workflow; all sources remain locked.
cmake -P cmake/AcquireDependencies.cmake
python tools/check_all.py
# GCC/libstdc++ supplies Linux's release compiler; the pinned clang-tidy also runs on every TU.
CC=gcc CXX=g++ cmake --workflow --preset release
python tools/package_smoke.py "dist/docenhance-$(python -c "import sys; sys.path.insert(0, 'tools'); from project_version import project_version; print(project_version())")-Linux-$(uname -m).tar.gz"
