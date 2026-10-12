Include(FetchContent)

if(NOT DEFINED ORBITER_BINARY_ROOT_DIR)
    message(FATAL_ERROR "ORBITER_BINARY_ROOT_DIR is not set")
endif(NOT DEFINED ORBITER_BINARY_ROOT_DIR)

add_subdirectory(Lua)
add_subdirectory(zlib)
add_subdirectory(imgui)

## LFS
add_library(lfs SHARED luafilesystem/src/lfs.c)
# install to ORBITER_BINARY_ROOT_DIR left out: an absolute path; the build dir gets lfs.so from LIBRARY_OUTPUT_DIRECTORY
install(TARGETS lfs DESTINATION ${ORBITER_INSTALL_SDK_DIR}/Utils/)
target_include_directories(lfs PUBLIC Lua)
target_link_libraries(lfs lua)
# /DEF:lfs.def left out: ELF exports luaopen_lfs by default
# lfs.so without the lib prefix, so Lua's require "lfs" finds it like lfs.dll
set_target_properties(lfs PROPERTIES PREFIX "")

set_target_properties(lfs PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
)

add_custom_target(CopyLDoc ALL
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/ldoc/ ${ORBITER_BINARY_ROOT_DIR}/packages/ldoc
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Penlight/lua/ ${ORBITER_BINARY_ROOT_DIR}/
)

# not upstream: DXT1 block encoder of the planet tile stream (Src/Orbiter/TileStreamImg.cpp), public domain or MIT, pinned
FetchContent_Declare(stb_dxt
	URL https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_dxt.h
	URL_HASH SHA256=807667ef98e0fd749cdb65cca0c2d980bc148109d2fed6f1873c81ae0f449933
	DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(stb_dxt)
add_library(stb_dxt INTERFACE)
target_include_directories(stb_dxt INTERFACE ${stb_dxt_SOURCE_DIR})
