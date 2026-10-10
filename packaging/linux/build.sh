#!/bin/bash
# not upstream: builds the portable Linux package inside the AlmaLinux 9 environment (Dockerfile); build.sh <src> <out> <version> [manuals dir]
set -euo pipefail
SRC=$(readlink -f "$1"); OUT=$(readlink -f "$2"); VER=$3; MAN=${4:-}
source /opt/rh/gcc-toolset-14/enable
B=$OUT/build; STAGE=$OUT/stage; NAME=Orbiter2027-Linux-x86_64-$VER
rm -rf "$STAGE" "$OUT/src"; mkdir -p "$B" "$OUT/src"
# a copy next to the build tree: the Html help step writes into the source tree and renames across
tar -C "$SRC" --exclude=./out --exclude=./.git --exclude="./Textures/*/Archive" -cf - . | tar -C "$OUT/src" -xf -
SRC=$OUT/src
cmake -S "$SRC" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DORBITER_MAKE_DOC=OFF -DORBITER_MAKE_TESTS=OFF \
	-DCMAKE_PREFIX_PATH="$QTDIR;$DEPS" -DVulkan_INCLUDE_DIR="$DEPS/include" -DCMAKE_INSTALL_PREFIX="$STAGE/inst"
cmake --build "$B" -j "${JOBS:-$(nproc)}"
cmake --install "$B"
R=$STAGE/$NAME
mv "$STAGE/inst/Orbiter" "$R" && rm -rf "$STAGE/inst"
[ -n "$MAN" ] && [ -d "$MAN" ] && cp -n "$MAN"/*.pdf "$R/Doc/" 2>/dev/null || true
python3 "$SRC/packaging/linux/collect.py" --root "$R" --qt "$QTDIR" --deps "$DEPS"
install -m 644 "$SRC/packaging/linux/README-LINUX.txt" "$R/README-LINUX.txt"
touch "$R/portable"
find "$R" -name 'Archive' -path '*Textures*' -prune -exec rm -rf {} + # never the high-res planet archives
( cd "$STAGE" && tar -cf - "$NAME" | xz -T0 -6 > "$OUT/$NAME.tar.xz" )
( cd "$OUT" && sha256sum "$NAME.tar.xz" > "$NAME.tar.xz.sha256" )
ls -l "$OUT/$NAME.tar.xz"
