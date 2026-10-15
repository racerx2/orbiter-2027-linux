# Copyright (c) Martin Schweiger
# Licensed under the MIT License

add_subdirectory(samples)
add_subdirectory(sample_build)

file(GLOB_RECURSE includes
	RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
	LIST_DIRECTORIES false
	include/*
)
foreach(fname ${includes})
	list(APPEND input
		${CMAKE_CURRENT_SOURCE_DIR}/${fname}
	)
	list(APPEND output
		${ORBITER_BINARY_SDK_DIR}/${fname}
	)
	list(APPEND commands
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/${fname} ${ORBITER_BINARY_SDK_DIR}/${fname}
	)
endforeach()

add_custom_command(
	OUTPUT ${output}
	DEPENDS ${input}
	${commands}
)
add_custom_target(copy_sdk_include
	DEPENDS ${output}
)
add_dependencies(Orbitersdk
	copy_sdk_include
)

# not upstream: CMake package for add-ons (find_package(Orbiter CONFIG)) and the .rc compiler it includes, in the build tree as installed
configure_file(OrbiterConfig.cmake.in ${ORBITER_BINARY_SDK_DIR}/lib/cmake/Orbiter/OrbiterConfig.cmake COPYONLY) # not upstream
configure_file(${ORBITER_SOURCE_ROOT_DIR}/cmake/rcresources.cmake ${ORBITER_BINARY_ROOT_DIR}/cmake/rcresources.cmake COPYONLY) # not upstream
configure_file(${ORBITER_SOURCE_ROOT_DIR}/cmake/rc2cpp.py ${ORBITER_BINARY_ROOT_DIR}/cmake/rc2cpp.py COPYONLY) # not upstream
include(CMakePackageConfigHelpers) # not upstream
write_basic_package_version_file(${ORBITER_BINARY_SDK_DIR}/lib/cmake/Orbiter/OrbiterConfigVersion.cmake COMPATIBILITY AnyNewerVersion) # not upstream
install(FILES # not upstream
	${ORBITER_BINARY_SDK_DIR}/lib/cmake/Orbiter/OrbiterConfig.cmake
	${ORBITER_BINARY_SDK_DIR}/lib/cmake/Orbiter/OrbiterConfigVersion.cmake
	DESTINATION ${ORBITER_INSTALL_SDK_DIR}/lib/cmake/Orbiter
)
install(FILES ${ORBITER_SOURCE_ROOT_DIR}/cmake/rcresources.cmake DESTINATION ${ORBITER_INSTALL_ROOT_DIR}/cmake) # not upstream
install(PROGRAMS ${ORBITER_SOURCE_ROOT_DIR}/cmake/rc2cpp.py DESTINATION ${ORBITER_INSTALL_ROOT_DIR}/cmake) # not upstream

if(ORBITER_MAKE_DOC)
	if(Doxygen_FOUND)
		add_subdirectory(doxygen)
	endif()
endif()

install(DIRECTORY html include
	DESTINATION ${ORBITER_INSTALL_SDK_DIR}
)

# Copy Prebuild PDFs ------------------------------------------------------

install(FILES doc/BuildingSamplesLinux.pdf DESTINATION ${ORBITER_INSTALL_SDK_DIR}/doc RENAME "BuildingSamples (Linux).pdf")

