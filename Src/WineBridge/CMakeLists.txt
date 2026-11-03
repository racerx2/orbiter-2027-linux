# WineBridge: Windows vessel modules in Linux Orbiter through a Wine helper
#   Modules/WineBridge.so (GCC, against an Orbiter package's SDK)
#   Modules/WineBridge/Orbiter.exe (clang for i686-windows, against Wine's headers and a Windows Orbiter SDK)
cmake_minimum_required(VERSION 3.20...3.31)
project(WineBridge LANGUAGES C CXX)

set(ORBITER_ROOT "" CACHE PATH "the Orbiter folder the module is built against and installed into")
set(WB_WIN_SDK "${CMAKE_SOURCE_DIR}/sdk/win32/Orbitersdk/include" CACHE PATH "the Windows Orbiter SDK headers the helper is built with (2024, sdk/win32)")
set(WB_WINE_INCLUDE "/usr/include/wine" CACHE PATH "Wine's headers (msvcrt/, windows/)")
set(WB_WINE_LIB "/usr/lib/wine/i386-windows" CACHE PATH "Wine's i386 import libraries")
option(WB_REQUIRE_ALL "stop the build when an import of api/imports.txt isn't exported by the helper" ON)
set(WB_HELPER_FROM "" CACHE PATH "a full build of this source against the same Orbiter: its Windows files and generated calls, only the Linux module built here (tools/release.sh)")

if(NOT EXISTS "${ORBITER_ROOT}/Orbitersdk/lib/cmake/Orbiter/OrbiterConfig.cmake")
	message(FATAL_ERROR "ORBITER_ROOT must be an Orbiter folder with Orbitersdk/lib/cmake/Orbiter")
endif()
find_package(Orbiter CONFIG REQUIRED PATHS "${ORBITER_ROOT}/Orbitersdk/lib/cmake/Orbiter" NO_DEFAULT_PATH)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "${ORBITER_ROOT}" CACHE PATH "" FORCE)
endif()
if(NOT WB_HELPER_FROM)
	find_package(Python3 REQUIRED COMPONENTS Interpreter)
	find_program(WB_CLANG clang REQUIRED)
	find_program(WB_LLD_LINK lld-link REQUIRED)
	foreach(f "${WB_WIN_SDK}/Orbitersdk.h" "${WB_WINE_INCLUDE}/windows/windows.h" "${WB_WINE_LIB}/libkernel32.a")
		if(NOT EXISTS "${f}")
			message(FATAL_ERROR "the helper needs ${f} (WB_WIN_SDK, WB_WINE_INCLUDE, WB_WINE_LIB)")
		endif()
	endforeach()
	execute_process(COMMAND ${WB_CLANG} -print-resource-dir OUTPUT_VARIABLE WB_CLANG_RES OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

# ---------------------------------------------------------------------
# the generated calls (tools/wbgen.py): both sides from the Windows and the Linux SDK's declarations
if(NOT WB_HELPER_FROM)
	set(WB_GEN ${CMAKE_BINARY_DIR}/gen)
	add_custom_command(OUTPUT ${WB_GEN}/linux_syms.txt ${WB_GEN}/orbiter.sha256
		COMMAND ${CMAKE_COMMAND} -E make_directory ${WB_GEN}
		COMMAND sh ${CMAKE_SOURCE_DIR}/tools/linuxsyms.sh ${WB_GEN}/linux_syms.txt ${ORBITER_ROOT}/Orbiter $<TARGET_FILE:Orbiter::Orbitersdk> ${WB_GEN}/orbiter.sha256
		DEPENDS ${CMAKE_SOURCE_DIR}/tools/linuxsyms.sh ${ORBITER_ROOT}/Orbiter
		COMMENT "generator: what the Linux Orbiter defines" VERBATIM)
	set(WB_GEN_OUT ${WB_GEN}/gen_calls.h ${WB_GEN}/gen_helper.cpp ${WB_GEN}/gen_serve.cpp ${WB_GEN}/absent.txt)
	add_custom_command(OUTPUT ${WB_GEN_OUT}
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/wbgen.py --imports ${CMAKE_SOURCE_DIR}/api/imports.txt --later ${CMAKE_SOURCE_DIR}/api/later.txt --linux-syms ${WB_GEN}/linux_syms.txt
			--manual ${CMAKE_SOURCE_DIR}/api/manual.txt --strings ${CMAKE_SOURCE_DIR}/api/strings.txt --win-sdk ${WB_WIN_SDK}
			--linux-sdk ${ORBITER_ROOT}/Orbitersdk/include --wine-include ${WB_WINE_INCLUDE} --clang-res ${WB_CLANG_RES} --out ${WB_GEN}
		DEPENDS ${CMAKE_SOURCE_DIR}/tools/wbgen.py ${CMAKE_SOURCE_DIR}/api/imports.txt ${CMAKE_SOURCE_DIR}/api/manual.txt ${CMAKE_SOURCE_DIR}/api/later.txt ${CMAKE_SOURCE_DIR}/api/strings.txt ${WB_GEN}/linux_syms.txt
		COMMENT "generator: the bridged functions" VERBATIM)
	add_custom_target(WineBridgeGen DEPENDS ${WB_GEN_OUT} ${WB_GEN}/orbiter.sha256)
else()
	# the generated calls of that build, made against this very Orbiter: the helper compares their hash with the module's at its start
	set(WB_GEN ${WB_HELPER_FROM}/gen)
	foreach(f gen_calls.h gen_serve.cpp orbiter.sha256)
		if(NOT EXISTS "${WB_GEN}/${f}")
			message(FATAL_ERROR "WB_HELPER_FROM: no ${WB_GEN}/${f} (build that tree first)")
		endif()
	endforeach()
	add_custom_target(WineBridgeFromCheck # at every build: that build's calls were made against this Orbiter binary
		COMMAND ${CMAKE_COMMAND} -DSHA=${WB_GEN}/orbiter.sha256 -DORBITER=${ORBITER_ROOT}/Orbiter -P ${CMAKE_SOURCE_DIR}/tools/fromcheck.cmake VERBATIM)
endif()

# ---------------------------------------------------------------------
# the Linux module
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Gui) # the Linux SDK's HDC and HBITMAP are QPainter and QImage
add_library(WineBridge MODULE linux/Bridge.cpp linux/WbVessel.cpp linux/Serve.cpp linux/Manual.cpp linux/Draw.cpp linux/Sound.cpp linux/Module.cpp linux/Slots.cpp
	linux/Mfd.cpp linux/Hooks.cpp linux/Celbody.cpp linux/Extra.cpp linux/Panel.cpp linux/Light.cpp linux/Vdata.cpp linux/Oapi.cpp linux/Lua.cpp linux/LuaRemote.cpp
	${WB_GEN}/gen_serve.cpp)
if(NOT WB_HELPER_FROM)
	add_dependencies(WineBridge WineBridgeGen)
else()
	add_dependencies(WineBridge WineBridgeFromCheck)
endif()
set_target_properties(WineBridge PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
target_include_directories(WineBridge PRIVATE ${WB_GEN} ${CMAKE_SOURCE_DIR}/linux)
target_compile_options(WineBridge PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(WineBridge PRIVATE Orbiter::Orbitersdk Orbiter::XRSound Orbiter::lua Qt6::Gui ${CMAKE_DL_LIBS})
set_target_properties(WineBridge PROPERTIES INSTALL_RPATH "$ORIGIN/..") # Orbiter's lua.so
target_compile_definitions(WineBridge PRIVATE QT_NO_VERSION_TAGGING) # runs with the Qt an Orbiter package brings (6.8 in RC11), older than the headers
install(TARGETS WineBridge LIBRARY DESTINATION Modules)

option(WB_TESTS "build the test probe plugin (tests/run.sh)" OFF)
if(WB_TESTS)
	add_library(WbProbe MODULE tests/probe.cpp)
	set_target_properties(WbProbe PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
	target_compile_options(WbProbe PRIVATE -Wall -Wextra)
	target_link_libraries(WbProbe PRIVATE Orbiter::Orbitersdk)
endif()
install(FILES doc/README.txt DESTINATION Doc/WineBridge)

# ---------------------------------------------------------------------
# WB_HELPER_FROM: that build's Windows files (PE files, the same in any Wine) go with this module; nothing below is built here
if(WB_HELPER_FROM)
	set(WB_FROM_FILES ${WB_HELPER_FROM}/helper/Orbiter.exe ${WB_HELPER_FROM}/helper/OrbiterSound.dll ${WB_HELPER_FROM}/helper/XRSound.dll
		${WB_HELPER_FROM}/lua/lua.dll ${WB_HELPER_FROM}/lua/lua5.1.dll ${WB_HELPER_FROM}/lua/LuaInterpreter.dll ${WB_HELPER_FROM}/lua/LuaInline.dll)
	foreach(f ${WB_FROM_FILES})
		if(NOT EXISTS "${f}")
			message(FATAL_ERROR "WB_HELPER_FROM: no ${f} (build that tree first)")
		endif()
	endforeach()
	install(FILES ${WB_FROM_FILES} DESTINATION Modules/WineBridge)
	return()
endif()

# ---------------------------------------------------------------------
# the helper, Orbiter.exe: its exports are the Windows SDK's names, so the DLLs' Orbiter.exe imports bind to it
include(FetchContent)
FetchContent_Declare(wb_zlib131 URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz # zlib 1.3.1, pinned by its hash
	URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23 DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
FetchContent_MakeAvailable(wb_zlib131)
set(WB_FLAGS --target=i686-windows -fms-extensions -fms-compatibility -Xclang -fno-dllexport-inlines -fno-threadsafe-statics
	-nostdinc -isystem ${WB_CLANG_RES}/include -isystem ${WB_WINE_INCLUDE}/msvcrt -isystem ${WB_WINE_INCLUDE}/windows -isystem ${wb_zlib131_SOURCE_DIR}
	-isystem ${CMAKE_SOURCE_DIR}/helper/shim -isystem ${WB_WIN_SDK} -I${CMAKE_SOURCE_DIR}/helper -I${WB_GEN}
	-DSTRICT -DNDEBUG -O2 -fno-exceptions -fno-rtti -Wall -Wno-microsoft-cast) # NDEBUG: released Orbiter.exe is a Release build (the SDK headers assert)
set(WB_HDR ${CMAKE_SOURCE_DIR}/proto/wb_proto.h ${CMAKE_SOURCE_DIR}/helper/wb_helper.h ${CMAKE_SOURCE_DIR}/lua/wb_luaapi.h ${CMAKE_SOURCE_DIR}/helper/osound.h ${CMAKE_SOURCE_DIR}/helper/xrsound.h ${CMAKE_SOURCE_DIR}/helper/compat2010.h ${WB_GEN}/gen_calls.h)
set(WB_HELPER_SRC main.cpp channel.cpp api2010.cpp manual.cpp draw.cpp mfd.cpp module.cpp celbody.cpp lpad.cpp camera.cpp text.cpp hooks.cpp hud.cpp
	live.cpp sound.cpp light.cpp made.cpp vdata.cpp oapi.cpp script.cpp luaremote.cpp luaserve.cpp dialog.cpp xrsound.cpp crt.c)
# the classes whose inline and compiler-made members api/inline.def exports: made there, none inlined away
set(WB_NOINLINE mfd.cpp module.cpp celbody.cpp lpad.cpp camera.cpp made.cpp)
set(WB_OBJS)
foreach(src ${WB_HELPER_SRC} gen_helper.cpp)
	if(src STREQUAL gen_helper.cpp)
		set(path ${WB_GEN}/gen_helper.cpp)
	else()
		set(path ${CMAKE_SOURCE_DIR}/helper/${src})
	endif()
	string(REPLACE "/" "_" name ${src})
	set(obj ${CMAKE_BINARY_DIR}/helper/${name}.obj)
	set(extra)
	if(src IN_LIST WB_NOINLINE)
		set(extra -fno-inline)
	endif()
	add_custom_command(OUTPUT ${obj}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/helper
		COMMAND ${WB_CLANG} ${WB_FLAGS} ${extra} -c ${path} -o ${obj}
		DEPENDS ${path} ${WB_HDR}
		COMMENT "helper: ${src}" VERBATIM)
	list(APPEND WB_OBJS ${obj})
endforeach()
# zlib in the helper, as in Orbiter.exe (oapiInflate, oapiDeflate), built for i686-windows
foreach(src adler32.c compress.c crc32.c deflate.c inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c)
	set(obj ${CMAKE_BINARY_DIR}/helper/zlib_${src}.obj)
	add_custom_command(OUTPUT ${obj}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/helper
		COMMAND ${WB_CLANG} --target=i686-windows -fms-extensions -nostdinc -isystem ${WB_CLANG_RES}/include -isystem ${WB_WINE_INCLUDE}/msvcrt
			-isystem ${WB_WINE_INCLUDE}/windows -DNDEBUG -O2 -w -c ${wb_zlib131_SOURCE_DIR}/${src} -o ${obj}
		DEPENDS ${wb_zlib131_SOURCE_DIR}/${src}
		COMMENT "helper: zlib ${src}" VERBATIM)
	list(APPEND WB_OBJS ${obj})
endforeach()
set(WB_EXE ${CMAKE_BINARY_DIR}/helper/Orbiter.exe)
# the activation context of Orbiter 2010's and 2016's orbiter.exe (its manifest): modules that import the VC80 or VC90 runtime
# without a manifest of their own (HUDDrawer.dll) find it through the process's, as on Windows
set(WB_MANIFEST /manifest:embed
	"/manifestdependency:type='win32' name='Microsoft.VC80.CRT' version='8.0.50608.0' processorArchitecture='x86' publicKeyToken='1fc8b3b9a1e18e3b'"
	"/manifestdependency:type='win32' name='Microsoft.VC90.CRT' version='9.0.21022.8' processorArchitecture='x86' publicKeyToken='1fc8b3b9a1e18e3b'"
	"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'")
set(WB_IMPLIB ${CMAKE_BINARY_DIR}/helper/Orbiter.lib)
add_custom_command(OUTPUT ${WB_EXE} ${WB_IMPLIB}
	COMMAND ${WB_LLD_LINK} /nologo /safeseh:no /subsystem:console /entry:mainCRTStartup /out:${WB_EXE} /implib:${WB_IMPLIB}
		${WB_MANIFEST} /def:${CMAKE_SOURCE_DIR}/api/inline.def ${WB_OBJS} ${WB_WINE_LIB}/libwinecrt0.a ${WB_WINE_LIB}/libucrtbase.a ${WB_WINE_LIB}/libkernel32.a
		${WB_WINE_LIB}/libuser32.a ${WB_WINE_LIB}/libgdi32.a
	DEPENDS ${WB_OBJS} ${CMAKE_SOURCE_DIR}/api/inline.def
	COMMENT "helper: Orbiter.exe" VERBATIM)
set(WB_COVERED ${CMAKE_BINARY_DIR}/helper/covered.txt)
add_custom_command(OUTPUT ${WB_COVERED}
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/coverage.py ${WB_EXE} ${CMAKE_SOURCE_DIR}/api/imports.txt ${WB_COVERED} ${CMAKE_SOURCE_DIR}/api/later.txt ${WB_GEN}/absent.txt
	DEPENDS ${WB_EXE} ${CMAKE_SOURCE_DIR}/tools/coverage.py ${CMAKE_SOURCE_DIR}/api/imports.txt ${CMAKE_SOURCE_DIR}/api/later.txt
	COMMENT "helper: every import of api/imports.txt exported" VERBATIM)
# OrbiterSound.dll: the OrbiterSound SDK in the modules finds it by name; its entry points call the helper
set(WB_OS_OBJ ${CMAKE_BINARY_DIR}/helper/osound.cpp.obj)
add_custom_command(OUTPUT ${WB_OS_OBJ}
	COMMAND ${WB_CLANG} ${WB_FLAGS} -c ${CMAKE_SOURCE_DIR}/helper/osound.cpp -o ${WB_OS_OBJ}
	DEPENDS ${CMAKE_SOURCE_DIR}/helper/osound.cpp ${CMAKE_SOURCE_DIR}/helper/osound.h
	COMMENT "helper: osound.cpp" VERBATIM)
set(WB_OS_DLL ${CMAKE_BINARY_DIR}/helper/OrbiterSound.dll)
add_custom_command(OUTPUT ${WB_OS_DLL}
	COMMAND ${WB_LLD_LINK} /nologo /safeseh:no /dll /noentry /nodefaultlib /out:${WB_OS_DLL} /implib:${CMAKE_BINARY_DIR}/helper/OrbiterSound.lib
		${WB_OS_OBJ} ${WB_IMPLIB}
	DEPENDS ${WB_OS_OBJ} ${WB_IMPLIB}
	COMMENT "helper: OrbiterSound.dll" VERBATIM)
# XRSound.dll: the XRSound SDK in the modules finds it by name; its engines are the helper's
set(WB_XRS_OBJ ${CMAKE_BINARY_DIR}/helper/xrsdll.cpp.obj)
add_custom_command(OUTPUT ${WB_XRS_OBJ}
	COMMAND ${WB_CLANG} ${WB_FLAGS} -c ${CMAKE_SOURCE_DIR}/helper/xrsdll.cpp -o ${WB_XRS_OBJ}
	DEPENDS ${CMAKE_SOURCE_DIR}/helper/xrsdll.cpp ${CMAKE_SOURCE_DIR}/helper/xrsound.h
	COMMENT "helper: xrsdll.cpp" VERBATIM)
set(WB_XRS_DLL ${CMAKE_BINARY_DIR}/helper/XRSound.dll)
add_custom_command(OUTPUT ${WB_XRS_DLL}
	COMMAND ${WB_LLD_LINK} /nologo /safeseh:no /dll /noentry /nodefaultlib /out:${WB_XRS_DLL} /implib:${CMAKE_BINARY_DIR}/helper/XRSound.lib
		${WB_XRS_OBJ} ${WB_IMPLIB}
	DEPENDS ${WB_XRS_OBJ} ${WB_IMPLIB}
	COMMENT "helper: XRSound.dll" VERBATIM)
# ---------------------------------------------------------------------
# the script interpreter of Windows modules, as Orbiter.exe's: Orbiter's Lua (Extern/Lua's release and hash) as lua.dll, and lua5.1.dll
# forwarding to it (one Lua for both names); Orbiter's interpreter (lua/: the upstream Windows source, the bridge's changes marked) as
# LuaInterpreter.dll and LuaInline.dll
FetchContent_Declare(wb_lua515 URL https://www.lua.org/ftp/lua-5.1.5.tar.gz URL_HASH SHA256=2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
FetchContent_MakeAvailable(wb_lua515)
set(WB_LUA_SRC_DIR ${wb_lua515_SOURCE_DIR}/src)
set(WB_LUA ${CMAKE_BINARY_DIR}/lua)
set(WB_CRT_OBJ ${CMAKE_BINARY_DIR}/helper/crt.c.obj)
file(GLOB WB_LUA_SRC ${WB_LUA_SRC_DIR}/*.c)
list(REMOVE_ITEM WB_LUA_SRC ${WB_LUA_SRC_DIR}/lua.c ${WB_LUA_SRC_DIR}/luac.c ${WB_LUA_SRC_DIR}/print.c)
# lua.dll's API is lua/wb_luaapi.c's: a Linux state's calls go to the helper (Orbiter.exe), a real one's to Lua's core, whose API is
# renamed (lua/wb_rename.h). Numbers convert to integers rounded, as MSVC's build does (luaconf.h: no __SSE2__ there)
set(WB_LUA_LIBS lauxlib.c lbaselib.c ldblib.c liolib.c lmathlib.c loslib.c lstrlib.c ltablib.c loadlib.c linit.c)
set(WB_LUA_CC ${WB_CLANG} --target=i686-windows -fms-extensions -nostdinc -isystem ${WB_CLANG_RES}/include -isystem ${WB_WINE_INCLUDE}/msvcrt
	-isystem ${WB_WINE_INCLUDE}/windows -U__SSE2__ -DNDEBUG -O2)
set(WB_LUA_OBJS)
foreach(path ${WB_LUA_SRC})
	get_filename_component(name ${path} NAME)
	set(obj ${WB_LUA}/${name}.obj)
	if(name IN_LIST WB_LUA_LIBS)
		set(kind -DLUA_BUILD_AS_DLL)
	else()
		set(kind -include ${CMAKE_SOURCE_DIR}/lua/wb_rename.h)
	endif()
	add_custom_command(OUTPUT ${obj}
		COMMAND ${WB_LUA_CC} ${kind} -w -c ${path} -o ${obj}
		DEPENDS ${path} ${CMAKE_SOURCE_DIR}/lua/wb_rename.h
		COMMENT "lua: ${name}" VERBATIM)
	list(APPEND WB_LUA_OBJS ${obj})
endforeach()
set(obj ${WB_LUA}/wb_luaapi.c.obj)
add_custom_command(OUTPUT ${obj}
	COMMAND ${WB_LUA_CC} -DLUA_BUILD_AS_DLL -I${WB_LUA_SRC_DIR} -Wall -c ${CMAKE_SOURCE_DIR}/lua/wb_luaapi.c -o ${obj}
	DEPENDS ${CMAKE_SOURCE_DIR}/lua/wb_luaapi.c ${CMAKE_SOURCE_DIR}/lua/wb_luaapi.h
	COMMENT "lua: wb_luaapi.c" VERBATIM)
list(APPEND WB_LUA_OBJS ${obj})
set(WB_LUA_DLL ${WB_LUA}/lua.dll)
add_custom_command(OUTPUT ${WB_LUA_DLL} ${WB_LUA}/lua.lib
	COMMAND ${WB_LLD_LINK} /nologo /safeseh:no /dll /noentry /def:${CMAKE_SOURCE_DIR}/lua/lua.def /out:${WB_LUA_DLL} /implib:${WB_LUA}/lua.lib ${WB_LUA_OBJS}
		${WB_CRT_OBJ} ${WB_IMPLIB} ${WB_WINE_LIB}/libucrtbase.a ${WB_WINE_LIB}/libkernel32.a ${WB_WINE_LIB}/libcompiler-rt.a
	DEPENDS ${WB_LUA_OBJS} ${WB_CRT_OBJ} ${WB_IMPLIB} ${CMAKE_SOURCE_DIR}/lua/lua.def
	COMMENT "lua: lua.dll" VERBATIM)
set(WB_LUA51_DLL ${WB_LUA}/lua5.1.dll)
add_custom_command(OUTPUT ${WB_LUA51_DLL}
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/forward.py ${WB_LUA_DLL} lua ${WB_LUA}/lua51.def
	COMMAND ${WB_LLD_LINK} /nologo /safeseh:no /dll /noentry /nodefaultlib /machine:x86 /def:${WB_LUA}/lua51.def /out:${WB_LUA51_DLL} ${WB_CRT_OBJ}
	DEPENDS ${WB_LUA_DLL} ${CMAKE_SOURCE_DIR}/tools/forward.py ${CMAKE_SOURCE_DIR}/tools/pe.py ${WB_CRT_OBJ}
	COMMENT "lua: lua5.1.dll" VERBATIM)
# the interpreter as Orbiter builds it: C++17, NOMINMAX, the SDK's Lua headers
set(WB_LUAI_FLAGS --target=i686-windows -std=c++17 -fms-extensions -fms-compatibility -fno-threadsafe-statics -nostdinc -isystem ${WB_CLANG_RES}/include
	-isystem ${WB_WINE_INCLUDE}/msvcrt -isystem ${WB_WINE_INCLUDE}/windows -isystem ${CMAKE_SOURCE_DIR}/lua/shim -isystem ${CMAKE_SOURCE_DIR}/helper/shim
	-isystem ${WB_WIN_SDK} -I${CMAKE_SOURCE_DIR}/lua/LuaInterpreter -I${CMAKE_SOURCE_DIR}/lua -I${CMAKE_SOURCE_DIR}/helper -DNOMINMAX -DNDEBUG -O2 -fno-exceptions
	-fno-rtti)
set(WB_LUAI_OBJS)
foreach(src LuaInterpreter/Interpreter.cpp LuaInterpreter/lua_vessel_mtd.cpp wb_interp.cpp wb_crt.cpp)
	string(REPLACE "/" "_" name ${src})
	set(obj ${WB_LUA}/${name}.obj)
	set(warn -w) # upstream's files as they are
	if(src MATCHES "^wb_")
		set(warn -Wall -Wno-microsoft-cast)
	endif()
	add_custom_command(OUTPUT ${obj}
		COMMAND ${WB_CLANG} ${WB_LUAI_FLAGS} ${warn} -c ${CMAKE_SOURCE_DIR}/lua/${src} -o ${obj}
		DEPENDS ${CMAKE_SOURCE_DIR}/lua/${src} ${CMAKE_SOURCE_DIR}/lua/LuaInterpreter/Interpreter.h ${CMAKE_SOURCE_DIR}/lua/wb_interp.h ${WB_LUA_DLL}
		COMMENT "lua: ${src}" VERBATIM)
	list(APPEND WB_LUAI_OBJS ${obj})
endforeach()
set(WB_LUAI_DLL ${WB_LUA}/LuaInterpreter.dll)
add_custom_command(OUTPUT ${WB_LUAI_DLL} ${WB_LUA}/LuaInterpreter.lib
	COMMAND ${WB_LLD_LINK} /nologo /safeseh:no /dll /opt:noicf /entry:DllMainCRTStartup /out:${WB_LUAI_DLL} /implib:${WB_LUA}/LuaInterpreter.lib
		/def:${CMAKE_SOURCE_DIR}/lua/compat.def ${WB_LUAI_OBJS} ${WB_CRT_OBJ} ${WB_LUA}/lua.lib ${WB_IMPLIB} ${WB_WINE_LIB}/libc++.a ${WB_WINE_LIB}/libwinecrt0.a
		${WB_WINE_LIB}/libucrtbase.a ${WB_WINE_LIB}/libkernel32.a ${WB_WINE_LIB}/libuser32.a ${WB_WINE_LIB}/libcompiler-rt.a
	DEPENDS ${WB_LUAI_OBJS} ${WB_CRT_OBJ} ${WB_LUA_DLL} ${WB_IMPLIB} ${CMAKE_SOURCE_DIR}/lua/compat.def
	COMMENT "lua: LuaInterpreter.dll" VERBATIM)
set(WB_LUAINLINE_OBJS)
foreach(src LuaInline/LuaInline.cpp Orbitersdk/Orbitersdk.cpp)
	string(REPLACE "/" "_" name ${src})
	set(obj ${WB_LUA}/${name}.obj)
	add_custom_command(OUTPUT ${obj}
		COMMAND ${WB_CLANG} ${WB_LUAI_FLAGS} -w -c ${CMAKE_SOURCE_DIR}/lua/${src} -o ${obj}
		DEPENDS ${CMAKE_SOURCE_DIR}/lua/${src} ${CMAKE_SOURCE_DIR}/lua/LuaInline/LuaInline.h ${CMAKE_SOURCE_DIR}/lua/LuaInterpreter/Interpreter.h ${WB_LUA_DLL}
		COMMENT "lua: ${src}" VERBATIM)
	list(APPEND WB_LUAINLINE_OBJS ${obj})
endforeach()
set(WB_LUAINLINE_DLL ${WB_LUA}/LuaInline.dll)
add_custom_command(OUTPUT ${WB_LUAINLINE_DLL}
	COMMAND ${WB_LLD_LINK} /nologo /safeseh:no /dll /entry:DllMainCRTStartup /out:${WB_LUAINLINE_DLL} ${WB_LUAINLINE_OBJS} ${WB_CRT_OBJ} ${WB_LUA}/wb_crt.cpp.obj
		${WB_LUA}/LuaInterpreter.lib ${WB_LUA}/lua.lib ${WB_IMPLIB} ${WB_WINE_LIB}/libc++.a ${WB_WINE_LIB}/libwinecrt0.a ${WB_WINE_LIB}/libucrtbase.a
		${WB_WINE_LIB}/libkernel32.a ${WB_WINE_LIB}/libcompiler-rt.a
	DEPENDS ${WB_LUAINLINE_OBJS} ${WB_CRT_OBJ} ${WB_LUA}/wb_crt.cpp.obj ${WB_LUAI_DLL} ${WB_IMPLIB}
	COMMENT "lua: LuaInline.dll" VERBATIM)
set(WB_LUA_ALL ${WB_LUA_DLL} ${WB_LUA51_DLL} ${WB_LUAI_DLL} ${WB_LUAINLINE_DLL})
# every name a 2010 or 2016 module may import from them (api/lua51.txt, api/luainterp.txt)
set(WB_LUA_CHECKED ${WB_LUA}/lua.checked ${WB_LUA}/lua51.checked ${WB_LUA}/LuaInterpreter.checked)
add_custom_command(OUTPUT ${WB_LUA_CHECKED}
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/exportcheck.py ${WB_LUA_DLL} ${CMAKE_SOURCE_DIR}/api/lua51.txt ${WB_LUA}/lua.checked
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/exportcheck.py ${WB_LUA51_DLL} ${CMAKE_SOURCE_DIR}/api/lua51.txt ${WB_LUA}/lua51.checked
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/exportcheck.py ${WB_LUAI_DLL} ${CMAKE_SOURCE_DIR}/api/luainterp.txt ${WB_LUA}/LuaInterpreter.checked
	DEPENDS ${WB_LUA_ALL} ${CMAKE_SOURCE_DIR}/tools/exportcheck.py ${CMAKE_SOURCE_DIR}/tools/pe.py ${CMAKE_SOURCE_DIR}/api/lua51.txt ${CMAKE_SOURCE_DIR}/api/luainterp.txt
	COMMENT "lua: every name of api/lua51.txt and api/luainterp.txt exported" VERBATIM)
list(APPEND WB_LUA_ALL_CHECKED ${WB_LUA_ALL} ${WB_LUA_CHECKED})
if(WB_REQUIRE_ALL)
	add_custom_target(WineBridgeHelper ALL DEPENDS ${WB_EXE} ${WB_OS_DLL} ${WB_XRS_DLL} ${WB_LUA_ALL_CHECKED} ${WB_COVERED})
else()
	add_custom_target(WineBridgeHelper ALL DEPENDS ${WB_EXE} ${WB_OS_DLL} ${WB_XRS_DLL} ${WB_LUA_ALL})
endif()
install(FILES ${WB_EXE} ${WB_OS_DLL} ${WB_XRS_DLL} ${WB_LUA_ALL} DESTINATION Modules/WineBridge)
