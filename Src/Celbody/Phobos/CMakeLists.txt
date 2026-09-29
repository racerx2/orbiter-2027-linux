# Copyright (c) Martin Schweiger
# Licensed under the MIT License

set(CELBODY "Phobos")

# Phobos.dll (prebuilt 32-bit, no source) left out: the body follows the Kepler elements in its config

srcdir_to_tgtlist("${CMAKE_CURRENT_SOURCE_DIR}/Config" "${ORBITER_BINARY_CONFIG_DIR}" srclist tgtlist)
add_custom_command(
	OUTPUT ${tgtlist}
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Config/ ${ORBITER_BINARY_CONFIG_DIR}
	DEPENDS ${srclist}
)
add_custom_target(${CELBODY}
	DEPENDS ${tgtlist}
)
add_dependencies(Sol
	${CELBODY}
)

set_target_properties(${CELBODY}
	PROPERTIES
	FOLDER Celbody
)

# Installation
install(DIRECTORY Config
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}
)
