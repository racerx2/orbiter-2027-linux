# GPSNav: a panel GPS navigator add-on for Orbiter (Linux); built with Orbiter under Src/Plugin, or standalone against an Orbiter folder's SDK
cmake_minimum_required(VERSION 3.19...3.31)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(GPSNav LANGUAGES CXX)
	set(GPSNAV_STANDALONE ON)
	set(CMAKE_CXX_STANDARD 20)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
		set(CMAKE_BUILD_TYPE Release)
	endif()
	set(CMAKE_CXX_VISIBILITY_PRESET hidden)
	set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
	set(CMAKE_SHARED_LIBRARY_PREFIX "")

	set(ORBITER_ROOT "" CACHE PATH "the Orbiter folder the add-on is built against and installed into")
	if(NOT EXISTS "${ORBITER_ROOT}/Orbitersdk/lib/cmake/Orbiter/OrbiterConfig.cmake")
		message(FATAL_ERROR "ORBITER_ROOT must be an Orbiter folder with Orbitersdk/lib/cmake/Orbiter")
	endif()
	if(NOT EXISTS "${ORBITER_ROOT}/Orbitersdk/include/imgui.h")
		message(FATAL_ERROR "${ORBITER_ROOT}/Orbitersdk/include has no imgui.h: an Orbiter of 2026-10-31 or later")
	endif()
	find_package(Orbiter CONFIG REQUIRED PATHS "${ORBITER_ROOT}/Orbitersdk/lib/cmake/Orbiter" NO_DEFAULT_PATH)
	find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network) # the map's tiles, with Orbiter's own Qt
	if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
		set(CMAKE_INSTALL_PREFIX "${ORBITER_ROOT}" CACHE PATH "" FORCE)
	endif()
	set(GPSNAV_OUT ${CMAKE_BINARY_DIR})
	set(GPSNAV_DEST .)
	option(GPSNAV_TESTS "unit tests (Catch2, fetched)" ON)
	set(GPSNAV_MAKE_TESTS ${GPSNAV_TESTS})
else()
	set(GPSNAV_STANDALONE OFF)
	set(CMAKE_AUTOMOC OFF)
	set_property(DIRECTORY PROPERTY LINK_LIBRARIES "") # drops Src/Plugin's Qt6::Widgets
	set(GPSNAV_OUT ${ORBITER_BINARY_ROOT_DIR})
	set(GPSNAV_DEST ${ORBITER_INSTALL_ROOT_DIR})
	set(GPSNAV_MAKE_TESTS ${ORBITER_MAKE_TESTS})
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# the parts that don't need Orbiter, for the module and the tests
add_library(gpsnav_core STATIC src/Data.cpp src/Nav.cpp src/Colour.cpp src/Map.cpp)
target_include_directories(gpsnav_core PUBLIC src)

add_library(GPSNav SHARED src/Module.cpp src/Unit.cpp src/Sources.cpp src/Terrain.cpp src/MapData.cpp)
set_target_properties(GPSNav PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${GPSNAV_OUT}/Modules/Plugin)
if(GPSNAV_STANDALONE)
	target_link_libraries(GPSNav PRIVATE gpsnav_core Orbiter::Orbitersdk Qt6::Core Qt6::Network)
else()
	target_include_directories(GPSNav SYSTEM PRIVATE ${ORBITER_SOURCE_SDK_INCLUDE_DIR}) # its warnings are the SDK's, as an imported SDK's are
	target_link_libraries(GPSNav PRIVATE gpsnav_core ${ORBITER_LIB} ${ORBITER_SDK_LIB} imgui Qt6::Core Qt6::Network)
	add_dependencies(GPSNav ${OrbiterTgt} Orbitersdk)
	set_target_properties(GPSNav gpsnav_core PROPERTIES FOLDER Modules/GPSNav)

	# the data in the build tree Orbiter runs from; GPSNav/User is only made, its files stay
	add_custom_target(GPSNavData ALL
		COMMAND ${ORBITER_COPY_NEWER} ${CMAKE_CURRENT_SOURCE_DIR}/Data/ ${ORBITER_BINARY_ROOT_DIR}/GPSNav/Data
		COMMAND ${CMAKE_COMMAND} -E make_directory ${ORBITER_BINARY_ROOT_DIR}/GPSNav/User)
	set_target_properties(GPSNavData PROPERTIES FOLDER Data)
	file(COPY Textures/MenuInfoBar/GPSNav.png DESTINATION ${ORBITER_BINARY_TEXTURES_DIR}/MenuInfoBar)
endif()

# the manual as a help file Orbiter's help viewer opens (a zip with the project file)
set(GPSNAV_CHM ${GPSNAV_OUT}/Html/Plugin/GPSNav.chm)
add_custom_command(OUTPUT ${GPSNAV_CHM}
	COMMAND ${CMAKE_COMMAND} -E make_directory ${GPSNAV_OUT}/Html/Plugin
	COMMAND ${CMAKE_COMMAND} -E tar cf ${GPSNAV_CHM} --format=zip GPSNav.hhp GPSNav.htm
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Html
	DEPENDS Html/GPSNav.hhp Html/GPSNav.htm)
add_custom_target(GPSNavHelp ALL DEPENDS ${GPSNAV_CHM})

install(TARGETS GPSNav LIBRARY DESTINATION ${GPSNAV_DEST}/Modules/Plugin)
install(DIRECTORY Data/ DESTINATION ${GPSNAV_DEST}/GPSNav/Data)
install(FILES ${GPSNAV_CHM} DESTINATION ${GPSNAV_DEST}/Html/Plugin)
install(FILES Textures/MenuInfoBar/GPSNav.png DESTINATION ${GPSNAV_DEST}/Textures/MenuInfoBar)
install(DIRECTORY DESTINATION ${GPSNAV_DEST}/GPSNav/User)

if(GPSNAV_MAKE_TESTS)
	if(GPSNAV_STANDALONE)
		include(FetchContent)
		FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.7.1 GIT_SHALLOW TRUE)
		set(BUILD_SHARED_LIBS OFF)
		FetchContent_MakeAvailable(Catch2)
	endif()
	add_executable(GPSNav.Test tests/Geo.Test.cpp tests/Data.Test.cpp tests/Nav.Test.cpp tests/Colour.Test.cpp tests/Map.Test.cpp)
	target_link_libraries(GPSNav.Test PRIVATE gpsnav_core Catch2::Catch2WithMain)
	target_compile_definitions(GPSNav.Test PRIVATE GPSNAV_TESTDATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data" GPSNAV_DATA="${CMAKE_CURRENT_SOURCE_DIR}/Data")
	if(GPSNAV_STANDALONE)
		enable_testing()
		add_test(NAME GPSNav.Test COMMAND GPSNav.Test)
	else()
		set_target_properties(GPSNav.Test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}/Tests FOLDER Tests/GPSNav)
		# at the end of the root file, after its enable_testing()
		cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL add_test NAME GPSNav.Test COMMAND $<TARGET_FILE:GPSNav.Test>)
		cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL set_tests_properties GPSNav.Test PROPERTIES TIMEOUT 30)
	endif()
endif()
