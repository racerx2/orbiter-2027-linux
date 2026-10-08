# Copyright (c) Martin Schweiger
# Licensed under the MIT License

FetchContent_MakeAvailable(imgui)
FetchContent_MakeAvailable(implot)
install(FILES ${imgui_SOURCE_DIR}/LICENSE.txt DESTINATION ${ORBITER_INSTALL_DOC_DIR}/Licenses RENAME ImGui.txt) # not upstream: licence text (Doc/Licenses/README.txt)
install(FILES ${implot_SOURCE_DIR}/LICENSE DESTINATION ${ORBITER_INSTALL_DOC_DIR}/Licenses RENAME ImPlot.txt) # not upstream: licence text (Doc/Licenses/README.txt)

# Sources for all Orbiter executable targets
set(common_src
# General source files
	Astro.cpp
	Camera.cpp
	cmdline.cpp
	Config.cpp
	console_ng.cpp
	Element.cpp
	elevmgr.cpp
	Help.cpp
	Input.cpp
	Keymap.cpp
	LightEmitter.cpp
	Mesh.cpp
	Nav.cpp
	Orbiter.cpp
	PlaybackEd.cpp
	Psys.cpp
	Script.cpp
	Shadow.cpp
	State.cpp
	Vecmat.cpp
	VectorMap.cpp
    ConsoleManager.cpp
	TimeData.cpp
# Launchpad
	Launchpad.cpp
	LpadTab.cpp
	TabAbout.cpp
	TabExtra.cpp
	TabModule.cpp
	TabOptions.cpp
	TabScenario.cpp
	TabVideo.cpp
	TexInstall.cpp # not upstream: missing planet texture download
	TexPack.cpp # not upstream
# Dialogs
	CustomControls.cpp
	DialogWin.cpp
	DlgCamera.cpp
	DlgCapture.cpp
	DlgFocus.cpp
	DlgFunction.cpp
	DlgHelp.cpp
	DlgInfo.cpp
	DlgMap.cpp
	DlgMenuCfg.cpp
	DlgMgr.cpp
	DlgOptions.cpp
	DlgRecorder.cpp
	DlgTacc.cpp
	htmlctrl.cpp
	ChmHelp.cpp
	ResDialog.cpp
	Select.cpp
	OptionsPages.cpp
# Body classes
	Body.cpp
	BodyIntegrator.cpp
	PinesGrav.cpp
	Celbody.cpp
	Planet.cpp
	Rigidbody.cpp
	Star.cpp
# Vessel classes
	FlightRecorder.cpp
	SuperVessel.cpp
	Vessel.cpp
	Vesselbase.cpp
	Vesselstatus.cpp
# Surface base classes
	Base.cpp
	Baseobj.cpp
# Cockpit classes
	Defpanel.cpp
	hud.cpp
	MenuInfoBar.cpp
	Pane.cpp
	Panel.cpp
	Panel2D.cpp
	VCockpit.cpp
# MFD classes
	Mfd.cpp
	MfdAlign.cpp
	MfdComms.cpp
	MfdDocking.cpp
	MfdHsi.cpp
	MfdLanding.cpp
	MfdMap.cpp
	MfdMap_old.cpp
	MfdOrbit.cpp
	MfdSurface.cpp
	MfdSync.cpp
	MfdTransfer.cpp
	MfdUser.cpp
# API implementations
	CamAPI.cpp
	CelSphereAPI.cpp
	DrawAPI.cpp
	GraphicsAPI.cpp
	MFDAPI.cpp
	ModuleAPI.cpp
	OrbiterAPI.cpp
# Graphics utils
	D3d7util.cpp
	D3dmath.cpp
	Di7frame.cpp
# Utils
	Log.cpp
	Memstat.cpp
	Util.cpp
	PathResolve.cpp
	SleepWatch.cpp
	WlPointer.cpp
	WlShortcuts.cpp
	ZTreeMgr.cpp
# ImGui platform backend
	imgui_impl_qt.cpp
)

set(Orbiter_includes
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/Orbitersdk/include
	${CMAKE_SOURCE_DIR}/OVP
	${CMAKE_CURRENT_BINARY_DIR}
)

# Qt Widgets carries the Launchpad and dialogs; libdl the module loading
set(Orbiter_common_libs
	zlib
	Orbitersdk
	DlgCtrl
	Qt6::Widgets
	Qt6::DBus
	Qt6::Network # not upstream: TexPack.cpp downloads
	${CMAKE_DL_LIBS}
)
set(Orbiter_libs
)

set(Orbiter_depends
	Orbitersdk
	DlgCtrl
)

# Orbiter executable (graphics server version)
add_executable(Orbiter ${common_src})
include(Custom/Custom.cmake) # custom: launcher skins

# the dialog templates, bitmaps, icons and strings of Orbiter.rc as a C++ resource table
orbiter_rc_resources(Orbiter Orbiter.rc EXE SYMBOL OrbiterResources)

# WlPointer.cpp, WlShortcuts.cpp: client code of the Wayland pointer-constraints, relative-pointer and
# keyboard-shortcuts-inhibit protocols
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols) # not upstream: configure fails clearly without it
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
foreach(proto pointer-constraints relative-pointer keyboard-shortcuts-inhibit)
	set(xml ${WAYLAND_PROTOCOLS_DIR}/unstable/${proto}/${proto}-unstable-v1.xml)
	set(base ${CMAKE_CURRENT_BINARY_DIR}/${proto}-unstable-v1)
	add_custom_command(OUTPUT ${base}-client-protocol.h ${base}-protocol.c
		COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${base}-client-protocol.h
		COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${base}-protocol.c
		DEPENDS ${xml}
	)
	target_sources(Orbiter PRIVATE ${base}-client-protocol.h ${base}-protocol.c)
endforeach()
target_link_libraries(Orbiter PkgConfig::WAYLAND_CLIENT)

set_target_properties(Orbiter
	PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
	OUTPUT_NAME Orbiter
	COMPILE_DEFINITIONS "NOGRAPHICS"
	FOLDER Core
	AUTOMOC ON
)

target_include_directories(Orbiter PUBLIC ${Orbiter_includes} ${TRACY_CLIENT_INCLUDE} ${imgui_SOURCE_DIR}/ ${imgui_SOURCE_DIR}/backends ${implot_SOURCE_DIR}/)

target_link_libraries(Orbiter ${Orbiter_common_libs} ${Orbiter_libs} ${TRACY_CLIENT})

add_dependencies(Orbiter ${Orbiter_depends})

# Installation: Orbiter executable (graphics-server versions)
install(TARGETS Orbiter	RUNTIME DESTINATION ${ORBITER_INSTALL_ROOT_DIR})

# If the standard Orbiter executable is not built...
set_target_properties(Orbiter PROPERTIES ENABLE_EXPORTS 1)

# no import library on ELF: modules bind the exe's exported oapi* symbols when they are loaded

string(TIMESTAMP DATE "%d %b %Y")
string(TIMESTAMP VERSION "%y%m%d")
configure_file(about.hpp.in about.hpp @ONLY)
