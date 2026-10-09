# not upstream: scenario tests of the collision addon (Design CA E4 7.3, 7.9): runner.py in a fixed sandbox per test and run, Linux only
option(COLLISION_SCENARIO_TESTS "Register the addon's headless scenario tests" ON)
option(COLLISION_VISUAL_TESTS "Register the addon's client scenario tests (Xvfb, lavapipe)" OFF)
set(COLL_SCN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Tests/Scenario)
if (COLL_TEST_DATA)
	set(COLL_UPSTREAM ${COLL_TEST_DATA}/UPSTREAM)
else ()
	set(COLL_UPSTREAM "")
endif ()

# test-only modules (E4 7.4): vessel modules in <build>/Tests/Modules, plugins in <build>/Tests/Modules/Plugin; never in the Launchpad
function(coll_test_module name)
	cmake_parse_arguments(PARSE_ARGV 1 M "PLUGIN" "" "SOURCES")
	add_library(${name} SHARED Tests/Modules/${name}/${name}.cpp ${M_SOURCES})
	target_include_directories(${name} PRIVATE ${COLL_SDK_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR}/Tests/Modules/${name})
	target_link_libraries(${name} ${COLL_ORBITER_LIB} ${COLL_SDK_LIB})
	set(out ${COLL_TEST_BIN}/Modules)
	if (M_PLUGIN)
		set(out ${COLL_TEST_BIN}/Modules/Plugin)
	endif ()
	set_target_properties(${name} PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${out} RUNTIME_OUTPUT_DIRECTORY ${out} FOLDER Tests/Collision)
	if (NOT COLL_STANDALONE)
		add_dependencies(${name} ${OrbiterTgt} Orbitersdk)
	endif ()
	set_property(GLOBAL APPEND PROPERTY COLL_TEST_TARGETS ${name})
endfunction()

coll_test_module(CollTestHarness PLUGIN)
coll_test_module(CollTestVessel)
coll_test_module(CollTestAnim)
if (COLL_STANDALONE)
	target_include_directories(CollTestVessel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}) # CollisionAPI.h of this source tree
else ()
	target_include_directories(CollTestVessel PRIVATE ${ORBITER_BINARY_SDK_DIR}/include) # the build tree's copy (E4 4.4)
	add_dependencies(CollTestVessel CollisionData)
endif ()
target_link_libraries(CollTestVessel ${CMAKE_DL_LIBS}) # collaFind

# coll_scenario_test(<name> SCN <stock path | gen:<setup>> FRAMES <n> [STEP h] [TIMEOUT s] [EVERY n] [CHECK x] [LUA x] RUNS "id|mode|addon|order|args|inherit|actions" .. [CFG ..] [ACFG ..] [ARGS ..] [MODULES ..] [LABELS ..] [VISUAL] [TIMING])
function(coll_scenario_test name)
	cmake_parse_arguments(PARSE_ARGV 1 S "VISUAL;TIMING" "SCN;STEP;FRAMES;TIMEOUT;CHECK;EVERY;LUA" "RUNS;CFG;ACFG;ARGS;LABELS;MODULES")
	if (NOT COLLISION_SCENARIO_TESTS OR (S_VISUAL AND NOT COLLISION_VISUAL_TESTS))
		return ()
	endif ()
	foreach (d STEP:0.02 FRAMES:60 TIMEOUT:120 EVERY:1)
		string(REPLACE ":" ";" d ${d})
		list(GET d 0 k)
		list(GET d 1 v)
		if (NOT S_${k})
			set(S_${k} ${v})
		endif ()
	endforeach ()
	set(cmd ${Python3_EXECUTABLE} ${COLL_SCN_DIR}/runner.py --name ${name} --exe ${COLL_EXE} --root ${COLL_ORBITER_ROOT} --data ${COLL_SCN_DIR}
		--work ${COLL_TEST_BIN}/run --modules ${COLL_TEST_BIN}/Modules --addon-so $<TARGET_FILE:Collision> --addon-src ${CMAKE_CURRENT_SOURCE_DIR}
		--upstream=${COLL_UPSTREAM} --scn ${S_SCN} --step ${S_STEP} --frames ${S_FRAMES} --every ${S_EVERY} --timeout ${S_TIMEOUT}
		--compiler ${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION} --buildtype=${CMAKE_BUILD_TYPE})
	if (S_CHECK)
		list(APPEND cmd --check ${S_CHECK})
	endif ()
	if (S_LUA)
		list(APPEND cmd --lua ${S_LUA})
	endif ()
	if (S_TIMING) # wall-clock checks: skipped (77) while a build runs, every other test runs
		list(APPEND cmd --timing)
	endif ()
	foreach (k RUNS:run CFG:cfg ACFG:acfg ARGS:arg MODULES:module)
		string(REPLACE ":" ";" k ${k})
		list(GET k 0 var)
		list(GET k 1 opt)
		foreach (v ${S_${var}})
			list(APPEND cmd "--${opt}=${v}")
		endforeach ()
	endforeach ()
	set(labels scenario coll ${S_LABELS})
	set(props "")
	if (S_VISUAL)
		list(APPEND labels visual)
		set(props FIXTURES_REQUIRED xvfb)
	endif ()
	math(EXPR t "${S_TIMEOUT} + 30")
	coll_add_test(${name} TIMEOUT ${t} SKIP 77 SERIAL LABELS ${labels} PROPS ${props} COMMAND ${cmd})
endfunction()

if (NOT COLLISION_SCENARIO_TESTS)
	return ()
endif ()

# T0.1 runner proofs (E4 7.9.4, 7.10)
coll_add_test(Scn.RunnerGuard TIMEOUT 60 SKIP 77 SERIAL LABELS scenario coll COMMAND ${Python3_EXECUTABLE} ${COLL_SCN_DIR}/runner.py
	--selftest-guards --work ${COLL_TEST_BIN}/run)
coll_scenario_test(Scn.SanityCheck SCN "Delta-glider/Smack!" FRAMES 60 CHECK sanity RUNS "main|headless|off||||")
coll_scenario_test(Scn.AddonLoad SCN "Delta-glider/Smack!" FRAMES 60 CHECK addonload RUNS "main|headless|on||||")
coll_scenario_test(Scn.ExitCode SCN gen:pair FRAMES 20 LUA exitcode CHECK exitcode RUNS "main|headless|off||@exit=3||")

# T0.2 run-to-run determinism (design-C-T T0.2)
coll_scenario_test(Scn.Twice SCN gen:pair:g0=1000 FRAMES 600 CHECK twice
	RUNS "off1|headless|off||||" "off2|headless|off||||" "on1|headless|on||||" "on2|headless|on||||")

# T0.3 checker self-tests: synthetic dumps and logs with planted errors (E4 T0.3)
coll_add_test(Scn.Selftest TIMEOUT 60 SERIAL LABELS scenario coll COMMAND ${Python3_EXECUTABLE} ${COLL_SCN_DIR}/selftest.py --work ${COLL_TEST_BIN}/run)

# T0.4 goldens (E4 7.6, design-C-T 7.3): G1-G4 addon off, G5 addon on in quiet scenes; golden/<name>.dump.gz from an off run with COLL_GOLDEN_WRITE=1, another compiler, build type or CPU skips (77)
coll_scenario_test(Coll.Off.Golden.Pair SCN gen:pair:g0=0.5,vA=0.5,vB=-0.5 FRAMES 600 EVERY 5 CHECK golden LABELS golden RUNS "c0|headless|off||||")
coll_scenario_test(Coll.Off.Golden.Smack SCN "Delta-glider/Smack!" FRAMES 1500 EVERY 10 CHECK golden LABELS golden RUNS "c0|headless|off||||")
coll_scenario_test(Coll.Off.Golden.Ascent SCN gen:ascent STEP 0.05 FRAMES 3000 EVERY 10 TIMEOUT 600 CHECK golden LABELS golden long
	RUNS "c0|headless|off||||")
coll_scenario_test(Coll.Off.Golden.Surface SCN gen:surface FRAMES 600 EVERY 5 CHECK golden LABELS golden RUNS "c0|headless|off||||")
coll_scenario_test(G5.Smack SCN "Delta-glider/Smack!" FRAMES 1500 EVERY 10 CHECK golden LABELS golden RUNS "c1|headless|on||||")
coll_scenario_test(G5.Surface SCN gen:surface FRAMES 600 EVERY 5 CHECK golden LABELS golden RUNS "c1|headless|on||||")
coll_scenario_test(G5.Far SCN gen:pair:g0=1000 FRAMES 600 EVERY 5 CHECK golden LABELS golden RUNS "c0|headless|off||||" "c1|headless|on||||")

# T0.7 test modules and actions (E4 7.4, 7.10)
coll_scenario_test(Scn.Place0 SCN gen:pair:g0=1000 FRAMES 10 CHECK place0 RUNS "main|headless|off||||TESTPLACEREL 5 PB-B PB-A 0 0 6 0 0 0 0 180 0")
coll_scenario_test(Scn.Actions SCN gen:pair:g0=1000,A=CollTestVessel,B=CollTestVessel FRAMES 100 CHECK actions RUNS "main|headless|off||||@actions.txt")
coll_scenario_test(Scn.PlaceBase SCN gen:surface FRAMES 10 CHECK placebase
	RUNS "main|headless|off||||TESTCREATE 1 PL CollTestVessel PB 0 30 0 0 0 0,TESTPLACEBASE 3 PL Brighton_Beach 100 3 -173.21 0 0 0 90")
coll_scenario_test(Scn.LuaCall SCN gen:surface:rcover=1 FRAMES 500 CHECK luacall RUNS "ref|headless|off||||" "call|headless|off||||LUACALL 5 GL Retro 0")
coll_scenario_test(Coll.MXCSR SCN gen:pair:g0=1000,director=1 FRAMES 60 CHECK mxcsr RUNS "off|headless|off||||" "on|headless|on||||")
coll_scenario_test(Scn.Jitter TIMING SCN gen:pair:g0=1000,director=1 FRAMES 60 CHECK jitter LABELS paced RUNS "main|paced|off||||TESTPACE 16.7,TESTSLEEP 30 100")
coll_scenario_test(G5.Warp SCN gen:pair:g0=1000,director=1 FRAMES 500 EVERY 5 CHECK g5pair LABELS golden
	RUNS "off|headless|off||||TESTWARP 100 10,TESTWARP 200 100,TESTWARP 300 1000,TESTWARP 400 1" "on|headless|on||||TESTWARP 100 10,TESTWARP 200 100,TESTWARP 300 1000,TESTWARP 400 1")
# G5.Land: the DG dropped 2 m on the flat Moon 2 km from Brighton Beach touches down in frame 80 (probe run); TESTMASS from 2 frames before to 2 after
set(land "TESTPLACEBASE 1 GL Brighton_Beach 0 4.5576 2000 0 0 0 0,TESTMASS 78 82 GL 12000")
coll_scenario_test(G5.Land SCN gen:surface:director=1 FRAMES 300 CHECK g5pair LABELS golden RUNS "off|headless|off||||${land}" "on|headless|on||||${land}")
coll_scenario_test(Scn.Anim SCN gen:pair:g0=1000,director=1,A=CollTestAnim,B=CollTestAnimVC FRAMES 100 CHECK anim
	RUNS "main|headless|off||||@PB-A:TESTANIMCYCLE 20,TESTCREATE 30 TA CollTestAnim PB-A 0 50 0 0 0 0")

# T0.8 recorder by simulated key, playback, synthetic records, the clear of E4 7.5
coll_scenario_test(Scn.RecPlay SCN gen:pair:g0=1000,director=1 FRAMES 300 CHECK recplay
	RUNS "rec|headless|off||||TESTRECORD 1 300" "play|headless|off||@scn=Playback/{stem}|rec|")
coll_scenario_test(Scn.RecTwice SCN gen:pair:g0=1000,director=1 FRAMES 100 CHECK rectwice
	RUNS "rec|headless|off||||TESTRECORD 1 100" "rec|headless|off||||TESTRECORD 1 100")
coll_scenario_test(Scn.SynthPlay SCN gen:synthplay FRAMES 300 CHECK synthplay RUNS "main|headless|off||||")

# T0.5, T0.6 client runs: VulkanClient on Xvfb :58 with lavapipe and the validation layer; label visual, off unless COLLISION_VISUAL_TESTS
if (COLLISION_VISUAL_TESTS)
	coll_add_test(Scn.Xvfb.Start TIMEOUT 60 SKIP 77 LABELS scenario coll visual PROPS FIXTURES_SETUP xvfb
		COMMAND ${Python3_EXECUTABLE} ${COLL_SCN_DIR}/runner.py --xvfb-start --work ${COLL_TEST_BIN}/run)
	coll_add_test(Scn.Xvfb.Stop TIMEOUT 60 LABELS scenario coll visual PROPS FIXTURES_CLEANUP xvfb
		COMMAND ${Python3_EXECUTABLE} ${COLL_SCN_DIR}/runner.py --xvfb-stop --work ${COLL_TEST_BIN}/run)
endif ()
coll_scenario_test(Coll.HvC.Stock VISUAL SCN gen:hvc FRAMES 200 TIMEOUT 900 CHECK hvc CFG "EnableParticleStreams=FALSE"
	RUNS "h|headless|on||||TESTRNGPROBE 50" "cf|client|on|client-first|||TESTRNGPROBE 50" "af|client|on|addon-first|||TESTRNGPROBE 50")
coll_scenario_test(Scn.Visual.Smoke VISUAL SCN gen:pair:g0=20 FRAMES 70 TIMEOUT 600 CHECK visualsmoke
	RUNS "a|client|off||||LUASHOT 60 smoke" "b|client|off||||LUASHOT 60 smoke")

# T0.9 guards and close paths (design-C-T 2.5, 3.5; E4 7.10); the build-running skip is proven by Scn.RunnerGuard
coll_scenario_test(Scn.Timeout TIMING SCN gen:pair:g0=1000 FRAMES 60 TIMEOUT 20 CHECK timeout RUNS "main|headless|off||@nolimit @expect=TIMEOUT||")
coll_scenario_test(Scn.Pause TIMING SCN gen:pair:g0=1000,director=1 FRAMES 190 CHECK pause
	RUNS "ref|headless|off||@nolimit --maxsimtime=4||" "pause|headless|off||@nolimit --maxsimtime=4 @stdin=pause.txt||TESTPAUSE 50")
set(close "'@endon=**** Closing simulation session' @cfg:ShutdownMode=0")
coll_scenario_test(Scn.Teardown SCN "Delta-glider/Smack!" FRAMES 60 CHECK teardown
	RUNS "off|headless|off||${close}||" "on|headless|on||${close}||" "onfast|headless|on||||")
coll_scenario_test(Scn.NormalClose SCN gen:pair:g0=1000,director=1,block=destroyed FRAMES 60 CHECK normalclose
	RUNS "main|headless|on||${close}||TESTRECORD 1 1000")

# integration smoke test: the scenario file Smoke-PB-headon.scn, PB-A and PB-B noses 6 m apart closing at 2 m/s, contact near t = 3 s
coll_scenario_test(Coll.Smoke.HeadOn SCN Smoke-PB-headon.scn FRAMES 400 CHECK smoke RUNS "main|headless|on||||")

# E1 bodies other than a free pair (review CA-F 1-6): base buildings, a docked stack, an attachment tree, a LANDED partner
coll_scenario_test(Coll.Base.KSC SCN gen:ksc FRAMES 600 CHECK ksc RUNS "main|headless|on||||")
coll_scenario_test(Coll.Base.Hangar SCN gen:hangar FRAMES 300 CHECK hangar LABELS slow RUNS "main|headless|on||||") # a building impact and its damage line
coll_scenario_test(Coll.Stack.Hit SCN gen:stack FRAMES 450 CHECK stackhit RUNS "off|headless|off||||" "on|headless|on||||")
coll_scenario_test(Coll.Stack.Attached SCN gen:attached FRAMES 300 CHECK stackhit RUNS "off|headless|off||||" "on|headless|on||||")
coll_scenario_test(Coll.Landed.Wake SCN gen:wake FRAMES 150 CHECK landedwake RUNS "main|headless|on||||")

# dmg3: DGs head-on (pair noses 6 + g0 m apart is a PB value, DG CGs 106 m and 46 m apart), contact near t 1.3 s and 1.8 s
set(dg70 "gen:pair:g0=100,vA=35,vB=-35,A=DeltaGlider,B=DeltaGlider,director=1")
set(dg15 "gen:pair:g0=40,vA=7.5,vB=-7.5,A=DeltaGlider,B=DeltaGlider,director=1")
set(dg30 "gen:pair:g0=60,vA=15,vB=-15,A=DeltaGlider,B=DeltaGlider,director=1")
coll_scenario_test(Coll.Dmg3.Crash70 SCN ${dg70} FRAMES 300 TIMEOUT 300 CHECK dmg3crash LABELS slow RUNS "main|headless|on||||") # crush dents, nose break, debris, section tear
coll_scenario_test(Coll.Dmg3.Crash30 SCN ${dg30} FRAMES 300 TIMEOUT 300 CHECK dmg3crash LABELS slow RUNS "main|headless|on||||") # no section tear
coll_scenario_test(Coll.Dmg3.Crash15 SCN ${dg15} FRAMES 300 TIMEOUT 300 CHECK dmg3crash LABELS slow RUNS "main|headless|on||||") # dents only
coll_scenario_test(Coll.Dmg3.SaveLoad SCN ${dg70} FRAMES 200 TIMEOUT 300 CHECK dmg3save LABELS slow
	RUNS "crash|headless|on||||LUASAVE 150 dmg3" "load|headless|on||@scn=Tests/Coll/Inherit/dmg3|crash|")
coll_scenario_test(Coll.Dmg3.RecPlay SCN ${dg70} FRAMES 300 TIMEOUT 300 CHECK dmg3recplay LABELS slow
	RUNS "rec|headless|on||||TESTRECORD 1 300" "play|headless|on||@scn=Playback/{stem}|rec|")
coll_scenario_test(Coll.Dmg3.Fx SCN ${dg70} FRAMES 150 TIMEOUT 300 CHECK dmg3fx LABELS slow RUNS "main|headless|on||||")
coll_scenario_test(Coll.Dmg3.Crash70.Client VISUAL SCN ${dg70} FRAMES 300 TIMEOUT 900 CHECK dmg3crash RUNS "main|client|on||||") # the same crash in VulkanClient
coll_scenario_test(Coll.Dmg3.Tear.Client VISUAL SCN "${dg70},cam=side" FRAMES 300 TIMEOUT 900 CHECK dmg3crash RUNS "main|client|on||||") # dmg3 tear: side view of the torn section
