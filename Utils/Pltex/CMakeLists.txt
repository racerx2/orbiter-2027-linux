# Copyright (c) Martin Schweiger
# Licensed under the MIT License

add_executable(pltex
	Pltex.cpp
)

target_include_directories(pltex
	PRIVATE ${ORBITER_SOURCE_SDK_INCLUDE_DIR} # not upstream: OrbiterPlatform.h for the windows.h integer types
)

set_target_properties(pltex
	PROPERTIES
	FOLDER Tools
)

# Installation
install(TARGETS
	pltex
	RUNTIME
	DESTINATION ${ORBITER_INSTALL_UTILS_DIR}
)
