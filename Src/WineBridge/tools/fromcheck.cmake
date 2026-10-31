# WB_HELPER_FROM: the helper build's generated calls are for this Orbiter binary (its sha256, tools/linuxsyms.sh): cmake -DSHA= -DORBITER= -P
file(READ "${SHA}" want)
string(STRIP "${want}" want)
file(SHA256 "${ORBITER}" have)
if(NOT want STREQUAL have)
	message(FATAL_ERROR "WB_HELPER_FROM was built against another Orbiter than ${ORBITER}: build it again")
endif()
