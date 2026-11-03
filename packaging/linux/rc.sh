#!/bin/bash
# not upstream: release candidates in one command (packaging/linux/README.md): rc.sh setup | build | package [rcN] | release [rcN]
set -euo pipefail
H=$(dirname "$(readlink -f "$0")"); SRC=$(readlink -f "$H/../.."); cd "$SRC"
ROOT=${ORBITER_RELEASE_ROOT:-$HOME/Projects/orbiter-collision/out/pkg/alma9} # AlmaLinux 9 root with the Dockerfile's packages, no Docker needed
REPOS=${ORBITER_RELEASE_REPOS:-"racerx2/orbiter-2027-linux racerx2/orbiter2027-Linux"} # the first one numbers the releases
BRANCH=${ORBITER_RELEASE_BRANCH:-Custom}
TAGP=2027-
OUT=$SRC/out/portable; W=/work/$(basename "$SRC") # the repo inside the root
die() { echo "rc: STOP: $*" >&2; exit 1; }
say() { echo "rc: $*" >&2; }

QTV=$(sed -n 's/^QV=//p' "$H/qt.sh") # the Qt qt.sh builds into the root
KEY=${ORBITER_RELEASE_KEY:-$HOME/.config/orbiter-release/release_ed25519} # the release signing key, never in the repository
SIGNER=orbiter-2027-linux-release # its name in allowed_signers
ENVPATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

inroot() { # the AlmaLinux folder as a read-only / (user namespace, no privileges, a clean environment); only the repo's parent (/work) and /tmp are writable
	nice -n 10 bwrap --unshare-user --uid 0 --gid 0 --clearenv --ro-bind "$ROOT" / --dev /dev --proc /proc --tmpfs /tmp \
		--ro-bind /etc/resolv.conf /etc/resolv.conf --share-net --bind "$(dirname "$SRC")" /work \
		--setenv HOME /tmp --setenv PATH "$ENVPATH" --setenv LANG C.UTF-8 --chdir / "$@"
}

setup() { # the root's contents (setup.sh: packages, build tools, Vulkan pieces, Qt): read-write, with the capabilities dnf needs; QT_SRC: a folder with Qt's source archives
	[ -d "$ROOT/etc/dnf" ] || die "no AlmaLinux 9 root at $ROOT (packaging/linux/README.md: making one)"
	mkdir -p "$ROOT/work"
	nice -n 10 bwrap --unshare-user --uid 0 --gid 0 --cap-add ALL --clearenv --bind "$ROOT" / --dev /dev --proc /proc --tmpfs /tmp \
		--ro-bind /etc/resolv.conf /etc/resolv.conf --share-net --ro-bind "$H" /setup ${QT_SRC:+--ro-bind "$QT_SRC" /qtsrc} \
		--setenv HOME /root --setenv PATH "$ENVPATH" --setenv LANG C.UTF-8 ${QT_SRC:+--setenv QT_SRC /qtsrc} --setenv JOBS "$(( $(nproc) > 3 ? $(nproc) - 2 : 1 ))" \
		--chdir / bash /setup/setup.sh
}

portable() { # $1 version, $2 PACK (0: compile only; 1: the package, built from the commit's files)
	[ -d "$ROOT/opt/deps" ] || die "no release root at $ROOT (set ORBITER_RELEASE_ROOT, rc.sh setup)"
	[ -d "$ROOT/opt/Qt/$QTV" ] || die "no Qt $QTV in the root: rc.sh setup"
	mkdir -p "$OUT"
	rootbuild "$1" 0 "" "$2" > "$OUT/build-$1.log" 2>&1 || { tail -15 "$OUT/build-$1.log" >&2; die "release build failed: $OUT/build-$1.log"; }
	wbhelper "$1"
	rootbuild "$1" "$2" "$W/out/portable/wbhost" "$2" >> "$OUT/build-$1.log" 2>&1 || { tail -15 "$OUT/build-$1.log" >&2; die "release build failed: $OUT/build-$1.log"; }
}

rootbuild() { # $1 version, $2 PACK, $3 the Wine bridge's helper build ("": none yet), $4 1: sources from git archive: build.sh in the root
	inroot env QTDIR=/opt/Qt/$QTV/gcc_64 DEPS=/opt/deps JOBS=$(( $(nproc) > 3 ? $(nproc) - 2 : 1 )) PACK="$2" WB_FROM="$3" FROM_GIT="$4" \
		bash "$W/packaging/linux/build.sh" "$W" "$W/out/portable" "$1" "$W/out/portable/man"
}

wbhelper() { # $1 version: the Wine bridge's Windows files and generated calls on this PC (Wine, clang), against the root's build tree
	local L=$OUT/wbhost-$1.log
	cmake -S "$SRC/Src/WineBridge" -B "$OUT/wbhost" -G Ninja -DCMAKE_BUILD_TYPE=Release -DORBITER_ROOT="$OUT/build" > "$L" 2>&1 \
		|| { tail -15 "$L" >&2; die "Wine bridge, this PC's part (needs Wine's headers and libraries, clang, lld, python3 with clang): $L"; }
	nice -n 10 ninja -C "$OUT/wbhost" WineBridgeHelper >> "$L" 2>&1 || { tail -15 "$L" >&2; die "Wine bridge, this PC's part: $L"; }
	! grep -i 'warning' "$L" || die "Wine bridge, this PC's part: warnings in $L"
}

clean() { [ -z "$(git status --porcelain --untracked-files=no)" ] || die "uncommitted changes"; }

GHT=""
token() { GHT=$(printf 'protocol=https\nhost=github.com\n\n' | git credential fill 2>/dev/null | awk -F= '/^password=/{print $2}'); [ -n "$GHT" ] || die "no GitHub token in git's credential store"; } # not exported: only gh_ passes it on
gh_() { GH_TOKEN=$GHT gh "$@"; }

sign() { # $1 package name: an SSH signature of the tarball (README-LINUX.txt: checking it), with a key made on first use
	if [ ! -f "$KEY" ]; then
		mkdir -p -m 700 "$(dirname "$KEY")"
		ssh-keygen -q -t ed25519 -N '' -C "$SIGNER" -f "$KEY" || die "making the release key $KEY"
		say "new release key $KEY: put this line in packaging/linux/allowed_signers and commit it:"
		echo "$SIGNER namespaces=\"file\" $(cut -d' ' -f1,2 "$KEY.pub")" >&2
	fi
	rm -f "$OUT/$1.tar.xz.sig"
	ssh-keygen -q -Y sign -f "$KEY" -n file "$OUT/$1.tar.xz" 2> /dev/null || die "signing $1.tar.xz"
	verify "$1" || die "the signature doesn't verify with packaging/linux/allowed_signers (a new key: commit its line)"
}

verify() { ssh-keygen -q -Y verify -f "$H/allowed_signers" -I "$SIGNER" -n file -s "$OUT/$1.tar.xz.sig" < "$OUT/$1.tar.xz" > /dev/null 2>&1; } # $1 package name

nextver() { # one more than the highest 2027-rcN on the first repo
	local n; n=$(gh_ release list -R "${REPOS%% *}" -L 100 --json tagName -q '.[].tagName' | sed -n "s/^${TAGP}rc\([0-9][0-9]*\)$/\1/p" | sort -n | tail -1) || die "cannot list the releases of ${REPOS%% *}"
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
	local B; B=$(python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import collect; print("\n".join(collect.runpath_outside(sys.argv[2]) + collect.unhardened(sys.argv[2])))' "$H" "$T/$1") || die "checking the package's binaries"
	[ -z "$B" ] || die "binaries: $(echo "$B" | head -5)"
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
	say "checks pass: sha256, no archives, RUNPATHs inside, hardened binaries, launcher ready, headless flight"
}

package() { # $1 version
	clean
	local N=Orbiter2027-Linux-x86_64-$1
	say "packaging $1 from $(git log -1 --format='%h %s')"
	manuals
	rm -f "$OUT/$N.commit"
	portable "$1" 1
	check "$N"
	sign "$N"
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
	verify "$N" || die "signature of $N.tar.xz"
	local SUM; SUM=$(cut -d' ' -f1 "$OUT/$N.tar.xz.sha256")
	n=${V#rc}; PREV=""; [ "$n" -gt 1 ] 2> /dev/null && PREV=${TAGP}rc$(( n - 1 ))
	notes "$V" "$PREV" "$NOTES"
	for r in $REPOS; do
		C=$(sync_repo "$r")
		if gh_ release view "$TAG" -R "$r" > /dev/null 2>&1; then # a re-run: same files on a commit with our tree
			TC=$(git ls-remote "https://github.com/$r.git" "refs/tags/$TAG" | cut -f1)
			git fetch -q "https://github.com/$r.git" "refs/tags/$TAG"
			[ "$(git rev-parse 'FETCH_HEAD^{tree}')" = "$(git rev-parse 'HEAD^{tree}')" ] || die "$r: $TAG exists on ${TC:0:8}, other files"
			gh_ release upload "$TAG" -R "$r" --clobber "$OUT/$N.tar.xz" "$OUT/$N.tar.xz.sha256" "$OUT/$N.tar.xz.sig"
		else
			gh_ release create "$TAG" -R "$r" --target "$C" --prerelease --title "Orbiter 2027 ${V^^} for Linux (x86-64)" \
				--notes-file "$NOTES" "$OUT/$N.tar.xz" "$OUT/$N.tar.xz.sha256" "$OUT/$N.tar.xz.sig" > /dev/null
		fi
		local D; D=$(gh_ api "repos/$r/releases/tags/$TAG" --jq ".assets[] | select(.name == \"$N.tar.xz\") | .digest // \"\"")
		[ -z "$D" ] || [ "$D" = "sha256:$SUM" ] || die "$r: GitHub's digest $D is not our sha256"
		say "$r: https://github.com/$r/releases/tag/$TAG (${C:0:8})"
	done
}

case "${1:-}" in
	setup) setup; say "release root ready: $ROOT";;
	build) portable dev 0; say "release build compiles (out/portable/build)";;
	package) token; package "${2:-$(nextver)}";;
	release) token; release "${2:-$(nextver)}";;
	*) echo "usage: rc.sh setup | build | package [rcN] | release [rcN]" >&2; exit 2;;
esac
