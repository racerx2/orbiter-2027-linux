#!/bin/sh
# DaemonTest.sh TEST DIR (absolute): silent daemon test in bwrap (null sink, no /dev/snd, no user daemon, volume 0); 77 = skipped
test=$1
dir=$2
for tool in bwrap pipewire wireplumber pw-cli; do
	command -v $tool > /dev/null 2>&1 || { echo "skip: $tool is missing"; exit 77; }
done
tmp=$(mktemp -d "${TMPDIR:-/tmp}/xrsound-daemon.XXXXXX") || exit 1
mkdir -p "$tmp/run" "$tmp/config/wireplumber/wireplumber.conf.d" "$tmp/state"
cp "$dir/xrsound-test-wireplumber.conf" "$tmp/config/wireplumber/wireplumber.conf.d/"
user_run=
[ -d "/run/user/$(id -u)" ] && user_run="--tmpfs /run/user/$(id -u)"
bwrap --unshare-pid --die-with-parent --dev-bind / / --proc /proc --tmpfs /dev/snd $user_run --unsetenv DBUS_SESSION_BUS_ADDRESS \
	--setenv PIPEWIRE_RUNTIME_DIR "$tmp/run" --setenv XDG_RUNTIME_DIR "$tmp/run" --setenv PIPEWIRE_REMOTE xrsound-test-0 \
	--setenv XDG_CONFIG_HOME "$tmp/config" --setenv XDG_STATE_HOME "$tmp/state" \
	"$test" --daemon "$dir"
rc=$?
case "$tmp" in
*/xrsound-daemon.*) rm -rf "$tmp" ;;    # this run's own temp dir
esac
exit $rc
