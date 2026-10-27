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
# not upstream: independent validation of Pines gravity
add_unit_test(PinesGrav.Test ${ORBITER_SOURCE_DIR}/PinesGrav.cpp ${ORBITER_SOURCE_DIR}/PathResolve.cpp)
add_unit_test(PathResolve.Test ${ORBITER_SOURCE_DIR}/PathResolve.cpp)
add_unit_test(Input.Test ${ORBITER_SOURCE_DIR}/Di7frame.cpp ${ORBITER_SOURCE_DIR}/Memstat.cpp)
# not upstream: Celbody modules, loaded like Orbiter loads them; the test exports the exe-side API they bind to
add_unit_test(Celbody.Test ${ORBITER_SOURCE_DIR}/PathResolve.cpp)
set_target_properties(Celbody.Test PROPERTIES ENABLE_EXPORTS ON)
add_dependencies(Celbody.Test Sol Sun Mercury Venus Earth Mars Jupiter Saturn Uranus Neptune Moon Io Europa Ganymede Callisto
	Mimas Enceladus Tethys Dione Rhea Titan Hyperion Iapetus Phobos Deimos Miranda Ariel Umbriel Titania Oberon Triton Proteus Nereid Vesta Pluto Charon Styx Nix Kerberos Hydra EarthAtm2006 EarthAtmJ71G EarthAtmNRLMSISE00 MarsAtm2006 VenusAtm2006 TitanAtm2005 JupiterAtm1995 SaturnAtm2017 UranusAtm1986 NeptuneAtm1989 IoAtm2007 PlutoAtm2017 TritonAtm2022)
if(SPICE_BUILD) # not upstream: the SPICE case needs the module
	add_dependencies(Celbody.Test spice)
endif()
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
add_library(Util.Module MODULE Util.Module.cpp) # not upstream: ModuleLoad/ModuleFree counts on a module
set_target_properties(Util.Module PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${ORBITER_BINARY_ROOT_DIR}/Tests") # not upstream: as above
target_compile_definitions(Util.Test PRIVATE UTIL_MODULE="$<TARGET_FILE:Util.Module>") # not upstream: as above
add_dependencies(Util.Test Util.Module) # not upstream: as above
# not upstream: the .msh reader on malformed files, built with ASan
add_unit_test(Mesh.Test ${ORBITER_SOURCE_DIR}/Mesh.cpp ${ORBITER_SOURCE_DIR}/PathResolve.cpp ${ORBITER_SOURCE_DIR}/GltfLoader.cpp)
target_link_libraries(Mesh.Test Qt6::Core)
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
# custom: glTF mesh reader (synthetic files, and Meshes/DGTest/dgTest.glb)
add_unit_test(GltfLoader.Test ${ORBITER_SOURCE_DIR}/GltfLoader.cpp)
target_link_libraries(GltfLoader.Test Qt6::Core)
target_compile_definitions(GltfLoader.Test PRIVATE GLTF_TEST_ROOT="${ORBITER_SOURCE_ROOT_DIR}")
# custom: the glTF round trip: records, default pose, unchanged test, fitted components (synthetic files)
add_unit_test(GltfRoundTrip.Test ${ORBITER_SOURCE_DIR}/GltfLoader.cpp ${ORBITER_SOURCE_DIR}/GltfRoundTrip.cpp)
target_link_libraries(GltfRoundTrip.Test Qt6::Gui)
# custom: VulkanClient PBR material records and their config text
add_unit_test(PbrMaterial.Test ${ORBITER_SOURCE_ROOT_DIR}/OVP/VulkanClient/PbrMaterial.cpp)
target_include_directories(PbrMaterial.Test PRIVATE ${ORBITER_SOURCE_ROOT_DIR}/OVP/VulkanClient)
target_link_libraries(PbrMaterial.Test Qt6::Core) # VkGltfImage.h
# not upstream: the planet tile stream: grids, pixels, DDS, config, size cap, and the service on file:// sources
add_unit_test(TileStream.Test ${ORBITER_SOURCE_DIR}/TileStream.cpp ${ORBITER_SOURCE_DIR}/TileStreamCfg.cpp ${ORBITER_SOURCE_DIR}/TileStreamImg.cpp ${ORBITER_SOURCE_DIR}/TileStreamNet.cpp ${ORBITER_SOURCE_DIR}/TileStreamSky.cpp ${ORBITER_SOURCE_DIR}/TileStreamCog.cpp ${ORBITER_SOURCE_DIR}/TileStreamMvt.cpp)
target_link_libraries(TileStream.Test Qt6::Network Qt6::Gui stb_dxt zlib)
target_compile_options(TileStream.Test PRIVATE -fsanitize=address -fno-omit-frame-pointer)
target_link_options(TileStream.Test PRIVATE -fsanitize=address)
set_tests_properties(TileStream.Test PROPERTIES TIMEOUT 120) # room for a slow machine: the binary runs in seconds
add_unit_test(TileStreamDecode.Test ${ORBITER_SOURCE_DIR}/TileStream.cpp ${ORBITER_SOURCE_DIR}/TileStreamCfg.cpp ${ORBITER_SOURCE_DIR}/TileStreamImg.cpp ${ORBITER_SOURCE_DIR}/TileStreamNet.cpp ${ORBITER_SOURCE_DIR}/TileStreamSky.cpp ${ORBITER_SOURCE_DIR}/TileStreamCog.cpp ${ORBITER_SOURCE_DIR}/TileStreamMvt.cpp) # not upstream: the tile stream's decoders on hostile input, ASan
target_link_libraries(TileStreamDecode.Test Qt6::Network Qt6::Gui stb_dxt zlib)
target_compile_options(TileStreamDecode.Test PRIVATE -fsanitize=address -fno-omit-frame-pointer)
target_link_options(TileStreamDecode.Test PRIVATE -fsanitize=address)
add_unit_test(TileStreamCore.Test ${ORBITER_SOURCE_DIR}/TileStream.cpp ${ORBITER_SOURCE_DIR}/TileStreamCfg.cpp ${ORBITER_SOURCE_DIR}/TileStreamImg.cpp ${ORBITER_SOURCE_DIR}/TileStreamNet.cpp ${ORBITER_SOURCE_DIR}/TileStreamSky.cpp ${ORBITER_SOURCE_DIR}/TileStreamCog.cpp ${ORBITER_SOURCE_DIR}/TileStreamMvt.cpp) # not upstream: the core's lock, holds, ownership, indexes, archives, disks and caps
target_link_libraries(TileStreamCore.Test Qt6::Network Qt6::Gui stb_dxt zlib)
target_compile_options(TileStreamCore.Test PRIVATE -fsanitize=address -fno-omit-frame-pointer)
target_link_options(TileStreamCore.Test PRIVATE -fsanitize=address)
# not upstream: the Launchpad Earth streaming page: base list, when Clear and Download are refused, how they end
add_unit_test(TileStreamLaunchpad.Test ${ORBITER_SOURCE_DIR}/StreamBases.cpp)
# not upstream: a base file's clear zones (BEGIN_CLEARZONES)
add_unit_test(BaseClear.Test ${ORBITER_SOURCE_DIR}/BaseClear.cpp)
# not upstream: streamed buildings' walls, roofs, atlas UVs and base structures, on .bld meshes the core makes
add_unit_test(BldgGeom.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/BldgGeom.cpp ${ORBITER_SOURCE_DIR}/TileStreamMvt.cpp)
target_include_directories(BldgGeom.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
target_link_libraries(BldgGeom.Test Qt6::Gui zlib)
target_compile_definitions(BldgGeom.Test PRIVATE BLDGTEX_DIR="${ORBITER_BINARY_TEXTURES_DIR}/Bldg")
add_dependencies(BldgGeom.Test BldgTextures)
target_compile_options(BldgGeom.Test PRIVATE -fsanitize=address -fno-omit-frame-pointer)
target_link_options(BldgGeom.Test PRIVATE -fsanitize=address)
add_unit_test(WayGeom.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/WayGeom.cpp ${ORBITER_SOURCE_DIR}/TileStreamMvt.cpp) # not upstream (design-ways)
target_include_directories(WayGeom.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
target_link_libraries(WayGeom.Test Qt6::Gui zlib)
target_compile_options(WayGeom.Test PRIVATE -fsanitize=address -fno-omit-frame-pointer)
target_link_options(WayGeom.Test PRIVATE -fsanitize=address)
# not upstream: terrain horizon maps (design-ground-graphics 2)
add_unit_test(HorizonMap.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/HorizonMap.cpp)
target_include_directories(HorizonMap.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
target_compile_options(HorizonMap.Test PRIVATE -O2 -fsanitize=address -fno-omit-frame-pointer)
target_link_options(HorizonMap.Test PRIVATE -fsanitize=address)
# not upstream: auto-exposure controller (design-ground-graphics 6)
add_unit_test(AutoExpo.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/AutoExpo.cpp)
target_include_directories(AutoExpo.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
target_compile_options(AutoExpo.Test PRIVATE -O2 -fsanitize=address -fno-omit-frame-pointer) # -O2: the night look's Monte Carlo cases
target_link_options(AutoExpo.Test PRIVATE -fsanitize=address)
# not upstream: volumetric cloud coverage map and noise (design-ground-graphics 4)
add_unit_test(VolCloudMap.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/VolCloudMap.cpp)
target_include_directories(VolCloudMap.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
target_compile_options(VolCloudMap.Test PRIVATE -O2 -fsanitize=address -fno-omit-frame-pointer)
target_link_options(VolCloudMap.Test PRIVATE -fsanitize=address)
# not upstream: tree placement and meshes (design-ground-graphics 5)
add_unit_test(TreeGeom.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/TreeGeom.cpp)
target_include_directories(TreeGeom.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
target_compile_options(TreeGeom.Test PRIVATE -O2 -fsanitize=address -fno-omit-frame-pointer)
target_link_options(TreeGeom.Test PRIVATE -fsanitize=address)
# not upstream: seamless far cloud layer (design-cloud-seams)
add_unit_test(CloudAtlas.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/CloudAtlas.cpp)
target_include_directories(CloudAtlas.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
target_compile_options(CloudAtlas.Test PRIVATE -fsanitize=address -fno-omit-frame-pointer)
target_link_options(CloudAtlas.Test PRIVATE -fsanitize=address)

# not upstream: visual batch maths (design-visual-batch 13)
add_unit_test(PostMath.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/PostMath.cpp)
target_include_directories(PostMath.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
add_unit_test(LookMath.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/LookMath.cpp)
target_include_directories(LookMath.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)

# not upstream: maths units of the new effects
add_unit_test(CsmMath.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/CsmMath.cpp)
target_include_directories(CsmMath.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
add_unit_test(SsrMath.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/SsrMath.cpp ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/PostMath.cpp)
target_include_directories(SsrMath.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
add_unit_test(SsgiMath.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/SsgiMath.cpp)
target_include_directories(SsgiMath.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
add_unit_test(FogMath.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/FogMath.cpp)
target_include_directories(FogMath.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
add_unit_test(PlumeMath.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/PlumeMath.cpp)
target_include_directories(PlumeMath.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
add_unit_test(HeatMath.Test ${CMAKE_SOURCE_DIR}/OVP/VulkanClient/HeatMath.cpp) # not upstream: re-entry hull heating maths
target_include_directories(HeatMath.Test PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)

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
