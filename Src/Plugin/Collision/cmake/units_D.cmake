# not upstream: Phase D units (E3: persistence, damage, visuals, exported API)
list(APPEND COLL_UNITS CollStore)
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/CollSdk.cpp) # the counted SDK calls of E2; E2's list adds the unit first when present
	if (NOT CollSdk IN_LIST COLL_UNITS)
		list(APPEND COLL_UNITS CollSdk)
	endif ()
	list(APPEND COLL_UNITS CollVisualA CollDamageA CollApiA)
endif ()
