# Copyright (c) Martin Schweiger
# Licensed under the MIT License

set(CELBODY "Umbriel")

# not upstream: built from new source fitted to JPL Horizons (upstream ships only a 32-bit DLL)
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
add_custom_target(${CELBODY}Config
	DEPENDS ${tgtlist}
)
add_dependencies(${CELBODY}
	${CELBODY}Config
)
add_dependencies(Sol
	${CELBODY}Config
)

set_target_properties(${CELBODY} ${CELBODY}Config
	PROPERTIES
	FOLDER Celbody
)

# Installation
install(TARGETS
	${CELBODY}
	LIBRARY
	DESTINATION ${ORBITER_INSTALL_CELBODY_DIR}
)
install(DIRECTORY Config
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}
)
