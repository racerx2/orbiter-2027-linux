#!/usr/bin/env bash
# Rebuilds Orbiter from scratch in out/build/linux-x64-release, the tree Orbiter runs from, and keeps your files there.
if [ -z "${BASH_VERSION:-}" ]; then exec bash "$0" "$@"; fi
set -euo pipefail
shopt -s nullglob

S=$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd -P)
B=$S/out/build/linux-x64-release
cd "$S"

# the build replaces the files of an Orbiter running from this tree
for p in $(pgrep -x Orbiter || true); do
    e=$(readlink "/proc/$p/exe" 2>/dev/null || true)
    if [ "${e% (deleted)}" = "$B/Orbiter" ]; then
        echo "Orbiter is running from $B (pid $p). Close it first." >&2
        exit 1
    fi
done

# fresh settings every run; CMakeFiles/copynewer keeps the record of files you changed in the tree
echo "=== Configuring linux-x64-release ==="
rm -f -- "$B/CMakeCache.txt"
if [ -d "$B/CMakeFiles" ]; then
    find "$B/CMakeFiles" -mindepth 1 -maxdepth 1 ! -name copynewer -exec rm -rf -- {} +
fi
rm -rf -- "$B"/_deps/*-subbuild
src=("$B"/_deps/*-src)
d=OFF
if [ ${#src[@]} -gt 0 ]; then d=ON; fi
nice -n 15 cmake --preset linux-x64-release -DFETCHCONTENT_FULLY_DISCONNECTED=$d

# only the compilers' object files go, not the data the build copied into the tree
echo "=== Removing object files ==="
mapfile -t rules < <(ninja -C "$B" -t rules | grep -E '^(C|CXX)_COMPILER__' || true)
if [ ${#rules[@]} -eq 0 ]; then
    echo "No compile rules in $B/build.ninja." >&2
    exit 1
fi
ninja -C "$B" -t clean -r "${rules[@]}"

echo "=== Building ==="
nice -n 15 cmake --build "$B"
echo "=== Done: $B ==="
