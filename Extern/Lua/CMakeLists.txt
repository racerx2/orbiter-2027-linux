project(lua)

Include(FetchContent)

FetchContent_Declare(
  lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG v5.1
)
FetchContent_MakeAvailable(lua)

file(GLOB LUA_SRC_FILES "${lua_SOURCE_DIR}/*.c")
file(GLOB LUA_HDR_FILES "${lua_SOURCE_DIR}/*.h")

list(REMOVE_ITEM LUA_SRC_FILES "${lua_SOURCE_DIR}/lua.c")
list(REMOVE_ITEM LUA_SRC_FILES "${lua_SOURCE_DIR}/luac.c")
list(REMOVE_ITEM LUA_SRC_FILES "${lua_SOURCE_DIR}/print.c")

# Executable - lua
add_executable(lua-exe ${LUA_SRC_FILES} "${lua_SOURCE_DIR}/lua.c")
set_target_properties(lua-exe PROPERTIES OUTPUT_NAME lua)

set_target_properties(lua-exe PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
)

install(TARGETS lua-exe RUNTIME DESTINATION ${ORBITER_INSTALL_ROOT_DIR})
install(TARGETS lua-exe DESTINATION ${ORBITER_INSTALL_SDK_DIR}/lib/Lua)
install(TARGETS lua-exe DESTINATION ${ORBITER_INSTALL_SDK_DIR}/Utils/)

# Library - liblua.so
# not upstream (dll-modules): its API is lua_remote.c's (a bridge's remote states, Orbitersdk/include/Lua/lua_remote.h); Lua's core is
# built with its API renamed (lua_rename.h), the libraries as they are. The lua executable is Lua's own.
set(LUA_LIB_FILES lauxlib.c lbaselib.c ldblib.c liolib.c lmathlib.c loslib.c lstrlib.c ltablib.c loadlib.c linit.c)
set(LUA_SO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/lua_remote.c)
foreach(path ${LUA_SRC_FILES})
	get_filename_component(name ${path} NAME)
	if(name IN_LIST LUA_LIB_FILES)
		list(APPEND LUA_SO_SRC ${path})
	else()
		set(renamed ${CMAKE_CURRENT_BINARY_DIR}/renamed/${name})
		file(WRITE ${renamed}.in "#include \"lua_rename.h\"\n#include \"${path}\"\n")
		configure_file(${renamed}.in ${renamed} COPYONLY)
		list(APPEND LUA_SO_SRC ${renamed})
	endif()
endforeach()
add_library(lua SHARED ${LUA_SO_SRC})
target_include_directories(lua PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${lua_SOURCE_DIR} ${ORBITER_SOURCE_SDK_INCLUDE_DIR}/Lua)

# LUA_BUILD_AS_DLL is the MSVC side; POSIX + dlopen give require() the C-module loader it gets from LoadLibrary
target_compile_definitions(lua-exe PRIVATE LUA_USE_POSIX LUA_USE_DLOPEN)
target_compile_definitions(lua PRIVATE LUA_USE_POSIX LUA_USE_DLOPEN)
target_link_libraries(lua-exe PUBLIC m ${CMAKE_DL_LIBS})
target_link_libraries(lua PUBLIC m ${CMAKE_DL_LIBS})

set_target_properties(lua PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
)
install(TARGETS lua DESTINATION ${ORBITER_INSTALL_ROOT_DIR})

set(LUA_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/include)
file(COPY ${LUA_HDR_FILES} DESTINATION ${LUA_INCLUDE_DIR})
target_include_directories(lua INTERFACE ${LUA_INCLUDE_DIR})

# Aliases for easier reference
add_executable(lua::exe ALIAS lua-exe)
add_library(lua::lib ALIAS lua)
