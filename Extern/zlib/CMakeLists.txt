project(zlib)

Include(FetchContent)

set(SKIP_INSTALL_ALL ON)

# zlib 1.2.11 asks for CMake 2.4.4, which CMake 4 refuses without a policy floor
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# not upstream: zlib 1.2.11 has K&R definitions, which C23 (GCC 15's default) removed; gnu17, as minigzip needs fileno
set(CMAKE_C_STANDARD 17)

FetchContent_Declare(
  zlib
  GIT_REPOSITORY https://github.com/madler/zlib.git
  GIT_TAG v1.2.11
)
FetchContent_MakeAvailable(zlib)
target_include_directories(zlib INTERFACE ${zlib_SOURCE_DIR})
target_include_directories(zlib INTERFACE ${zlib_BINARY_DIR})

set_target_properties(zlib PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
)
install(TARGETS zlib LIBRARY DESTINATION ${ORBITER_INSTALL_ROOT_DIR})
