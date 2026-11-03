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

Checking the download (optional):
  Each release has a .sha256 and a .sig next to the tarball. The .sig is an SSH signature made with
  the Orbiter 2027 Linux release key; packaging/linux/allowed_signers in the repository holds it:
    orbiter-2027-linux-release namespaces="file" ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAUwkXGNb2FzTQDYA2U7lYfRyEW+I9C7hivqAyhr6KKg
  Save that line as allowed_signers, then:
    ssh-keygen -Y verify -f allowed_signers -I orbiter-2027-linux-release -n file \
      -s Orbiter2027-Linux-x86_64-<version>.tar.xz.sig < Orbiter2027-Linux-x86_64-<version>.tar.xz
  It prints "Good" for an untouched file.

Start:
  1. Unpack into a folder you own, for example your home folder (not /opt): Orbiter writes its
     settings, logs and downloads next to itself.
       tar -xf Orbiter2027-Linux-x86_64-*.tar.xz
  2. Run OpenOrbiter in the unpacked folder (double-click, or ./OpenOrbiter in a terminal).
     The first start checks the system libraries Orbiter uses from your distribution and offers to
     install any that are missing, and adds Orbiter to the app menu (./OpenOrbiter --remove-menu
     takes it out). ./OpenOrbiter --verbose keeps Orbiter's output in the terminal.
  3. Planet textures: the Launchpad offers to download the missing planet and moon textures.

Windows add-ons:
  Windows add-ons (.dll modules) run through the Wine bridge that comes with Orbiter. It needs Wine
  that runs 32-bit programs: on Linux Mint the Software Manager's "Wine" (sudo apt install
  wine-installer). Doc/WineBridge/README.txt has the other distributions.

Included: Qt 6.12 (built for this package) and the other libraries Orbiter needs that differ between distributions (lib/).
Taken from the system: the C library, graphics drivers (Vulkan, OpenGL), X11/Wayland, fonts,
D-Bus, GLib, OpenSSL, PipeWire (sound).

Source and build scripts: packaging/linux in the repository.
