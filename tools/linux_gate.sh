#!/bin/sh
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
# Offline gate: explicit locked image/archive preparation happens before this command.
set -eu
cd /source
temporary_root=/tmp/docenhance-linux-tmp
mkdir -p "$temporary_root"
for entry in "$temporary_root"/* "$temporary_root"/.[!.]* "$temporary_root"/..?*; do
    if [ -e "$entry" ] || [ -L "$entry" ]; then
        echo "FAIL: retained Linux temporary state requires inspection: $entry" >&2
        exit 1
    fi
done
mkdir "$TMPDIR"
cleanup_temporary() {
    status=$?
    trap - EXIT
    if ! rm -rf -- "$TMPDIR"; then
        echo "FAIL: owned Linux temporary cleanup failed: $TMPDIR" >&2
        if [ "$status" -eq 0 ]; then status=1; fi
    fi
    exit "$status"
}
trap cleanup_temporary EXIT
python tools/linux_cache_seed.py --host /host-deps --cache .cache/deps
python tools/check_all.py
CC=gcc CXX=g++ cmake --workflow --preset release
python tools/package_smoke.py --build out/release
