# not upstream: the Wine bridge (Src/WineBridge), Windows modules in Linux Orbiter. A project of its own, built against this build tree
# after Orbiter and its SDK and installed into it, with the Windows toolchain here or from the Windows files of a build made elsewhere
# (ORBITER_WB_HELPER_FROM: the package, packaging/linux/rc.sh)
option(ORBITER_WINEBRIDGE "build the Wine bridge: Windows modules (.dll) through Wine" ON)
set(ORBITER_WB_HELPER_FROM "" CACHE PATH "a build of Src/WineBridge against this build tree: its Windows files and generated calls, only the Linux module built here")
set(ORBITER_WB_WINE_INCLUDE "/usr/include/wine" CACHE PATH "Wine's headers (msvcrt/, windows/)")
set(ORBITER_WB_WINE_LIB "/usr/lib/wine/i386-windows" CACHE PATH "Wine's i386 import libraries")
if(NOT ORBITER_WINEBRIDGE)
	return()
endif()

set(_wb_missing)
if(NOT ORBITER_WB_HELPER_FROM)
	find_program(ORBITER_WB_CLANG clang)
	find_program(ORBITER_WB_LLD_LINK lld-link)
	find_package(Python3 COMPONENTS Interpreter)
	if(NOT ORBITER_WB_CLANG)
		list(APPEND _wb_missing clang)
	endif()
	if(NOT ORBITER_WB_LLD_LINK)
		list(APPEND _wb_missing lld)
	endif()
	if(NOT EXISTS "${ORBITER_WB_WINE_INCLUDE}/windows/windows.h")
		list(APPEND _wb_missing "Wine's headers (ORBITER_WB_WINE_INCLUDE)")
	endif()
	if(NOT EXISTS "${ORBITER_WB_WINE_LIB}/libkernel32.a")
		list(APPEND _wb_missing "Wine's i386 import libraries (ORBITER_WB_WINE_LIB)")
	endif()
	set(_wb_py 1)
	if(Python3_EXECUTABLE)
		execute_process(COMMAND ${Python3_EXECUTABLE} -c "import clang.cindex" RESULT_VARIABLE _wb_py OUTPUT_QUIET ERROR_QUIET)
	endif()
	if(_wb_py)
		list(APPEND _wb_missing "python3 with clang.cindex")
	endif()
endif()
if(_wb_missing)
	string(JOIN ", " _wb_list ${_wb_missing})
	message(STATUS "Wine bridge: not built, missing ${_wb_list}")
	return()
endif()

include(ExternalProject)
string(REPLACE ";" "|" _wb_prefix_path "${CMAKE_PREFIX_PATH}")
ExternalProject_Add(WineBridge
	SOURCE_DIR ${CMAKE_SOURCE_DIR}/Src/WineBridge
	BINARY_DIR ${CMAKE_BINARY_DIR}/Src/WineBridge
	PREFIX ${CMAKE_BINARY_DIR}/CMakeFiles/WineBridge
	LIST_SEPARATOR |
	CMAKE_GENERATOR ${CMAKE_GENERATOR}
	CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DORBITER_ROOT=${ORBITER_BINARY_ROOT_DIR} -DCMAKE_INSTALL_PREFIX=${ORBITER_BINARY_ROOT_DIR}
		-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_PREFIX_PATH=${_wb_prefix_path}
		-DWB_HELPER_FROM=${ORBITER_WB_HELPER_FROM} -DWB_WINE_INCLUDE=${ORBITER_WB_WINE_INCLUDE} -DWB_WINE_LIB=${ORBITER_WB_WINE_LIB}
	BUILD_ALWAYS ON
	DEPENDS Orbiter Orbitersdk XRSound_lib lua)

# the package: what the bridge installed into the build tree
install(PROGRAMS ${ORBITER_BINARY_MODULE_DIR}/WineBridge.so DESTINATION ${ORBITER_INSTALL_MODULE_DIR})
install(DIRECTORY ${ORBITER_BINARY_MODULE_DIR}/WineBridge/ DESTINATION ${ORBITER_INSTALL_MODULE_DIR}/WineBridge)
install(FILES ${ORBITER_BINARY_DOC_DIR}/WineBridge/README.txt DESTINATION ${ORBITER_INSTALL_DOC_DIR}/WineBridge)
