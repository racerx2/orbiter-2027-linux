# Copyright (c) Martin Schweiger
# Licensed under the MIT License

set(CELBODY "Charon")

# not upstream: new module, a series fitted to JPL's plu060 (upstream has no Pluto system)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Modules/Celbody)

add_library(${CELBODY} SHARED
	${CELBODY}.cpp
)

add_dependencies(${CELBODY}
	${OrbiterTgt}
	Orbitersdk
)

target_include_directories(${CELBODY}
	PUBLIC ${CMAKE_SOURCE_DIR}/Orbitersdk/include
)

target_link_libraries(${CELBODY}
	${ORBITER_LIB}
	${ORBITER_SDK_LIB}
)

srcdir_to_tgtlist("${CMAKE_CURRENT_SOURCE_DIR}/Config" "${ORBITER_BINARY_CONFIG_DIR}" srclist tgtlist)
add_custom_command(
	OUTPUT ${tgtlist}
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Config/ ${ORBITER_BINARY_CONFIG_DIR}
	DEPENDS ${srclist}
)
add_custom_target(${CELBODY}Config # not upstream: the library takes the name ${CELBODY}
	DEPENDS ${tgtlist}
)
add_dependencies(${CELBODY} # not upstream: the module needs its cfg
	${CELBODY}Config
)
add_dependencies(Sol
	${CELBODY}Config # not upstream: was ${CELBODY}
)

set_target_properties(${CELBODY} ${CELBODY}Config # not upstream: both targets
	PROPERTIES
	FOLDER Celbody
)

# Installation
install(TARGETS # not upstream: the new module
	${CELBODY}
	LIBRARY
	DESTINATION ${ORBITER_INSTALL_CELBODY_DIR}
)
install(DIRECTORY Config
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}
)
