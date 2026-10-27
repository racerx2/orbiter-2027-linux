#!/bin/bash
# not upstream: checks a portable package in distribution containers: test.sh <package.tar.xz> [image ...]
# TEST_DOCKER_ARGS: extra docker run options (network, a /prelude.sh for proxies); CLIENT=0 skips the lavapipe client runs
# KERNELS: a folder with orbiter.bsp and kerberos_styx.bsp, for a flight with the SPICE bodies too
PKG=$(readlink -f "$1"); shift
H=$(dirname "$(readlink -f "$0")")
IMAGES=${*:-ubuntu:22.04 ubuntu:24.04 ubuntu:26.04 debian:12 debian:13 fedora:42 almalinux:9 opensuse/tumbleweed archlinux:latest cachyos/cachyos-v3:latest}
rc=0
for img in $IMAGES; do
	# shellcheck disable=SC2086
	docker run --rm -e CLIENT="${CLIENT:-1}" ${TEST_DOCKER_ARGS:-} ${KERNELS:+-v "$KERNELS":/kernels:ro} -v "$PKG":/pkg.tar.xz:ro -v "$H/test-inner.sh":/test-inner.sh:ro "$img" bash /test-inner.sh || rc=1
done
exit $rc
