# Copyright (c) Martin Schweiger
# Licensed under the MIT License

cmake_minimum_required(VERSION 3.15)

project (plsplit VERSION 2021.1)

add_executable(plsplit
	plsplit.cpp
)

# not upstream: OrbiterPlatform.h for the windows.h integer types; WIC (ReadPNG) -> libpng
find_package(PNG REQUIRED)
target_include_directories(plsplit PRIVATE ${ORBITER_SOURCE_SDK_INCLUDE_DIR})
target_link_libraries(plsplit PNG::PNG)

set_target_properties(plsplit
	PROPERTIES
	# LINK_FLAGS "/SUBSYSTEM:CONSOLE" left out: MSVC linker flag, ELF has no subsystem
	RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}
	RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}
)

# not upstream: dxtex replaces the Windows DxTex.exe (BMP -> DXT1/DXT5 .dds for plsplit and pltex), built on tileedit's fastdxt
add_executable(dxtex
	dxtex.cpp
)
target_include_directories(dxtex PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tileedit/qt/extern/fastdxt)
target_link_libraries(dxtex fastdxt)
set_target_properties(dxtex
	PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}
	RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}
)

# PRE_BUILD copy of DxTex.exe replaced by the dxtex target: it builds next to plsplit

# Installation - this is currently skipped
# INSTALLDIR came from the parent ExternalProject (left out): ORBITER_INSTALL_UTILS_DIR, where the parent installed plsplit
install(TARGETS
	plsplit
	RUNTIME
	DESTINATION ${ORBITER_INSTALL_UTILS_DIR}
)
# install(FILES DxTex.exe) replaced by dxtex, installed where the parent installed DxTex.exe
install(TARGETS dxtex RUNTIME DESTINATION ${ORBITER_INSTALL_UTILS_DIR}) # not upstream: dxtex replaces DxTex.exe
