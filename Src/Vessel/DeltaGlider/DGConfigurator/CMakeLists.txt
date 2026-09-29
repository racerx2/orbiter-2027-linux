# Copyright (c) Martin Schweiger
# Licensed under the MIT License

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${ORBITER_BINARY_STARTUP_DIR})

add_library(DGConfigurator SHARED
	DGConfigurator.cpp
)
orbiter_rc_resources(DGConfigurator DGConfigurator.rc)

target_include_directories(DGConfigurator
	PUBLIC ${ORBITER_SOURCE_SDK_INCLUDE_DIR}
)

target_link_libraries(DGConfigurator
	${ORBITER_LIB}
	${ORBITER_SDK_LIB}
)

add_dependencies(DGConfigurator
	${OrbiterTgt}
	Orbitersdk
)

set_target_properties(DGConfigurator
	PROPERTIES
	FOLDER Vessels/DeltaGlider
)

# Installation
install(TARGETS DGConfigurator
	LIBRARY
	DESTINATION ${ORBITER_INSTALL_MODULE_DIR}/Startup
)
