ORBITER VISUALISATION PROJECT
Graphics Client Plugins for Orbiter Space Flight Simulator
Copyright (C) 2006-2026  Martin Schweiger


The aim of the Orbiter Visualisation Project (OVP) is to provide 3-D
visualisation support for Orbiter Space Flight Simulator via external plugin
modules.

The next Orbiter release distribution will include a "non-graphics" version
of the Orbiter core executable. This can be used as a standalone application
(e.g. as a server in a multi-user environment), but it also allows to load
a graphics client module to provide visualisation support. 

Separating the physics engine (orbiter core) and 3-D graphics support
(graphics clients) will ease code maintenance and provide an upgrade path
for future versions of rendering interfaces. It also allows developers to
implement new graphics features not found in the standard Orbiter
distribution.

OVP is an open source project that spans a number of (relatively independent)
client implementations.

- VulkanClient (OVP/VulkanClient), the DirectX-9 client ported to Vulkan on Linux.

Other clients may be added in the future.


Installing OVP
--------------
The installation requirements for the different clients may vary. Check the
documentation in the individual client directories for installation and
compilation prerequisites.
