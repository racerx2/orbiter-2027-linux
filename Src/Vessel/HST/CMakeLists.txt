# Copyright (c) Martin Schweiger
# Licensed under the MIT License

# Compile in Lua interface for executing HST functions?
set(ADD_SCRIPT_SUPPORT 1)

set(MESH_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Meshes)
set(TEXTURE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Textures)
set(CONFIG_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Config)
set(THUMBNAIL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Thumbnail)

add_library(HST SHARED
	HST.cpp
	HST_Lua.cpp
)

set_target_properties(HST
	PROPERTIES
	FOLDER Vessels
)

target_include_directories(HST
	PUBLIC ${ORBITER_SOURCE_SDK_INCLUDE_DIR}
)

target_link_libraries(HST
	${ORBITER_LIB}
	${ORBITER_SDK_LIB}
)

add_dependencies(HST
	${OrbiterTgt}
	Orbitersdk
)

if(ADD_SCRIPT_SUPPORT)
	add_definitions(-DSCRIPTSUPPORT)
	target_link_libraries(HST lua::lib)
endif()

add_custom_command(
	TARGET HST PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${CONFIG_SOURCE_DIR}/ ${CMAKE_BINARY_DIR}/Config/Vessels # not upstream: copynewer.cmake keeps files changed in the build tree
)

add_custom_command(
	TARGET HST PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${MESH_SOURCE_DIR}/ ${CMAKE_BINARY_DIR}/Meshes # not upstream: copynewer.cmake keeps files changed in the build tree
)

add_custom_command(
	TARGET HST PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${TEXTURE_SOURCE_DIR}/ ${ORBITER_BINARY_TEXTURES_DIR}   # this should really go to its own subdirectory # not upstream: copynewer.cmake keeps files changed in the build tree
)

add_custom_command(
	TARGET HST PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${THUMBNAIL_SOURCE_DIR}/ ${CMAKE_BINARY_DIR}/Images/Vessels/Default # not upstream: copynewer.cmake keeps files changed in the build tree
)

orbiter_link_depends_dirs(HST ${CONFIG_SOURCE_DIR} ${MESH_SOURCE_DIR} ${TEXTURE_SOURCE_DIR} ${THUMBNAIL_SOURCE_DIR}) # not upstream: a changed data file relinks the module, so its PRE_BUILD copy runs

#Installation
install(TARGETS HST
	LIBRARY
	DESTINATION ${ORBITER_INSTALL_MODULE_DIR}
)
install(DIRECTORY ${CONFIG_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}/Config/Vessels
)
install(DIRECTORY ${MESH_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}/Meshes
)
install(DIRECTORY ${TEXTURE_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_TEXTURES_DIR}
)
install(DIRECTORY ${THUMBNAIL_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}/Images/Vessels/Default
)
