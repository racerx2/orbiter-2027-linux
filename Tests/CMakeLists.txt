Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1 # not upstream: v3.0.0-preview3 sizes a static array with MINSIGSTKSZ (sysconf since glibc 2.34)
)

set(BUILD_SHARED_LIBS OFF) # not upstream: static Catch2; tracy.cmake turns shared libs on
FetchContent_MakeAvailable(Catch2)

# Utility function
function(add_test_file test_name)
	add_executable(${test_name} "${test_name}.cpp")

	set_target_properties( ${test_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${ORBITER_BINARY_ROOT_DIR}" )

	target_include_directories(${test_name}
		PRIVATE ${ORBITER_SOURCE_SDK_INCLUDE_DIR}
		PRIVATE ${MODULE_COMMON_DIR}
		PRIVATE ${ORBITER_SOURCE_ROOT_DIR}/Src/Module/LuaScript/LuaInterpreter
	)

	target_link_libraries(${test_name}
		${ORBITER_LIB}
		${ORBITER_SDK_LIB}
		lua::lib
		LuaInterpreter
		Catch2::Catch2WithMain
	)

	add_dependencies(${test_name}
		${OrbiterTgt}
		Orbitersdk
		LuaInterpreter
	)

	add_test( 
		NAME ${test_name} 
		COMMAND $<TARGET_FILE:${test_name}>
		WORKING_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
	)
	set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
endfunction()

# not upstream: unit tests for ported code that runs without the Orbiter exe; extra args are the port sources under test
function(add_unit_test test_name)
	add_executable(${test_name} "${test_name}.cpp" ${ARGN})

	set_target_properties( ${test_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${ORBITER_BINARY_ROOT_DIR}/Tests" )

	target_include_directories(${test_name}
		PRIVATE ${ORBITER_SOURCE_SDK_INCLUDE_DIR}
		PRIVATE ${ORBITER_SOURCE_DIR}
	)

	target_link_libraries(${test_name}
		Catch2::Catch2WithMain
	)

	add_test(
		NAME ${test_name}
		COMMAND $<TARGET_FILE:${test_name}>
		WORKING_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
	)
	set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
endfunction()

# Register unit tests
add_test_file(Lua.Interpreter)
# not upstream: LuaInterpreter.so imports the exe-side API (Orbiter.exe on Windows); functions bind lazily, the rest comes from test doubles
target_sources(Lua.Interpreter PRIVATE Lua.Interpreter.Exe.cpp)
set_target_properties(Lua.Interpreter PROPERTIES ENABLE_EXPORTS ON)
target_link_options(Lua.Interpreter PRIVATE -Wl,--unresolved-symbols=ignore-in-shared-libs)

# not upstream: proves the Catch2/ctest harness builds and runs
add_unit_test(Port.Harness)
# not upstream: core utilities
add_unit_test(Vecmat.Test ${ORBITER_SOURCE_DIR}/Vecmat.cpp)
add_unit_test(Astro.Test ${ORBITER_SOURCE_DIR}/Astro.cpp ${ORBITER_SOURCE_DIR}/TimeData.cpp)
add_unit_test(PathResolve.Test ${ORBITER_SOURCE_DIR}/PathResolve.cpp)
add_unit_test(Input.Test ${ORBITER_SOURCE_DIR}/Di7frame.cpp ${ORBITER_SOURCE_DIR}/Memstat.cpp)
# not upstream: Celbody modules, loaded like Orbiter loads them; the test exports the exe-side API they bind to
add_unit_test(Celbody.Test ${ORBITER_SOURCE_DIR}/PathResolve.cpp)
set_target_properties(Celbody.Test PROPERTIES ENABLE_EXPORTS ON)
add_dependencies(Celbody.Test Sol Sun Mercury Venus Earth Mars Jupiter Saturn Uranus Neptune Moon Io Europa Ganymede Callisto
	Mimas Enceladus Tethys Dione Rhea Titan Hyperion Iapetus Phobos Deimos Miranda Ariel Umbriel Titania Oberon Triton Proteus Nereid Vesta Pluto Charon Styx Nix Kerberos Hydra EarthAtm2006 EarthAtmJ71G EarthAtmNRLMSISE00 MarsAtm2006 VenusAtm2006 TitanAtm2005 JupiterAtm1995 SaturnAtm2017 UranusAtm1986 NeptuneAtm1989 IoAtm2007)
# not upstream: dialog builder on Orbiter.rc, rendered offscreen
add_unit_test(ResDialog.Test ${ORBITER_SOURCE_DIR}/ResDialog.cpp ${ORBITER_SOURCE_DIR}/ResDialog.h)
set_target_properties(ResDialog.Test PROPERTIES AUTOMOC ON)
target_link_libraries(ResDialog.Test Qt6::Widgets DlgCtrl)
orbiter_rc_resources(ResDialog.Test ${ORBITER_SOURCE_DIR}/Orbiter.rc EXE SYMBOL OrbiterResources)
orbiter_rc_resources(ResDialog.Test ${CMAKE_CURRENT_SOURCE_DIR}/ResMenu.Test.rc SYMBOL TestMenuResources)
set_tests_properties(ResDialog.Test PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
# not upstream: help viewer (.chm pages, help window beside modal dialogs) and the ImGui Qt backend, offscreen
find_package(Qt6 REQUIRED COMPONENTS Test)
add_unit_test(ChmHelp.Test ${ORBITER_SOURCE_DIR}/ChmHelp.cpp ${ORBITER_SOURCE_DIR}/PathResolve.cpp)
target_link_libraries(ChmHelp.Test Qt6::Widgets Qt6::Test zlib)
set_tests_properties(ChmHelp.Test PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
add_unit_test(ImGuiQt.Test ${ORBITER_SOURCE_DIR}/imgui_impl_qt.cpp)
target_link_libraries(ImGuiQt.Test Orbitersdk Qt6::Gui)
set_tests_properties(ImGuiQt.Test PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
# not upstream: rc2cpp's #if evaluator (C semantics, errors instead of tracebacks)
add_test(NAME RcCond.Test COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/RcCond.Test.py ${ORBITER_SOURCE_ROOT_DIR}/cmake)
# not upstream: folder listings in NTFS order, MakePath under a long working directory
add_unit_test(Util.Test ${ORBITER_SOURCE_DIR}/Util.cpp ${ORBITER_SOURCE_DIR}/PathResolve.cpp)
target_link_libraries(Util.Test Qt6::Widgets)
# not upstream: the .msh reader on malformed files, built with ASan
add_unit_test(Mesh.Test ${ORBITER_SOURCE_DIR}/Mesh.cpp ${ORBITER_SOURCE_DIR}/PathResolve.cpp)
target_compile_definitions(Mesh.Test PRIVATE NOGRAPHICS)
target_compile_options(Mesh.Test PRIVATE -fsanitize=address -fno-omit-frame-pointer)
target_link_options(Mesh.Test PRIVATE -fsanitize=address)
# not upstream: missing planet textures: package list, check, zip installer, file:// download; built with ASan
add_unit_test(TexPack.Test ${ORBITER_SOURCE_DIR}/TexPack.cpp ${ORBITER_SOURCE_DIR}/PathResolve.cpp)
target_link_libraries(TexPack.Test Qt6::Network zlib)
target_compile_options(TexPack.Test PRIVATE -fsanitize=address -fno-omit-frame-pointer)
target_link_options(TexPack.Test PRIVATE -fsanitize=address)
# custom: launcher skins; the parsers, and the classic-control hider and reset key on IDD_MAIN offscreen
add_unit_test(LauncherFacts.Test ${ORBITER_SOURCE_DIR}/Custom/LauncherFacts.cpp)
target_compile_definitions(LauncherFacts.Test PRIVATE SCN_DIR="${CMAKE_SOURCE_DIR}/Scenarios")
add_unit_test(ClassicHider.Test ${ORBITER_SOURCE_DIR}/Custom/ClassicHider.cpp ${ORBITER_SOURCE_DIR}/Custom/ResetKey.cpp ${ORBITER_SOURCE_DIR}/ResDialog.cpp ${ORBITER_SOURCE_DIR}/ResDialog.h)
set_target_properties(ClassicHider.Test PROPERTIES AUTOMOC ON)
target_link_libraries(ClassicHider.Test Qt6::Widgets Qt6::Test DlgCtrl)
orbiter_rc_resources(ClassicHider.Test ${ORBITER_SOURCE_DIR}/Orbiter.rc EXE SYMBOL OrbiterResources)
set_tests_properties(ClassicHider.Test PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
# custom: forms skins; Copy... in the skin picker
add_unit_test(SkinCopy.Test ${ORBITER_SOURCE_DIR}/Custom/SkinCopy.cpp ${ORBITER_SOURCE_DIR}/Custom/LauncherFacts.cpp)
target_link_libraries(SkinCopy.Test Qt6::Core)
# custom: launcher layouts; export, the .ui reader and writer (uic on every export), applying and undoing, offscreen
add_unit_test(LayoutApply.Test ${ORBITER_SOURCE_DIR}/Custom/UiForm.cpp ${ORBITER_SOURCE_DIR}/Custom/LayoutApply.cpp ${ORBITER_SOURCE_DIR}/ResDialog.cpp ${ORBITER_SOURCE_DIR}/ResDialog.h)
set_target_properties(LayoutApply.Test PROPERTIES AUTOMOC ON)
target_link_libraries(LayoutApply.Test Qt6::Widgets DlgCtrl)
target_compile_definitions(LayoutApply.Test PRIVATE LAYOUT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/Layout")
orbiter_rc_resources(LayoutApply.Test ${ORBITER_SOURCE_DIR}/Orbiter.rc EXE SYMBOL OrbiterResources)
set_tests_properties(LayoutApply.Test PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 120)
# custom: forms skins; the .ui layouts, the form builder and its JavaScript runtime, the shipped forms skins, offscreen
if (TARGET LauncherFormsCore)
	add_unit_test(FormsRuntime.Test)
	set_target_properties(FormsRuntime.Test PROPERTIES AUTOMOC ON)
	target_link_libraries(FormsRuntime.Test LauncherFormsCore Qt6::Test)
	target_compile_definitions(FormsRuntime.Test PRIVATE FORMS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/Forms")
	set_tests_properties(FormsRuntime.Test PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 120)
	add_unit_test(FormsSkins.Test)
	set_target_properties(FormsSkins.Test PROPERTIES AUTOMOC ON)
	target_link_libraries(FormsSkins.Test LauncherFormsCore Qt6::Test)
	target_compile_definitions(FormsSkins.Test PRIVATE SKINS_DIR="${CMAKE_SOURCE_DIR}/Skins" FORMS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/Forms")
	set_tests_properties(FormsSkins.Test PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 300)
	# the pictures of the shipped forms skins, drawn by the painted widgets' code (a tool, run by hand)
	add_executable(FormsPreview FormsPreview.cpp)
	target_link_libraries(FormsPreview LauncherFormsCore)
	set_target_properties(FormsPreview PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${ORBITER_BINARY_ROOT_DIR}/Tests" EXCLUDE_FROM_ALL ON)
endif()
# custom: launcher skins; the QML view module and the Launcher object a QML skin sees, offscreen
if (TARGET LauncherQml)
	find_package(Qt6 REQUIRED COMPONENTS Quick QuickWidgets Qml Network)
	add_unit_test(LauncherQml.Test ${ORBITER_SOURCE_ROOT_DIR}/Src/Custom/LauncherQml/LauncherQml.cpp)
	set_target_properties(LauncherQml.Test PROPERTIES AUTOMOC ON)
	target_include_directories(LauncherQml.Test PRIVATE ${ORBITER_SOURCE_DIR}/Custom)
	target_link_libraries(LauncherQml.Test Qt6::Quick Qt6::QuickWidgets Qt6::Qml Qt6::Network)
	set_tests_properties(LauncherQml.Test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 120)
endif()

if (BUILD_ORBITER_SERVER)

	# Sanity check for scenario tests
	add_test(
		NAME "Scenario.SanityCheck"
		COMMAND $<TARGET_FILE:Orbiter_server> "--scenariox=${CMAKE_SOURCE_DIR}/Scenarios/Delta-glider/Smack!.scn" "--maxframes=60"
		WORKING_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
	)
	set_tests_properties(Scenario.SanityCheck PROPERTIES TIMEOUT 60)

	# Register scenario tests
	file(GLOB TestScenarios "${CMAKE_SOURCE_DIR}/Scenarios/Tests/*.scn")
	foreach(Scenario ${TestScenarios})
		get_filename_component(test_name ${Scenario} NAME_WE)
		add_test(
			NAME "Scenario.${test_name}"
			COMMAND $<TARGET_FILE:Orbiter_server> "--scenariox=${Scenario}"
			WORKING_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}
		)
		set_tests_properties(Scenario.${test_name} PROPERTIES TIMEOUT 60)
	endforeach()

endif()
