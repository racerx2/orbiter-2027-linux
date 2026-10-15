#!/bin/bash
# not upstream: release candidates in one command (packaging/linux/README.md): rc.sh build | package [rcN] | release [rcN]
set -euo pipefail
H=$(dirname "$(readlink -f "$0")"); SRC=$(readlink -f "$H/../.."); cd "$SRC"
ROOT=${ORBITER_RELEASE_ROOT:-$HOME/Projects/orbiter-collision/out/pkg/alma9} # AlmaLinux 9 root with the Dockerfile's packages, no Docker needed
REPOS=${ORBITER_RELEASE_REPOS:-"racerx2/orbiter-2027-linux racerx2/orbiter2027-Linux"} # the first one numbers the releases
BRANCH=${ORBITER_RELEASE_BRANCH:-Custom}
TAGP=2027-
OUT=$SRC/out/portable; W=/work/$(basename "$SRC") # the repo inside the root
die() { echo "rc: STOP: $*" >&2; exit 1; }
say() { echo "rc: $*" >&2; }

inroot() { # fake root in the AlmaLinux folder (user namespace, no privileges); the repo's parent is /work
	nice -n 10 bwrap --unshare-user --uid 0 --gid 0 --cap-add ALL --bind "$ROOT" / --dev /dev --proc /proc --tmpfs /tmp \
		--ro-bind /etc/resolv.conf /etc/resolv.conf --share-net --bind "$(dirname "$SRC")" /work \
		--setenv HOME /root --setenv PATH /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin --setenv LANG C.UTF-8 \
		--chdir / "$@"
}

portable() { # $1 version, $2 PACK (0: compile only)
	[ -d "$ROOT/opt/deps" ] || die "no release root at $ROOT (set ORBITER_RELEASE_ROOT)"
	local qt; qt=$(ls "$ROOT/opt/Qt" | grep -m1 '^6\.') || die "no Qt in $ROOT/opt/Qt"
	mkdir -p "$OUT"
	inroot env QTDIR=/opt/Qt/$qt/gcc_64 DEPS=/opt/deps JOBS=$(( $(nproc) > 3 ? $(nproc) - 2 : 1 )) PACK="$2" \
		bash "$W/packaging/linux/build.sh" "$W" "$W/out/portable" "$1" "$W/out/portable/man" > "$OUT/build-$1.log" 2>&1 \
		|| { tail -15 "$OUT/build-$1.log" >&2; die "release build failed: $OUT/build-$1.log"; }
}

clean() { [ -z "$(git status --porcelain --untracked-files=no)" ] || die "uncommitted changes"; }

token() { GH_TOKEN=$(printf 'protocol=https\nhost=github.com\n\n' | git credential fill 2>/dev/null | awk -F= '/^password=/{print $2}'); export GH_TOKEN; [ -n "$GH_TOKEN" ] || die "no GitHub token in git's credential store"; }

nextver() { # one more than the highest 2027-rcN on the first repo
	local n; n=$(gh release list -R "${REPOS%% *}" -L 100 --json tagName -q '.[].tagName' | sed -n "s/^${TAGP}rc\([0-9][0-9]*\)$/\1/p" | sort -n | tail -1) || die "cannot list the releases of ${REPOS%% *}"
	echo "rc$(( ${n:-0} + 1 ))"
}

manuals() {
	local D=$SRC/out/build/docs L=$OUT/docs.log
	mkdir -p "$OUT/man"
	[ -f "$D/CMakeCache.txt" ] || cmake -S "$SRC" -B "$D" -G Ninja -DCMAKE_BUILD_TYPE=Release -DORBITER_MAKE_DOC=ON -DORBITER_MAKE_TESTS=OFF > "$L" 2>&1 || die "manuals configure: $L"
	nice -n 10 cmake --build "$D" --target OrbiterUserManual OrbiterDeveloperManual OrbiterTechnicalReference -j1 >> "$L" 2>&1 || die "manuals: $L" # some document compilers are not thread-safe
	cp "$D/Doc/Orbiter User Manual Linux/Orbiter User Manual.pdf" "$D/Doc/Orbiter Developer Manual Linux/Orbiter Developer Manual.pdf" \
		"$D/Doc/Orbiter Technical Reference Linux/Orbiter Technical Reference.pdf" "$OUT/man/"
}

check() { # $1 package name: unpacked with a temp HOME, so nothing lands in the user's menu or settings
	local T=$OUT/check H0=$HOME
	rm -rf "$T"; mkdir -p "$T/home"
	(cd "$OUT" && sha256sum -c "$1.tar.xz.sha256" > /dev/null) || die "sha256 of $1.tar.xz"
	tar -xf "$OUT/$1.tar.xz" -C "$T"
	cd "$T/$1"
	[ -z "$(find . -path '*Textures*' -name Archive)" ] || die "planet texture archives in the package"
	export HOME=$T/home XDG_DATA_HOME=$T/home/.local/share XDG_CONFIG_HOME=$T/home/.config XDG_CACHE_HOME=$T/home/.cache
	local l; l=$(OB_LAUNCHER_NO_EXEC=1 OB_LAUNCHER_DRY_RUN=1 ./OpenOrbiter < /dev/null 2>&1 | tail -1)
	[ "$l" = "Orbiter is ready to start." ] || die "launcher: $l"
	QT_QPA_PLATFORM=offscreen timeout 120 ./Orbiter "--scenariox=Delta-glider/Smack!" --fixedstep=0.02 --maxframes=200 > "$T/headless.out" 2>&1 || die "headless flight failed: $T/headless.out"
	cp Orbiter.log "$T/headless.log"
	if pgrep -x Orbiter > /dev/null; then
		say "client check skipped: Orbiter is running"
	else
		printf 'StartPaused = FALSE\nFullscreen = FALSE\nWindowWidth = 1280\nWindowHeight = 720\nDeviceIndex = 0\nACTIVE_MODULES\nVulkanClient\nEND_MODULES\n' > Orbiter.cfg
		export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0} DISPLAY=${DISPLAY:-:0}
		timeout 120 ./Orbiter "--scenariox=Delta-glider/Smack!" --fixedstep=0.02 --maxframes=150 > "$T/client.out" 2>&1 || die "Vulkan client failed: $T/client.out, $T/$1/Orbiter.log"
		say "client check: rc 0, $(grep 'ERROR' Orbiter.log | grep -cv 'D3D9ERROR: \(Surface \|UnDeleted Surface\)' || true) ERROR lines in its log besides the surfaces freed at exit"
	fi
	export HOME=$H0; unset XDG_DATA_HOME XDG_CONFIG_HOME XDG_CACHE_HOME
	cd "$SRC"
	say "checks pass: sha256, no archives, launcher ready, headless flight"
}

package() { # $1 version
	clean
	local N=Orbiter2027-Linux-x86_64-$1
	say "packaging $1 from $(git log -1 --format='%h %s')"
	manuals
	rm -f "$OUT/$N.commit"
	portable "$1" 1
	check "$N"
	echo "$(git rev-parse HEAD) $(git rev-parse 'HEAD^{tree}')" > "$OUT/$N.commit"
	say "package: $OUT/$N.tar.xz ($(stat -c %s "$OUT/$N.tar.xz") bytes)"
}

sync_repo() { # $1 owner/name: its branch gets HEAD's tree; prints the commit to tag
	local U=https://github.com/$1.git R RT HT M
	R=$(git ls-remote "$U" "refs/heads/$BRANCH" | cut -f1); [ -n "$R" ] || die "$1 has no $BRANCH branch"
	git fetch -q "$U" "refs/heads/$BRANCH"
	RT=$(git rev-parse "$R^{tree}"); HT=$(git rev-parse 'HEAD^{tree}')
	if [ "$RT" = "$HT" ]; then echo "$R"; return; fi
	if git merge-base --is-ancestor "$R" HEAD; then git push -q "$U" "HEAD:refs/heads/$BRANCH" >&2; git rev-parse HEAD; return; fi
	[ "$(git log --format=%T HEAD | grep -cx "$RT")" -gt 0 ] || die "$1 $BRANCH has changes that are not here (a web edit?): merge them first"
	M=$(GIT_AUTHOR_NAME=$(git log -1 --format=%an) GIT_AUTHOR_EMAIL=$(git log -1 --format=%ae) GIT_COMMITTER_NAME=$(git log -1 --format=%an) GIT_COMMITTER_EMAIL=$(git log -1 --format=%ae) \
		git commit-tree "$HT" -p "$R" -p HEAD -m "Merge $(git rev-parse --short HEAD) into $BRANCH (the same files as $(git rev-parse --short HEAD))")
	git push -q "$U" "$M:refs/heads/$BRANCH" >&2 # its history is a copy of ours under other hashes: the merge keeps it and loses nothing
	echo "$M"
}

notes() { # $1 version, $2 previous tag or "", $3 output file
	local C=$OUT/changes-$1.md G=$OUT/changes-$1.auto.md
	if [ ! -s "$C" ]; then
		if [ -n "$2" ] && git fetch -q "https://github.com/${REPOS%% *}.git" "refs/tags/$2" 2> /dev/null; then
			git log --first-parent --format='- %s' FETCH_HEAD..HEAD > "$G"
		else
			git log --first-parent --format='- %s' -20 > "$G"
		fi
		C=$G
		say "no $OUT/changes-$1.md: notes list the commits"
	fi
	python3 - "$H/notes.md" "$C" "$3" "$1" "${2#"$TAGP"}" "Orbiter2027-Linux-x86_64-$1" <<'EOF'
import sys
t, c, o, ver, prev, name = sys.argv[1:7]
s = open(t).read().replace('@CHANGES@', open(c).read().strip()).replace('@NAME@', name).replace('@VER@', ver.upper())
s = s.replace('@PREV@', prev.upper() if prev else 'the last release')
open(o, 'w').write(s)
EOF
}

release() { # $1 version
	local V=$1 N=Orbiter2027-Linux-x86_64-$1 TAG=$TAGP$1 n C PREV TC NOTES=$OUT/notes-$1.md
	clean
	[ "$(cat "$OUT/$N.commit" 2>/dev/null)" = "$(git rev-parse HEAD) $(git rev-parse 'HEAD^{tree}')" ] || package "$V"
	(cd "$OUT" && sha256sum -c "$N.tar.xz.sha256" > /dev/null) || die "sha256 of $N.tar.xz"
	local SUM; SUM=$(cut -d' ' -f1 "$OUT/$N.tar.xz.sha256")
	n=${V#rc}; PREV=""; [ "$n" -gt 1 ] 2> /dev/null && PREV=${TAGP}rc$(( n - 1 ))
	notes "$V" "$PREV" "$NOTES"
	for r in $REPOS; do
		C=$(sync_repo "$r")
		if gh release view "$TAG" -R "$r" > /dev/null 2>&1; then # a re-run: same files on a commit with our tree
			TC=$(git ls-remote "https://github.com/$r.git" "refs/tags/$TAG" | cut -f1)
			git fetch -q "https://github.com/$r.git" "refs/tags/$TAG"
			[ "$(git rev-parse 'FETCH_HEAD^{tree}')" = "$(git rev-parse 'HEAD^{tree}')" ] || die "$r: $TAG exists on ${TC:0:8}, other files"
			gh release upload "$TAG" -R "$r" --clobber "$OUT/$N.tar.xz" "$OUT/$N.tar.xz.sha256"
		else
			gh release create "$TAG" -R "$r" --target "$C" --prerelease --title "Orbiter 2027 ${V^^} for Linux (x86-64)" \
				--notes-file "$NOTES" "$OUT/$N.tar.xz" "$OUT/$N.tar.xz.sha256" > /dev/null
		fi
		local D; D=$(gh api "repos/$r/releases/tags/$TAG" --jq ".assets[] | select(.name == \"$N.tar.xz\") | .digest // \"\"")
		[ -z "$D" ] || [ "$D" = "sha256:$SUM" ] || die "$r: GitHub's digest $D is not our sha256"
		say "$r: https://github.com/$r/releases/tag/$TAG (${C:0:8})"
	done
}

case "${1:-}" in
	build) portable dev 0; say "release build compiles (out/portable/build)";;
	package) token; package "${2:-$(nextver)}";;
	release) token; release "${2:-$(nextver)}";;
	*) echo "usage: rc.sh build | package [rcN] | release [rcN]" >&2; exit 2;;
esac
