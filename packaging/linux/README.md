# Portable Linux package

`Orbiter2027-Linux-x86_64-<version>.tar.xz`: one folder that runs on the common x86-64 distributions with glibc 2.34 or newer (README-LINUX.txt).

- Build environment: AlmaLinux 9 (glibc 2.34) with GCC 14 (gcc-toolset-14), Qt 6.8 LTS (aqtinstall), Vulkan-Headers, glslang and Khronos' shader-object layer built by `deps.sh`.
- `build.sh <source> <out> <version> [manuals folder]` configures, builds and installs Orbiter, runs `collect.py` and packs the folder.
- `collect.py` bundles Qt (its plugins and the QML modules the launcher skins use) and every library outside the system set, sets each file's RUNPATH to the package's `lib/`, writes `qt.conf` and a `deps.list` with only the system libraries, and stops if a file needs a newer glibc or libstdc++ than the baseline.

With Docker:

    docker build -t orbiter-portable-build packaging/linux
    docker run --rm -v $PWD:/src -v $PWD/out/portable:/out orbiter-portable-build \
        /src/packaging/linux/build.sh /src /out <version>

`test.sh` checks a package in other distributions' containers (packaging/linux/test.sh).

## Release candidates (`rc.sh`, no Docker)

`rc.sh` builds in an AlmaLinux 9 folder with the Dockerfile's packages (`ORBITER_RELEASE_ROOT`), entered with bwrap, so no Docker and no root are needed. It makes one package, which runs on every distribution listed above.

    packaging/linux/rc.sh build              # compiles as the release does (incremental): run it with every build
    packaging/linux/rc.sh package [rcN]      # manuals, package, checks on this PC (launcher, headless flight, Vulkan client)
    packaging/linux/rc.sh release [rcN]      # package if needed, bring Custom of both GitHub repos to this commit, publish

- `rcN` defaults to the next number after the highest `2027-rcN` release.
- The notes come from `notes.md`. The changes go in `out/portable/changes-rcN.md`; without that file, the notes list the commit subjects since the previous tag.
- `release` pushes only a fast-forward. On a fork whose Custom holds our commits under other hashes, it pushes a merge with this commit's files.
- It stops if a Custom branch has changes that are not here.
- A second run re-uploads the files of an existing release.
