# custom: launcher skins; Modules/Launcher/LauncherQml.so, loaded by Orbiter only when a QML skin is used

find_package(Qt6 QUIET COMPONENTS Quick QuickWidgets Qml Network)
if(Qt6Quick_FOUND AND Qt6QuickWidgets_FOUND AND Qt6Qml_FOUND AND Qt6Network_FOUND)
	set(LAUNCHER_QML_FOUND ON)
else()
	set(LAUNCHER_QML_FOUND OFF)
endif()
option(ORBITER_BUILD_LAUNCHER_QML "Build the QML launcher skin module (needs Qt Quick)" ${LAUNCHER_QML_FOUND})

if(ORBITER_BUILD_LAUNCHER_QML AND LAUNCHER_QML_FOUND)
	add_library(LauncherQml MODULE LauncherQml.cpp)
	target_include_directories(LauncherQml PRIVATE ${ORBITER_SOURCE_ROOT_DIR}/Src/Orbiter/Custom)
	target_link_libraries(LauncherQml PRIVATE Qt6::Quick Qt6::QuickWidgets Qt6::Qml Qt6::Network)
	set_target_properties(LauncherQml PROPERTIES
		LIBRARY_OUTPUT_DIRECTORY ${ORBITER_BINARY_MODULE_DIR}/Launcher
		FOLDER Custom
	)
	install(TARGETS LauncherQml LIBRARY DESTINATION ${ORBITER_INSTALL_MODULE_DIR}/Launcher)
else()
	message(STATUS "QML launcher skin module not built (Qt Quick not found or ORBITER_BUILD_LAUNCHER_QML off): QML skins fall back to classic")
endif()
