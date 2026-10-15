# Copyright (c) Martin Schweiger
# Licensed under the MIT License

srcdir_to_tgtlist("${CMAKE_CURRENT_SOURCE_DIR}/Config" "${ORBITER_BINARY_CONFIG_DIR}" srclist tgtlist)
add_custom_command(
	OUTPUT ${tgtlist}
	COMMAND ${ORBITER_COPY_NEWER} ${CMAKE_CURRENT_SOURCE_DIR}/Config/ ${ORBITER_BINARY_CONFIG_DIR} # not upstream: copynewer.cmake keeps files changed in the build tree
	DEPENDS ${srclist}
)
add_custom_target(Sol
	DEPENDS ${tgtlist}
)
add_dependencies(${OrbiterTgt}
	Sol
)

set_target_properties(Sol
	PROPERTIES
	FOLDER Celbody
)

# Installation
install(DIRECTORY Config
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}
)