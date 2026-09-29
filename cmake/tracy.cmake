# Copyright (c) Gondos
# Licensed under the MIT License

# Tracy profiler integration (https://github.com/wolfpld/tracy)
# TracyClient is a shared library so every module can profile; Tracy.hpp stays on the include path even when disabled
# Modules opt in with ${TRACY_CLIENT_INCLUDE} and ${TRACY_CLIENT}; the server is built as the "profiler" project

if(ORBITER_TRACY_PROFILER)
	set(TRACY_ENABLE ON)
else()
	set(TRACY_ENABLE OFF)
endif()

set(BUILD_SHARED_LIBS ON)
set(TRACY_ONLY_LOCALHOST ON)
set(TRACY_ON_DEMAND ON)
set(TRACY_ONLY_IPV4 ON)

Include(FetchContent)
include(ExternalProject)

FetchContent_Declare(tracy
	GIT_REPOSITORY https://github.com/wolfpld/tracy.git
	GIT_TAG v0.12.2
	GIT_SHALLOW TRUE
	EXCLUDE_FROM_ALL # prevents installation of lib and include directories in ${ORBITER_INSTALL_ROOT_DIR}
)
FetchContent_MakeAvailable(tracy)

set(TRACY_CLIENT_INCLUDE ${tracy_SOURCE_DIR}/public/tracy CACHE PATH "Tracy public include path")

# Build the server and client library only if profiling is enabled.
if(ORBITER_TRACY_PROFILER)
	# To be used in CMakeLists.txt. 
	set(TRACY_CLIENT TracyClient)

	# Copy the shared library alongside the main Orbiter binary.
	install(TARGETS TracyClient LIBRARY DESTINATION ${ORBITER_INSTALL_ROOT_DIR})

	# The root CMakeLists.txt file of the repo only handles the client side.
	# The server is inside the profiler subdirectory.
	ExternalProject_Add(profiler
		SOURCE_DIR "${tracy_SOURCE_DIR}/profiler"
		INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/Orbiter/Utils
		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
               -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_INSTALL_BINDIR=.
	)
endif()
