# Portable Linux package

`Orbiter2027-Linux-x86_64-<version>.tar.xz`: one folder that runs on the common x86-64 distributions with glibc 2.34 or newer (README-LINUX.txt).

- Build environment: AlmaLinux 9 (glibc 2.34) with GCC 14 (gcc-toolset-14), set up by `setup.sh`: the packages, cmake and ninja from `requirements.txt` (pinned by hash), Vulkan-Headers, glslang and Khronos' shader-object layer built by `deps.sh` (each tag checked against its commit), and Qt 6.12 built from source by `qt.sh` (archives pinned by SHA-256, Qt's security patches applied, hardened, keeping the glibc 2.34 floor that Qt's own binaries would raise to 2.39).
- `build.sh <source> <out> <version> [manuals folder]` configures, builds and installs Orbiter, runs `collect.py` and packs the folder. With `FROM_GIT=1` it builds from `git archive HEAD`, so ignored and untracked files never ship.
- `collect.py` bundles Qt (its plugins and the QML modules the launcher skins use) and every library outside the system set, sets each file's RUNPATH (own folder, the package root, Modules and `lib/` for Orbiter's files; `lib/` for bundled ones), writes `qt.conf` and a `deps.list` with only the system libraries, and stops if a file needs a newer glibc or libstdc++ than the baseline, has a RUNPATH entry outside the package, or is not hardened (PIE or DSO, full RELRO with `-z now`, no executable stack).

With Docker:

    docker build -t orbiter-portable-build packaging/linux
    docker run --rm -v $PWD:/src -v $PWD/out/portable:/out orbiter-portable-build \
        /src/packaging/linux/build.sh /src /out <version>

`test.sh` checks a package in other distributions' containers (packaging/linux/test.sh).

## Release candidates (`rc.sh`, no Docker)

`rc.sh` builds in an AlmaLinux 9 folder (`ORBITER_RELEASE_ROOT`), entered with bwrap, so no Docker and no root are needed. It makes one package, which runs on every distribution listed above. Builds see the folder read-only, with a clean environment; only `rc.sh setup` writes into it.

    packaging/linux/rc.sh setup              # the folder's contents (setup.sh); QT_SRC=<folder> takes Qt's source archives from there
    packaging/linux/rc.sh build              # compiles as the release does (incremental): run it with every build
    packaging/linux/rc.sh package [rcN]      # manuals, package, checks on this PC (launcher, headless flight, Vulkan client)
    packaging/linux/rc.sh release [rcN]      # package if needed, bring Custom of both GitHub repos to this commit, publish

- `rcN` defaults to the next number after the highest `2027-rcN` release.
- `package` signs the tarball with `ssh-keygen -Y sign` (`.sig`, uploaded with the release) using the release key in `~/.config/orbiter-release/` (`ORBITER_RELEASE_KEY`), made on first use; its public line goes in `allowed_signers`, and the signature must verify against it.
- The GitHub token comes from git's credential store and goes only to `gh`, never into the build. A fine-grained token limited to the two repositories (contents and releases) is enough.
- Making the folder: unpack AlmaLinux 9's container root file system (the image the Dockerfile names, by digest) into it, then `rc.sh setup`.
- The notes come from `notes.md`. The changes go in `out/portable/changes-rcN.md`; without that file, the notes list the commit subjects since the previous tag.
- `release` pushes only a fast-forward. On a fork whose Custom holds our commits under other hashes, it pushes a merge with this commit's files.
- It stops if a Custom branch has changes that are not here.
- The Wine bridge (Src/WineBridge): its Windows files and generated calls are built on this PC (Wine's headers and libraries, clang, lld, python3 with clang) against the root's build tree (out/portable/wbhost), between two builds in the root; the second builds its Linux module from them (ORBITER_WB_HELPER_FROM), and collect.py checks it with the rest.
- A second run re-uploads the files of an existing release.
