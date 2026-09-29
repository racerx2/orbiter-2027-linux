# Copyright (c) Martin Schweiger
# Licensed under the MIT License

# not upstream: C++20 like the rest of the port (Qt 6 needs C++17 at least)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

# system libpng replaces the prebuilt extern/libpng
find_package(PNG REQUIRED)

add_executable(tileedit
	tileedit.cpp
	tileedit.qrc
	cmap.cpp
	colorbar.cpp
	ddsread.cpp
	dlgconfig.cpp
	dlgelevconfig.cpp
	dlgelevexport.cpp
	dlgelevimport.cpp
	dlgsurfimport.cpp
	dxt_io.cpp
	elevtile.cpp
	elv_io.cpp
	imagetools.cpp
	main.cpp
	tile.cpp
	tileblock.cpp
	tilecanvas.cpp
	ZTreeMgr.cpp
	tileedit.ui
	dlgConfig.ui
	dlgElevConfig.ui
	dlgElevExport.ui
	dlgElevImport.ui
	dlgSurfImport.ui
)

add_dependencies(tileedit
	fastdxt
)

# extern/libpng and extern/zlib headers left out: PNG::PNG and the zlib target carry their own
target_include_directories(tileedit PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../extern/fastdxt
	${ORBITER_SOURCE_SDK_INCLUDE_DIR} # not upstream: OrbiterPlatform.h stands in for windows.h (DWORD, BYTE, ...)
)

# prebuilt Windows zlib.lib/libpng16_static.lib left out: the repo's zlib target and system libpng
target_link_libraries(tileedit
	Qt6::Widgets
	Qt6::Core
	Qt6::Gui
	zlib
	PNG::PNG
	${FASTDXT_LIB}
)

# WIN32_EXECUTABLE left out: no GUI/console subsystem split on Linux
# not upstream: build-tree RPATH reaches z.so in the build root (upstream linked zlib statically); install keeps $ORIGIN/..
set_target_properties(tileedit
	PROPERTIES
	BUILD_WITH_INSTALL_RPATH OFF
)

# windeployqt left out: Qt 6 comes from the system, so no tmp/ deploy copy is made

# Installation - this is currently skipped

# INSTALLDIR came from the ExternalProject, which is gone
install(TARGETS
	tileedit
	RUNTIME
	DESTINATION ${ORBITER_INSTALL_UTILS_DIR}
)
# windeployqt DLL install left out: no deployed Qt libraries
