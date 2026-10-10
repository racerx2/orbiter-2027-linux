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
