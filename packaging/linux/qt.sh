#!/bin/bash
# not upstream: Qt from source for the portable build, hardened, keeping the glibc 2.34 floor (Qt's own binaries need 2.39); qt.sh <prefix> [source cache]
set -euo pipefail
QV=6.12.0
P=${1:-/opt/Qt/$QV/gcc_64}
C=${2:-}
U=https://download.qt.io/official_releases/qt/${QV%.*}
MODS=(qtbase qtshadertools qtsvg qtdeclarative qtwayland) # in dependency order
declare -A SUM=(
	[qtbase]=a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307
	[qtshadertools]=c7d84f436e1aaef39fdcadebf2bd71bdf24dc69497e13f2e0198873cbbfea2ab
	[qtsvg]=e4ab39534ec97987b1b9b60ef7f4d3253d5912a69f8c713a01753174a4029331
	[qtdeclarative]=311f3a2603e1973bb59baef9dfa740a376de713157d4782ee043681e889c9260
	[qtwayland]=ae14d9bcf1bb3c300a3ab2e6a53f50284e5c6c966a4ac4cc5aedcb1bc74e4d02
)
PATCHES=("qtbase CVE-2026-105924-qtbase-6.12.diff c27d27b86aaf2cdab0c18d513fbb94a432e98782eddfc5f856387e4be68be50b") # Qt's security patches for this release
source /opt/rh/gcc-toolset-14/enable
W=$(mktemp -d -p "${QT_WORK:-/var/tmp}") # several GB: on disk, not a tmpfs
trap 'rm -rf "$W"' EXIT
get() { # $1 url, $2 file, $3 sha256: from the cache when it has the file
	if [ -n "$C" ] && [ -f "$C/$2" ]; then cp "$C/$2" "$W/$2"; else curl -fsSL --proto '=https' -o "$W/$2" "$1"; fi
	echo "$3  $W/$2" | sha256sum -c --quiet - || { echo "qt.sh: $2 does not match its sha256" >&2; exit 1; }
}
for m in "${MODS[@]}"; do
	f=$m-everywhere-src-$QV.tar.xz
	get "$U/$QV/submodules/$f" "$f" "${SUM[$m]}"
	tar --no-same-owner -C "$W" -xf "$W/$f"
done
for p in "${PATCHES[@]}"; do
	read -r m f s <<< "$p"
	get "$U/$f" "$f" "$s"
	patch -d "$W/$m-everywhere-src-$QV" -p1 --forward < "$W/$f"
done
mkdir -p "$W/b/qtbase"
cd "$W/b/qtbase"
"$W/qtbase-everywhere-src-$QV/configure" -prefix "$P" -release -opensource -confirm-license -nomake examples -nomake tests \
	-openssl-runtime -dbus-runtime -glib -xcb -xcb-xlib -bundled-xcb-xinput -qt-harfbuzz -qt-libpng -qt-libjpeg -qt-pcre -system-zlib \
	-fontconfig -system-freetype -no-feature-sql -feature-vulkan -feature-wayland \
	-feature-stack-protector -feature-stack-clash-protection -feature-intelcet -feature-relro-now-linker -feature-glibc-fortify-source \
	-- -DVulkan_INCLUDE_DIR="${DEPS:-/opt/deps}/include"
cmake --build . --parallel "${JOBS:-$(nproc)}"
cmake --install .
for m in "${MODS[@]:1}"; do
	mkdir -p "$W/b/$m"
	cd "$W/b/$m"
	"$P/bin/qt-configure-module" "$W/$m-everywhere-src-$QV"
	cmake --build . --parallel "${JOBS:-$(nproc)}"
	cmake --install .
done
echo "qt.sh: Qt $QV in $P"
