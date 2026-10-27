Pre-release @VER@ of Orbiter 2027 for Linux, built from the Custom branch.

## Changes since @PREV@
@CHANGES@

## What's in it
- **Streamed Earth and Mars**: Earth's imagery, terrain, water, OpenStreetMap buildings and roads, and forests out to the horizon; Mars in natural colour with 6 m detail near the equator and MOLA terrain. Both stream in as you fly, from free servers with no account or key.
- **Graphics effects** in the Vulkan client: shadows, reflections, bounce light, volumetric plumes, re-entry plasma and hull heating, fog, night look.
- **Collisions and damage** add-on (Modules tab, Collision): dents, live NVIDIA Blast fracture, debris, crashes into buildings and the ground.
- **glTF models and PBR materials**: ships straight from Blender as `.glb`/`.gltf`. Try **Delta-glider > DG and DG-S and DGTest** and **Graphics > PBR material bench**.
- **JPL SPICE ephemerides** for the planets and moons (1900 to 2199), with Ceres, Amalthea, Himalia and Phoebe.
- **Launchpad skins** (Extra > Launchpad skin), **the Pluto system** and measured atmospheres.
- Manuals for what is new since the Windows version.

## Download and start
1. Download `@NAME@.tar.xz`. Its SHA256 is in the `.sha256` file.
2. Unpack it into a folder you own, for example your home folder (not `/opt`): `tar -xf @NAME@.tar.xz`
3. Start `OpenOrbiter` in that folder. The first start checks the few system libraries Orbiter takes from your distribution, offers to install any that are missing, and adds Orbiter to the app menu.
4. The Launchpad offers to download the planet and moon textures, and JPL's SPICE ephemeris kernels (1.6 GB). Without the kernels Orbiter uses its built-in series.

## Runs on
x86-64 PCs with glibc 2.34 or newer, on an X11 or Wayland desktop:
- Ubuntu 22.04, 24.04, 26.04 and relatives (Linux Mint 21+, Pop!_OS 22.04+, Zorin OS 17+)
- Debian 12, 13
- Fedora 36 or newer; RHEL, AlmaLinux, Rocky Linux 9 or newer
- openSUSE Tumbleweed
- Arch Linux, CachyOS, Manjaro, EndeavourOS, SteamOS 3

One package for all of them: it is built on AlmaLinux 9 (glibc 2.34) and brings the libraries that differ between distributions. Checked before release on an NVIDIA desktop: launcher, headless flight, Vulkan client.

## Graphics
A Vulkan 1.3 driver: Mesa (AMD, Intel) or NVIDIA. Drivers without `VK_EXT_shader_object` (Intel, older AMD Mesa) use the Khronos shader-object layer included in `lib/vulkan`.

## Included
Qt 6.8 and the other libraries that differ between distributions (`lib/`), the Orbiter User Manual, Developer Manual and Technical Reference (`Doc/`). Built with GCC 14; the build scripts are in `packaging/linux`.

Not covered: glibc older than 2.34 (Ubuntu 20.04, Debian 11, openSUSE Leap 15), ARM.
