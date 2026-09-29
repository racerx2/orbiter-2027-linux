# not upstream: module dialogs and drawing are Qt (the SDK's HWND/HDC are QWidget/QPainter)
set(CMAKE_AUTOMOC ON)
link_libraries(Qt6::Widgets)

# The target directory for plugin DLLs
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${ORBITER_BINARY_PLUGIN_DIR})

# Common routines for inclusion by plugins
set(MODULE_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Common)

# The subdirectories with individual plugin implementations
add_subdirectory(ExtMFD)
add_subdirectory(FlightData)
add_subdirectory(Framerate)
add_subdirectory(LaunchpadExtensions)
add_subdirectory(LuaConsole)
add_subdirectory(LuaMFD)
add_subdirectory(Meshdebug)
add_subdirectory(Rcontrol)
add_subdirectory(ScnEditor)
add_subdirectory(ScriptMFD)
# TrackIR left out: NaturalPoint's NPClient library and its registry lookup are Windows only
add_subdirectory(TransX)
add_subdirectory(Notes)

# ==========================================================================
# SDK Sample Installation
# ==========================================================================

set(IgnoreFiles
	PATTERN CMakeLists.txt EXCLUDE
	PATTERN Textures EXCLUDE
	PATTERN Textures2 EXCLUDE
	PATTERN Config EXCLUDE
	PATTERN Meshes EXCLUDE
	PATTERN Thumbnail EXCLUDE
)

install(DIRECTORY 
	LuaConsole
	LuaMFD
	ScriptMFD
	TransX
	DESTINATION ${ORBITER_INSTALL_SDK_DIR}/samples ${IgnoreFiles}
)
