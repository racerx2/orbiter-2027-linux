# Licensed under the MIT License


# =============================================================================
# Atlantis
# =============================================================================

set(PRJ Atlantis)
set(MESH_DIR ${ROOT_DIR}/Meshes/Atlantis)

set(Sources
	AscentAP.cpp
	Atlantis.cpp
	PlBayOp.cpp
	../Common.cpp
	# Common/Dialog/Graph.cpp and TabDlg.cpp left out: not installed, Atlantis no longer uses them
)

set(Headers
	AscentAP.h
	Atlantis.h
	PlBayOp.h
	# resource.h, Common/Dialog/Graph.h and TabDlg.h left out: not installed, Atlantis no longer uses them
)
set(Other
	# Atlantis.rc and its icons left out: not installed, Atlantis no longer has resources
	${MESH_DIR}/Atlantis.msh
	${MESH_DIR}/AtlantisVC.msh
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


# Execute Mesh Compiler ------------------------------

# scan mesh files for labels and output to header file
add_custom_command(OUTPUT meshres.h
	COMMAND ${ORBITER_TOOL_MESHC} /I ${MESH_DIR}/Atlantis.msh /P - /O meshres.h
	DEPENDS ${MESH_DIR}/Atlantis.msh
)
add_custom_command(OUTPUT meshres_vc.h
	COMMAND ${ORBITER_TOOL_MESHC} /I ${MESH_DIR}/AtlantisVC.msh /P _VC /O meshres_vc.h
	DEPENDS ${MESH_DIR}/AtlantisVC.msh
)
add_custom_target(Atlantis_Prerequisites
	DEPENDS meshres.h meshres_vc.h
)
# -----------------------------------------------------


add_dependencies(${PRJ}
	Atlantis_Prerequisites
	Atlantis_SRB
	Atlantis_Tank
)

target_link_libraries(${PRJ} debug ${DebugLib} Atlantis_Tank)
target_link_libraries(${PRJ} optimized ${ReleaseLib} Atlantis_Tank)

set_target_properties(${PRJ}
	PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY_DEBUG ${MODULE_DIR}
	LIBRARY_OUTPUT_DIRECTORY_RELEASE ${MODULE_DIR}
	FOLDER Atlantis
)
