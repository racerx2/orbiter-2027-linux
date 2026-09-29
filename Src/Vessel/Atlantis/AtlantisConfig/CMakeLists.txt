# Copyright (c) Martin Schweiger
# Licensed under the MIT License

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${ORBITER_BINARY_STARTUP_DIR})

add_library(AtlantisConfig SHARED
	AtlantisConfig.cpp
)
orbiter_rc_resources(AtlantisConfig AConfigurator.rc)

target_include_directories(AtlantisConfig
	PUBLIC ${ORBITER_SOURCE_SDK_INCLUDE_DIR}
)

target_link_libraries(AtlantisConfig
	${ORBITER_LIB}
	${ORBITER_SDK_LIB}
)

add_dependencies(AtlantisConfig
	${OrbiterTgt}
	Orbitersdk
)

set_target_properties(AtlantisConfig
	PROPERTIES
	FOLDER Vessels/Atlantis
)

# Installation
install(TARGETS AtlantisConfig
	LIBRARY
	DESTINATION ${ORBITER_INSTALL_MODULE_DIR}/Startup
)
