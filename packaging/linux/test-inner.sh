#!/bin/bash
# not upstream: runs inside a distribution container (test.sh): installs what deps.list names, then checks the portable package
set -u
PKG=/pkg.tar.xz; CLIENT=${CLIENT:-0}
[ -f /prelude.sh ] && . /prelude.sh # site set-up (proxy, CA), optional
. /etc/os-release
echo "== $PRETTY_NAME"
fail=0; ok() { echo "PASS $*"; }; bad() { echo "FAIL $*"; fail=1; }
if command -v apt-get >/dev/null; then PM=apt; elif command -v dnf >/dev/null; then PM=dnf; elif command -v zypper >/dev/null; then PM=zypper; else PM=pacman; fi
case $PM in
	apt) export DEBIAN_FRONTEND=noninteractive; apt-get update -qq >/dev/null
		apt-get install -y -qq tar xz-utils binutils xvfb xauth mesa-vulkan-drivers libgl1-mesa-dri weston >/dev/null 2>&1 || apt-get install -y -qq tar xz-utils binutils xvfb xauth mesa-vulkan-drivers libgl1-mesa-dri >/dev/null ;;
	dnf) dnf -y -q install tar xz binutils findutils xorg-x11-server-Xvfb xorg-x11-xauth mesa-vulkan-drivers mesa-dri-drivers weston >/dev/null 2>&1 ;;
	zypper) zypper -q -n install tar xz binutils findutils xvfb-run xorg-x11-server-Xvfb Mesa-vulkan-device-select libvulkan_lvp Mesa-dri weston >/dev/null 2>&1 ;;
	pacman) pacman -Sy -q --noconfirm --needed tar xz binutils findutils xorg-server-xvfb xorg-xauth vulkan-swrast mesa weston >/dev/null 2>&1 ;;
esac
mkdir -p /t && tar -xf "$PKG" -C /t && R=$(ls -d /t/Orbiter2027-*) || { echo "FAIL unpack"; exit 1; }
cd "$R"
want() { awk -v k="$1" '$1 == k { print $2 }' deps.list; }
case $PM in # the system libraries deps.list names, as the launcher asks for them
	apt) pk=""; for w in $(want apt); do for a in ${w//|/ }; do apt-cache show --no-all-versions "$a" >/dev/null 2>&1 && { pk="$pk $a"; break; }; done; done
		apt-get install -y -qq $pk >/dev/null 2>&1 && ok "apt installs the deps.list names" || bad "apt install: $pk" ;;
	dnf) dnf -y -q install $(want rpm) >/dev/null 2>&1 && ok "dnf installs the deps.list sonames" || bad "dnf install" ;;
	zypper) zypper -q -n install $(want rpm) >/dev/null 2>&1 && ok "zypper installs the deps.list sonames" || bad "zypper install" ;;
	pacman) pacman -S -q --noconfirm --needed $(want pacman) >/dev/null 2>&1 && ok "pacman installs the deps.list names" || bad "pacman install" ;;
esac
nf=$(find . -type f \( -name '*.so*' -o -perm -u+x \) -exec sh -c 'head -c4 "$1" | grep -q ELF && LC_ALL=C ldd "$1" 2>&1 | sed "s|^|$1: |"' _ {} \; | awk '/not found/' | sort -u)
[ -z "$nf" ] && ok "every library resolves" || bad "unresolved: $(echo "$nf" | head -5)"
LC_ALL=C OB_LAUNCHER_NO_EXEC=1 ./OpenOrbiter </dev/null 2>&1 | tail -2 | awk '/ready to start/ { f = 1 } END { exit !f }' && ok "OpenOrbiter checks pass" || bad "OpenOrbiter check: $(OB_LAUNCHER_NO_EXEC=1 ./OpenOrbiter </dev/null 2>&1 | tail -3)"
rm -f Orbiter.cfg # headless: a fresh package has no Orbiter.cfg, so no graphics client is active
export QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg; mkdir -p -m 700 /tmp/xdg
timeout 120 ./Orbiter "--scenariox=Delta-glider/Smack!" --fixedstep=0.02 --maxframes=200 >/tmp/h.out 2>&1; rc=$?
[ $rc = 0 ] && ok "headless scenario (200 frames)" || bad "headless scenario rc=$rc: $(tail -3 /tmp/h.out)"
mkdir -p Skins/QmlProbe/qml
printf 'Name = QML probe\nApi = 1\nQml = qml/Main.qml\n' > Skins/QmlProbe/skin.cfg
cat > Skins/QmlProbe/qml/Main.qml <<'QML'
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects
import QtQuick.Particles
import Orbiter.Launcher 1.0
Item { width: 400; height: 300; ColumnLayout { Text { text: "probe" } Shape { } ParticleSystem { } MultiEffect { } } Component.onCompleted: console.log("QML probe loaded") }
QML
for plat in offscreen xcb; do
	cp Orbiter.log /tmp/before.log 2>/dev/null
	if [ $plat = xcb ]; then run="xvfb-run -a"; else run=""; fi
	ORBITER_LAUNCHER_SKIN=QmlProbe QT_QPA_PLATFORM=$plat timeout 25 $run ./Orbiter >/tmp/l.out 2>&1; rc=$?
	if [ $rc = 124 ] && ! grep -qiE 'not installed|module .* is not|failed to load|cannot load library|undefined symbol' /tmp/l.out Orbiter.log; then ok "Launchpad with a QML skin ($plat)"; else bad "Launchpad $plat rc=$rc: $(grep -iE 'qml|error|cannot|failed' /tmp/l.out Orbiter.log | head -4)"; fi
done
rm -rf Skins/QmlProbe
if [ "$CLIENT" = 1 ]; then
	icd=$(ls /usr/share/vulkan/icd.d/lvp_icd*.json 2>/dev/null | head -1)
	printf 'StartPaused = FALSE\nFullscreen = FALSE\nWindowWidth = 1280\nWindowHeight = 720\nDeviceIndex = 0\nACTIVE_MODULES\nVulkanClient\nEND_MODULES\n' > Orbiter.cfg
	for force in false true; do
		VK_SHADER_OBJECT_FORCE_ENABLE=$force VK_DRIVER_FILES=$icd VK_ICD_FILENAMES=$icd QT_QPA_PLATFORM=xcb timeout 240 xvfb-run -a -s '-screen 0 1280x800x24' ./Orbiter "--scenariox=Delta-glider/Smack!" --fixedstep=0.02 --maxframes=120 >/tmp/c.out 2>&1; rc=$?
		if [ $rc = 0 ] && grep -qE 'Shader-object layer (available|folder)' Orbiter.log && ! grep -qE '\[ERROR\]|need ' Orbiter.log; then ok "client on lavapipe, layer forced=$force"; else bad "client force=$force rc=$rc: $(grep -E 'ERROR|need |layer|Vulkan' Orbiter.log | head -5)"; fi
	done
	if command -v weston >/dev/null; then
		b=headless; weston --help 2>&1 | grep -q 'headless-backend.so' && b=headless-backend.so # weston 9/10 name the module
		weston --backend=$b --socket=wl-test --width=1280 --height=800 >/tmp/w.out 2>&1 & wp=$!; sleep 3
		[ -S "$XDG_RUNTIME_DIR/wl-test" ] || echo "note: weston did not start: $(tail -2 /tmp/w.out)"
		WAYLAND_DISPLAY=wl-test VK_DRIVER_FILES=$icd VK_ICD_FILENAMES=$icd QT_QPA_PLATFORM=wayland timeout 240 ./Orbiter "--scenariox=Delta-glider/Smack!" --fixedstep=0.02 --maxframes=120 >/tmp/c.out 2>&1; rc=$?
		kill $wp 2>/dev/null
		[ $rc = 0 ] && ! grep -qE '\[ERROR\]' Orbiter.log && ok "client on Wayland (headless weston)" || bad "client Wayland rc=$rc: $(grep -E 'ERROR|wayland|Vulkan' /tmp/c.out Orbiter.log | head -4)"
	fi
fi
echo "== $PRETTY_NAME: $([ $fail = 0 ] && echo ALL PASS || echo FAILURES)"
exit $fail
