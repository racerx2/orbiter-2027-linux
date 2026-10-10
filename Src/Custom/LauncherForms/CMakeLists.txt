
# custom: forms skins; Modules/Launcher/LauncherForms.so, loaded by Orbiter only when a forms skin is used

find_package(Qt6 QUIET COMPONENTS Widgets Qml)
if(Qt6Widgets_FOUND AND Qt6Qml_FOUND)
	set(LAUNCHER_FORMS_FOUND ON)
else()
	set(LAUNCHER_FORMS_FOUND OFF)
endif()
option(ORBITER_BUILD_LAUNCHER_FORMS "Build the forms launcher skin module (needs Qt QML)" ${LAUNCHER_FORMS_FOUND})

if(ORBITER_BUILD_LAUNCHER_FORMS AND LAUNCHER_FORMS_FOUND)
	# everything but the entry points, also for the tests
	add_library(LauncherFormsCore STATIC
		FormBuild.cpp
		FormPainted.cpp
		FormRuntime.cpp
		FormWidgets.cpp
		FormsView.cpp
		Orbits.cpp
		${ORBITER_SOURCE_ROOT_DIR}/Src/Orbiter/Custom/UiForm.cpp
		${ORBITER_SOURCE_ROOT_DIR}/Src/Orbiter/Custom/SkinStyle.cpp
	)
	target_include_directories(LauncherFormsCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${ORBITER_SOURCE_ROOT_DIR}/Src/Orbiter/Custom)
	target_link_libraries(LauncherFormsCore PUBLIC Qt6::Widgets Qt6::Qml)
	target_compile_options(LauncherFormsCore PRIVATE -ffp-contract=off)
	set_target_properties(LauncherFormsCore PROPERTIES
		AUTOMOC ON
		POSITION_INDEPENDENT_CODE ON
		CXX_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON
		FOLDER Custom
	)

	add_library(LauncherForms MODULE LauncherForms.cpp)
	target_link_libraries(LauncherForms PRIVATE LauncherFormsCore)
	target_link_options(LauncherForms PRIVATE -Wl,-Bsymbolic-functions) # its own UiForm and SkinStyle, not the ones Orbiter exports
	set_target_properties(LauncherForms PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON
		LIBRARY_OUTPUT_DIRECTORY ${ORBITER_BINARY_MODULE_DIR}/Launcher
		FOLDER Custom
	)
	install(TARGETS LauncherForms LIBRARY DESTINATION ${ORBITER_INSTALL_MODULE_DIR}/Launcher)
else()
	message(STATUS "Forms launcher skin module not built (Qt QML not found or ORBITER_BUILD_LAUNCHER_FORMS off): forms skins fall back to classic")
endif()
