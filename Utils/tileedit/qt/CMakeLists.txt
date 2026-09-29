# Copyright (c) Martin Schweiger
# Licensed under the MIT License

cmake_minimum_required(VERSION 3.15)

project (tileedit VERSION 2021.1)

set(FASTDXT_LIB $<TARGET_FILE:fastdxt>)

# Qt6 replaces Qt5; QTDIR hint left out: it came from the ExternalProject, Qt 6 is a system package
find_package(Qt6 QUIET COMPONENTS Widgets Core Gui)

if(Qt6_FOUND)
	add_subdirectory(extern/fastdxt)
	add_subdirectory(src)
endif()
