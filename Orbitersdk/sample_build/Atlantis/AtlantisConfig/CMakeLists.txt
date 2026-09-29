# Licensed under the MIT License


# =============================================================================
# AtlantisConfig
# =============================================================================

set(PRJ AtlantisConfig)

set(Sources
	../Common.cpp
	AtlantisConfig.cpp
)
set(Headers
	AC_resource.h
)
set(Other
)
	
source_group(APIHeaders FILES ${APIHeaders})
source_group(Headers FILES ${Headers})
source_group(Sources FILES ${Sources})
source_group(Other FILES ${Other})

add_library(${PRJ} SHARED
	${Sources}
	${Headers}
    ${APIHeaders}
	${Other}
)
orbiter_rc_resources(${PRJ} AConfigurator.rc)

target_link_libraries(${PRJ} debug ${DebugLib})
target_link_libraries(${PRJ} optimized ${ReleaseLib})

#target_include_directories(${PRJ} PUBLIC ${INCLUDE_DIR} ${IncludeDirs})

set_target_properties(${PRJ}
	PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY_DEBUG ${STARTUP_DIR}
	LIBRARY_OUTPUT_DIRECTORY_RELEASE ${STARTUP_DIR}
	FOLDER Atlantis
)
