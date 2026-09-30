Orbiter 2024 Readme
===================


Orbiter is now published as an Open Source project under the MIT
License (see LICENSE file for details).


1. Hardware requirements
------------------------
The minimum hardware requirements needed by Orbiter are:
RAM: 500 MB
CPU: Dual Core
GPU: 50 GFlops, Vulkan 1.3 driver
Disk: 5 GB of free space

The recommended hardware requirements are:
RAM: 2 GB
GPU: 100 GFlops
Disk: 10 GB of free space (80 GB if you want hi-res textures)

The Linux package needs glibc 2.43 and Qt 6.10 or newer (Ubuntu
26.04 or a distribution at least as new); the OpenOrbiter
launcher checks this. The graphics driver needs Vulkan 1.3 with
the extensions VK_KHR_push_descriptor, VK_EXT_shader_object,
VK_EXT_extended_dynamic_state3 and VK_EXT_vertex_input_dynamic_state
(current Mesa and NVIDIA drivers have them; on Intel's ANV the
Khronos layer VK_LAYER_KHRONOS_shader_object is needed, see
Doc/VulkanClient/VulkanClient.html).


2. Orbiter installation
-----------------------
Create a new folder for the Orbiter installation, e.g. ~/Orbiter.

If a previous version of Orbiter is already installed on your
computer, you should not install the new version into the same
folder, because this could lead to file conflicts. You may want
to keep your old installation until you have made sure that the
latest version works without problems. Multiple Orbiter
installations can exist on the same computer.

Unpack the Orbiter .tar.gz installation package into the new
folder, using either your file manager's archive tool or
tar -xzf OpenOrbiter-<version>-Linux.tar.gz -C ~/Orbiter
Important: Take care to preserve the directory structure of the
package. The Orbiter folder is OpenOrbiter-<version>-Linux/Orbiter
inside it.

After unpacking the package, make sure your Orbiter folder
contains the executable (Orbiter), its launcher (OpenOrbiter)
and, among other files, the Config, Meshes, Scenarios and Textures
subfolders.

To uninstall Orbiter, run ./OpenOrbiter --remove-menu, then
simply remove the Orbiter folders with all contents and
subdirectories. This will completely remove Orbiter from your
hard drive. --remove-menu keeps every copy of Orbiter out of the
app menu; ./OpenOrbiter --add-menu puts it back. The launcher's
own files are in ~/.cache/orbiter-linux (~/.cache/orbiter64-linux
for the first Linux release).


3. Launching Orbiter
--------------------
The Orbiter simulator is launched with the OpenOrbiter launcher
in the Orbiter folder (it starts the Orbiter executable from its
own folder). The first time, it checks what Orbiter needs, offers
to install anything missing from your distribution's own
repositories, and puts Orbiter in your app menu, so after that it
starts from the menu like any other program. Orbiter runs on its
own: its output goes to ~/.cache/orbiter-linux/orbiter.log
(./OpenOrbiter --verbose keeps it in the terminal), and
./OpenOrbiter --remove-menu takes it out of the app menu.
Graphics come from the included VulkanClient plugin
(a Vulkan 1.3 driver is required): select it as graphics engine
on the Video tab of the Launchpad. Without a graphics engine,
Orbiter runs in console mode.
Once running, Orbiter will show you the "Launchpad" dialog, where
you can select video options and simulation parameters.

You are now ready to start, select a scenario from the Launchpad
dialog and click the "Launch Orbiter" button!


4. Help
-------
In the release package, help files are located in the Doc
subfolder (a source build makes the manuals only with
-DORBITER_MAKE_DOC=ON, see COMPILE.md).
Orbiter User Manual (Linux).pdf is the main Orbiter manual, and it
is highly recommended you read it. VulkanClient/VulkanClient.html
describes the graphics client and its settings.

The in-game help system can be opened via the "Help" button on
the Orbiter Launchpad dialog, or with Alt-F1 while running
Orbiter. In a KDE Plasma Wayland session Orbiter's keys (such as
Alt-F1, Ctrl-F1 to Ctrl-F4 and Ctrl-F9) go to Orbiter while its
simulation window has the focus, and the other desktop shortcuts
keep working. Elsewhere the desktop may keep such keys for itself;
their dialogs are also on Orbiter's main menu (F4). See "Keys
taken by the desktop" in the User Manual.

Remaining questions can be posted on the Orbiter user forum at
https://orbiter-forum.com.


5. Linux notes
--------------
Add-ons that consist of meshes, textures, configuration files and
scenarios work as on Windows. Add-on modules (.dll files) are
Windows programs and do not run on Linux: such an add-on needs a
Linux build of its modules (.so files). Unpack add-ons into the
existing folders: folders whose names differ only in case are
merged when files are opened, not when Orbiter lists a folder
(scenarios, startup modules, bases).

Sound plays through PipeWire; without it Orbiter runs silent.

Orbiter writes its log to Orbiter.log in the Orbiter folder. The
VulkanClient writes its own log to
Modules/VulkanClient/D3D9ClientLog.html.
