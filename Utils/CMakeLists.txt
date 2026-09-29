# Copyright (c) Martin Schweiger
# Licensed under the MIT License

include(ExternalProject)

add_subdirectory(Date)
add_subdirectory(meshc)
add_subdirectory(Pltex)
add_subdirectory(Shipedit)
add_subdirectory(texpack)
#add_subdirectory(tileedit/qt)

# We do this as an external project to invoke x64 toolchain
# ExternalProject left out: the Linux toolchain is 64-bit already; the install moved into plsplit/CMakeLists.txt
add_subdirectory(plsplit)

# tileedit is another 64-bit project. This also requires
# Qt5 so will only be built if that is available
# ExternalProject left out as for plsplit; tileedit uses Qt 6 like the rest of the port
add_subdirectory(tileedit/qt)
