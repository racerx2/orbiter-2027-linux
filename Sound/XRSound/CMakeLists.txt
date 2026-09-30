## irrKlang
# irrKlang left out: a closed-source Windows library; XRSound plays through its own mixer on PipeWire (src/Audio)
# not upstream: the mixer's decoders, public-domain single-file libraries (OGG stb_vorbis, MP3 dr_mp3, FLAC dr_flac), pinned
include(FetchContent)
FetchContent_Declare(stb_vorbis
	URL https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_vorbis.c
	URL_HASH SHA256=4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636
	DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(dr_mp3
	URL https://raw.githubusercontent.com/mackron/dr_libs/5690d4671d7ad07ae6021756d7222eb159745f06/dr_mp3.h
	URL_HASH SHA256=d80e6800ddde29d50988ce894affe601858802d1bb8f047f98b84b33ae66c11a
	DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(dr_flac
	URL https://raw.githubusercontent.com/mackron/dr_libs/69d777c482775858e8ea8a7b047c9bcd451febc8/dr_flac.h
	URL_HASH SHA256=a35468f278f17cf9538ce189debf76a5854a77e139980f7b5185d67566a0c837
	DOWNLOAD_NO_EXTRACT TRUE
)
# not upstream: tracker modules (MOD, S3M, XM, IT, which irrKlang played) through libxmp-lite (MIT), built static
FetchContent_Declare(libxmp_lite
	URL https://github.com/libxmp/libxmp/releases/download/libxmp-4.6.3/libxmp-lite-4.6.3.tar.gz
	URL_HASH SHA256=fa6465d8b911363ae602c7baaa625ca1f5223142d10bb4682029c4d2f630cb62
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW) # its options take the values below
set(BUILD_STATIC ON)
set(BUILD_SHARED OFF)
set(LIBXMP_PIC ON)
FetchContent_MakeAvailable(stb_vorbis dr_mp3 dr_flac libxmp_lite)
set_property(DIRECTORY ${libxmp_lite_SOURCE_DIR} PROPERTY EXCLUDE_FROM_ALL TRUE) # not upstream: its install rules stay out of Orbiter's install; linked, so still built
set(AUDIO_DECODER_INCLUDE_DIRS ${stb_vorbis_SOURCE_DIR} ${dr_mp3_SOURCE_DIR} ${dr_flac_SOURCE_DIR})
set(AUDIO_DECODER_LIBS libxmp-lite::xmp_lite_static)

## XRSound
set(ASSET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(SOLUTION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
# IRRKLANG_DLL_DIR left out: there are no irrKlang DLLs to deploy
# Deploy assets
file(GLOB config_files ${ASSET_DIR}/XRSound/*.cfg)

add_custom_target(XRSound_assets ALL
	COMMAND ${CMAKE_COMMAND} -E make_directory ${ORBITER_BINARY_ROOT_DIR}/Doc
	COMMAND ${CMAKE_COMMAND} -E make_directory ${ORBITER_BINARY_ROOT_DIR}/XRSound
	COMMAND ${CMAKE_COMMAND} -E make_directory ${ORBITER_BINARY_SDK_DIR}/XRSound/
	COMMAND ${CMAKE_COMMAND} -E copy ${config_files} ${ORBITER_BINARY_ROOT_DIR}/XRSound/
	COMMAND ${CMAKE_COMMAND} -E copy ${ASSET_DIR}/XRSound/ReadMe.txt ${ORBITER_BINARY_ROOT_DIR}/XRSound/
	COMMAND ${CMAKE_COMMAND} -E copy "${ASSET_DIR}/Doc/XRSound User Manual.pdf" ${ORBITER_BINARY_ROOT_DIR}/Doc/
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SOLUTION_DIR}/XRSound.h ${ORBITER_BINARY_SDK_DIR}/XRSound/
	# ikpMP3.dll, ikpFlac.dll and irrKlang.dll left out: the mixer and its decoders are built into XRSound.so
	# copy_directory_if_different: the 200 MB of default sounds are not copied again on every build
	COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${ASSET_DIR}/XRSound/Default ${ORBITER_BINARY_ROOT_DIR}/XRSound/Default
)

install(DIRECTORY ${ORBITER_BINARY_ROOT_DIR}/XRSound
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}
)
install(DIRECTORY ${ORBITER_BINARY_SDK_DIR}/XRSound
	DESTINATION ${ORBITER_INSTALL_SDK_DIR}
)
install(DIRECTORY ${ORBITER_BINARY_ROOT_DIR}/XRSound/Default
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}/XRSound
)
install(FILES "${ORBITER_BINARY_ROOT_DIR}/Doc/XRSound User Manual.pdf"
	DESTINATION ${ORBITER_INSTALL_DOC_DIR}/
)
# irrKlang DLL install left out: no irrKlang

find_package(Threads REQUIRED) # not upstream: the mixer's decoder threads
add_subdirectory(src)

# not upstream: mixer tests, all silent: an offline engine, no daemon (PIPEWIRE_REMOTE points nowhere), a private daemon without devices
if(ORBITER_MAKE_TESTS)
	add_executable(XRSound.AudioEngine.Test
		test/AudioEngineTest.cpp
		src/Audio/AudioEngine.cpp
		src/Audio/AudioDecode.cpp
	)
	set_target_properties(XRSound.AudioEngine.Test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ORBITER_BINARY_ROOT_DIR}/Tests)
	target_include_directories(XRSound.AudioEngine.Test PRIVATE ${SOLUTION_DIR} ${AUDIO_DECODER_INCLUDE_DIRS} $<TARGET_PROPERTY:PkgConfig::PIPEWIRE,INTERFACE_INCLUDE_DIRECTORIES>)
	target_compile_definitions(XRSound.AudioEngine.Test PRIVATE XRSOUND_TEST_SOUNDS="${ASSET_DIR}/XRSound/Default" XRSOUND_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/test/data")
	target_link_libraries(XRSound.AudioEngine.Test ${CMAKE_DL_LIBS} Threads::Threads ${AUDIO_DECODER_LIBS})
	add_test(NAME XRSound.AudioEngine.Test COMMAND $<TARGET_FILE:XRSound.AudioEngine.Test>)
	set_tests_properties(XRSound.AudioEngine.Test PROPERTIES TIMEOUT 120 ENVIRONMENT "PIPEWIRE_REMOTE=xrsound-test-none")
	add_test(NAME XRSound.AudioEngine.Daemon.Test COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/DaemonTest.sh $<TARGET_FILE:XRSound.AudioEngine.Test> ${CMAKE_CURRENT_SOURCE_DIR}/test)
	set_tests_properties(XRSound.AudioEngine.Daemon.Test PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
	add_test(NAME XRSound.NoPipeWireLink.Test COMMAND ${CMAKE_COMMAND} -DLIBRARY=$<TARGET_FILE:XRSound_dll> -DREADELF=${CMAKE_READELF} -DNM=${CMAKE_NM} -P ${CMAKE_CURRENT_SOURCE_DIR}/test/CheckNoPipeWireLink.cmake)
endif()
