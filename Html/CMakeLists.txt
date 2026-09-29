add_subdirectory(Scenarios)

if(ORBITER_MAKE_DOC OR ORBITER_MAKE_HELP) # not upstream: ORBITER_MAKE_HELP
	add_subdirectory(Main)
endif()
