# Portable Linux build environment: AlmaLinux 9 (glibc 2.34) with GCC 14, Qt 6.8 LTS and the Vulkan pieces (packaging/linux/README.md)
ARG BASE=almalinux:9
FROM ${BASE}
RUN dnf -y install dnf-plugins-core epel-release && dnf config-manager --set-enabled crb && dnf -y install \
	gcc-toolset-14 gcc-toolset-14-libatomic-devel python3 python3-pip git which file patchelf xz tar findutils pkgconf-pkg-config \
	libpng-devel zlib-devel freetype-devel fontconfig-devel wayland-devel wayland-protocols-devel libxkbcommon-devel libxkbcommon-x11-devel \
	pipewire-devel mesa-libGL-devel mesa-libEGL-devel libglvnd-devel vulkan-loader-devel libX11-devel libxcb-devel xcb-util-devel \
	xcb-util-cursor-devel xcb-util-image-devel xcb-util-keysyms-devel xcb-util-renderutil-devel xcb-util-wm-devel libXrandr-devel libSM libICE \
	dbus-libs krb5-libs openssl-libs glib2 libzstd && dnf clean all
RUN python3 -m pip install --no-cache-dir 'cmake>=3.28,<4' ninja aqtinstall
ARG QT=6.8.3
RUN python3 -m aqt install-qt linux desktop ${QT} linux_gcc_64 -O /opt/Qt && rm -rf /opt/Qt/*.json
COPY deps.sh /opt/deps.sh
RUN bash /opt/deps.sh /opt/deps
ENV QTDIR=/opt/Qt/${QT}/gcc_64 DEPS=/opt/deps
