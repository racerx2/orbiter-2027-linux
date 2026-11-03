# Portable Linux build environment: AlmaLinux 9 (glibc 2.34) with GCC 14, Qt 6.12 built from source and the Vulkan pieces (packaging/linux/README.md)
ARG BASE=almalinux:9@sha256:245f8aab832fe800aa42bdab5fad75ebbd8e0b4c9f24820a83c638b177857ae2
FROM ${BASE}
COPY setup.sh requirements.txt deps.sh qt.sh /opt/setup/
RUN bash /opt/setup/setup.sh
ENV QTDIR=/opt/Qt/6.12.0/gcc_64 DEPS=/opt/deps
