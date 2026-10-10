Orbiter 2027 for Linux (x86-64) - portable package

Runs on:
  Ubuntu 22.04 or newer and its relatives (Linux Mint 21+, Pop!_OS 22.04+, Zorin OS 17+)
  Debian 12 or newer
  Fedora 36 or newer, RHEL / AlmaLinux / Rocky Linux 9 or newer
  openSUSE Tumbleweed
  Arch Linux, Manjaro, EndeavourOS, SteamOS 3
  (glibc 2.34 or newer; X11 or Wayland desktop)

Graphics:
  A Vulkan 1.3 driver. Mesa (AMD, Intel) and the NVIDIA driver both work. Drivers without
  VK_EXT_shader_object (Intel, older AMD Mesa) use the Khronos shader-object layer that comes with
  this package (lib/vulkan).

Start:
  1. Unpack into a folder you own, for example your home folder (not /opt): Orbiter writes its
     settings, logs and downloads next to itself.
       tar -xf Orbiter2027-Linux-x86_64-*.tar.xz
  2. Run OpenOrbiter in the unpacked folder (double-click, or ./OpenOrbiter in a terminal).
     The first start checks the system libraries Orbiter uses from your distribution and offers to
     install any that are missing, and adds Orbiter to the app menu (./OpenOrbiter --remove-menu
     takes it out). ./OpenOrbiter --verbose keeps Orbiter's output in the terminal.
  3. Planet textures: the Launchpad offers to download the missing planet and moon textures.

Included: Qt 6.8 and the other libraries Orbiter needs that differ between distributions (lib/).
Taken from the system: the C library, graphics drivers (Vulkan, OpenGL), X11/Wayland, fonts,
D-Bus, GLib, OpenSSL, PipeWire (sound).

Source and build scripts: packaging/linux in the repository.
