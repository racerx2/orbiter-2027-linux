# not upstream (head-track): the TrackIR 4 and 5 without NaturalPoint's software, ported from linuxtrack (MIT, LICENSE-linuxtrack)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
find_package(Threads REQUIRED)

add_library(HeadTrack STATIC
	HeadTracker.cpp
	HtAxis.cpp
	HtBlobs.cpp
	HtFirmware.cpp
	HtMath.cpp
	HtPose.cpp
	HtSettings.cpp
	HtTirDevice.cpp
	HtTirPacket.cpp
	HtTirUsb.cpp
)
target_include_directories(HeadTrack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(HeadTrack PUBLIC PkgConfig::LIBUSB Qt6::Core zlib Threads::Threads)
set_target_properties(HeadTrack PROPERTIES POSITION_INDEPENDENT_CODE ON)

install(FILES LICENSE-linuxtrack DESTINATION ${ORBITER_INSTALL_DOC_DIR}/Licenses RENAME linuxtrack.txt) # the licence goes with the code ported from it
