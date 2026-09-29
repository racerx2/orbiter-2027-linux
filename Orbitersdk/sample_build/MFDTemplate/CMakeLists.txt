# Licensed under the MIT License


# =============================================================================
# MFDTemplate
# =============================================================================

set(PRJ MFDTemplate)

set(Sources
	MFDTemplate.cpp
)
set(Headers
	MFDTemplate.h
	resource.h
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
orbiter_rc_resources(${PRJ} MFDTemplate.rc)

target_link_libraries(${PRJ} debug ${DebugLib})
target_link_libraries(${PRJ} optimized ${ReleaseLib})

set_target_properties(${PRJ}
	PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY_DEBUG ${PLUGIN_DIR}
	LIBRARY_OUTPUT_DIRECTORY_RELEASE ${PLUGIN_DIR}
	FOLDER Template
)
