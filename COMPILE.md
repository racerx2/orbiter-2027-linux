BUILDING ON LINUX
=================
Orbiter builds natively on Linux (Qt 6 Widgets, Vulkan 1.3, PipeWire). This tree holds only the
Linux code.

LINUX: PREREQUISITES
====================
The packages, one line per distribution (only the Ubuntu names are tested so far):

- Debian/Ubuntu: `cmake ninja-build g++ git pkg-config python3 libvulkan-dev glslang-dev qt6-base-dev libwayland-dev wayland-protocols libpipewire-0.3-dev libpng-dev`
- Fedora: `cmake ninja-build gcc-c++ git pkgconf-pkg-config python3 vulkan-headers vulkan-loader-devel glslang-devel qt6-qtbase-devel wayland-devel wayland-protocols-devel pipewire-devel libpng-devel`
- openSUSE: `cmake ninja gcc-c++ git pkg-config python3 vulkan-devel vulkan-headers glslang-devel qt6-base-devel wayland-devel wayland-protocols-devel pipewire-devel libpng16-devel`
- Arch: `cmake ninja gcc git pkgconf python vulkan-headers vulkan-icd-loader glslang qt6-base wayland wayland-protocols libpipewire libpng`

Optional, Dragonfly's ADI ball (the vessel is left out without it): `libgl-dev libglu1-mesa-dev`
(Fedora `mesa-libGLU-devel libglvnd-devel`, openSUSE `glu-devel Mesa-libGL-devel`, Arch `glu libglvnd`).
For debugging: the Vulkan validation layers (`vulkan-validationlayers`).

What they are for: Qt 6 draws the windows and dialogs; Vulkan and glslang are the graphics client (glslang
compiles its shaders at run time); Wayland and wayland-protocols give mouse look (pointer lock) on Wayland;
PipeWire's headers are for sound (XRSound loads libpipewire at run time, and without it Orbiter runs silent);
libpng is for the utilities plsplit and tileedit.

Minimum versions: CMake 3.28, GCC with C++20 (built with GCC 15.2), Qt 6.5 (tested with 6.10), Vulkan
headers 1.3.246, glslang with its CMake package, PipeWire 0.3, Wayland with wayland-protocols, libpng, Python 3.
By package versions that is Ubuntu 26.04 (tested), Debian 13, Fedora 43 or newer, openSUSE Tumbleweed and Arch;
not Ubuntu 24.04 (Qt 6.4) or Debian 12 (Qt 6.4, CMake 3.25, Vulkan headers 1.3.239).

The graphics client needs a Vulkan driver with Vulkan 1.3 and VK_KHR_push_descriptor (or Vulkan 1.4),
VK_EXT_shader_object, VK_EXT_extended_dynamic_state3 and VK_EXT_vertex_input_dynamic_state; Orbiter.log names
any that are missing. Where a driver has no VK_EXT_shader_object (Intel's ANV), the Khronos layer
VK_LAYER_KHRONOS_shader_object emulates it.

Lua 5.1, zlib 1.2.11, Dear ImGui, ImPlot and Tracy are fetched by CMake at the same tags
upstream uses; Catch2 is v3.8.1. XRSound's decoders (stb_vorbis, dr_mp3, dr_flac, libxmp-lite
for tracker modules) and the VulkanClient's Vulkan Memory Allocator are fetched by CMake too.

To build the Orbiter documentation (`-DORBITER_MAKE_DOC=ON`) you need LaTeX:
`texlive-latex-extra`, `texlive-fonts-recommended`, `texlive-fonts-extra`, `texlive-science`,
`texlive-plain-generic`, `texlive-font-utils`, `ghostscript`, and for the code-level documentation
[Doxygen](https://www.doxygen.nl/index.html) with Graphviz (`doxygen`, `graphviz`). The option needs all of them.


LINUX: BUILDING ORBITER
=======================
```
cmake --preset linux-x64-release
cmake --build --preset linux-x64-release
ctest --preset linux-x64-release
```

Binaries and data land in `out/build/linux-x64-release`, in the same layout as upstream
(Modules/, Modules/Plugin/, Modules/Celbody/, Config/, ...).

To create a complete Orbiter folder, install it:
```
cmake --install out/build/linux-x64-release
```
This puts it in `out/install/linux-x64-release/Orbiter`. Start `OpenOrbiter` (or `Orbiter`) there.
Its `readme.txt` is the Linux readme (`readme_linux.txt`). `Doc/VulkanClient/VulkanClient.html`
describes the graphics client, and with `-DORBITER_MAKE_DOC=ON` `Doc/` also gets the Linux
editions of the manuals.

Other presets: `linux-x64-debug`, `linux-x64-asan`, `linux-x64-tracy`.


LINUX: PACKAGE
==============
The release package is a tarball with the manuals in `Doc/`:
```
cmake --preset linux-x64-release -DORBITER_MAKE_DOC=ON
cmake --build --preset linux-x64-release
cd out/build/linux-x64-release && cpack
```
This makes `OpenOrbiter-<version>-Linux.tar.gz`. Configure with `-DORBITER_MAKE_DOC=OFF` afterwards
to leave the manuals out of normal builds.


LINUX: PLANETARY TEXTURES
=========================
The Orbiter Git repository does not include the planetary texture files for most
celestial bodies. You need to install these separately (e.g., by installing Orbiter
2016 and optionally downloading high-res texture packs from the Orbiter website).

Set the environment variable `ORBITER_PLANET_TEXTURE_INSTALL_DIR` in your profile so that
the Orbiter build correctly configures the reference in the configuration.

Assuming Orbiter 2016 is installed in `~/orbiter2016`:

```
export ORBITER_PLANET_TEXTURE_INSTALL_DIR=~/orbiter2016/Textures
```

Alternatively, you can specify the location of the texture files as a CMake variable:
```
cmake --preset linux-x64-debug -DORBITER_PLANET_TEXTURE_INSTALL_DIR=~/orbiter2016/Textures
```
You can also set the planetary texture directory after building
Orbiter by setting the `PlanetTexDir` entry in `Orbiter.cfg`.
A package made with `cpack` always uses the `Textures` folder inside Orbiter; only
`cmake --install` keeps your directory.


LINUX: TROUBLESHOOTING
======================
* If you get errors during the build, in particular when building documentation (PDF from
LaTeX sources), try disabling multithreaded build support (limit to a single
thread). Some of the document converters/compilers you are using may not be
thread-safe.

* Running under Xvfb (e.g. for tests): set `QT_QPA_PLATFORM=xcb`. On a machine without a
Vulkan capable GPU, the software driver lavapipe can be used with
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json`.
