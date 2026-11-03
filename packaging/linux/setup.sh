#!/bin/bash
# not upstream: the portable build environment's contents, inside AlmaLinux 9 as root (Dockerfile, rc.sh setup): packages, build tools, Vulkan pieces, Qt
set -euo pipefail
H=$(dirname "$(readlink -f "$0")")
dnf -y install dnf-plugins-core epel-release
dnf config-manager --set-enabled crb
dnf -y install gcc-toolset-14 gcc-toolset-14-libatomic-devel python3 python3-pip git which file patch patchelf xz tar findutils pkgconf-pkg-config \
	libpng-devel zlib-devel freetype-devel fontconfig-devel wayland-devel wayland-protocols-devel libxkbcommon-devel libxkbcommon-x11-devel \
	pipewire-devel mesa-libGL-devel mesa-libEGL-devel libglvnd-devel vulkan-loader-devel libX11-devel libxcb-devel xcb-util-devel \
	xcb-util-cursor-devel xcb-util-image-devel xcb-util-keysyms-devel xcb-util-renderutil-devel xcb-util-wm-devel libXrandr-devel libSM libICE \
	dbus-libs krb5-libs krb5-devel openssl-libs openssl-devel glib2 glib2-devel libzstd libzstd-devel
python3 -m pip install --no-cache-dir --require-hashes -r "$H/requirements.txt"
bash "$H/deps.sh" /opt/deps
DEPS=/opt/deps bash "$H/qt.sh" "/opt/Qt/$(sed -n 's/^QV=//p' "$H/qt.sh")/gcc_64" "${QT_SRC:-}"
dnf clean all
