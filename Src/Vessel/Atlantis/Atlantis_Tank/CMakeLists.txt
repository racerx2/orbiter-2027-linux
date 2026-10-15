# Copyright (c) Martin Schweiger
# Licensed under the MIT License

set(VESSEL_NAME Atlantis_Tank)
set(INSTALL_SUBDIR Atlantis)

set(MESH_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Meshes)
set(TEXTURE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Textures)
set(CONFIG_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Config)
set(THUMBNAIL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Thumbnail)

add_library(${VESSEL_NAME} SHARED
	Atlantis_Tank.cpp
)

set_target_properties(${VESSEL_NAME}
	PROPERTIES
	FOLDER Vessels/${VESSEL_NAME}
)

target_include_directories(${VESSEL_NAME}
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../Atlantis
	PUBLIC ${ORBITER_SOURCE_SDK_INCLUDE_DIR}
)

target_link_libraries(${VESSEL_NAME}
	${ORBITER_LIB}
	${ORBITER_SDK_LIB}
	Atlantis_SRB # not upstream: by target name, CMake turns a path to a prefix-less .so into -l<name>
)

add_custom_command(
	TARGET ${VESSEL_NAME} PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${CONFIG_SOURCE_DIR}/ ${CMAKE_BINARY_DIR}/Config/Vessels # not upstream: copynewer.cmake keeps files changed in the build tree
)

add_custom_command(
	TARGET ${VESSEL_NAME} PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${MESH_SOURCE_DIR}/ ${ORBITER_BINARY_MESH_DIR}/${INSTALL_SUBDIR} # not upstream: copynewer.cmake keeps files changed in the build tree
)

add_custom_command(
	TARGET ${VESSEL_NAME} PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${TEXTURE_SOURCE_DIR}/ ${ORBITER_BINARY_TEXTURES_DIR}/${INSTALL_SUBDIR} # not upstream: copynewer.cmake keeps files changed in the build tree
)

add_custom_command(
	TARGET ${VESSEL_NAME} PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${THUMBNAIL_SOURCE_DIR}/ ${CMAKE_BINARY_DIR}/Images/Vessels/Default # not upstream: copynewer.cmake keeps files changed in the build tree
)

orbiter_link_depends_dirs(${VESSEL_NAME} ${CONFIG_SOURCE_DIR} ${MESH_SOURCE_DIR} ${TEXTURE_SOURCE_DIR} ${THUMBNAIL_SOURCE_DIR}) # not upstream: a changed data file relinks the module, so its PRE_BUILD copy runs

add_dependencies(${VESSEL_NAME}
	${OrbiterTgt}
	Orbitersdk
	Atlantis_SRB
)

install(TARGETS ${VESSEL_NAME}
	LIBRARY
	DESTINATION ${ORBITER_INSTALL_MODULE_DIR}
)
install(DIRECTORY ${CONFIG_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}/Config/Vessels
)
install(DIRECTORY ${MESH_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_MESH_DIR}/${INSTALL_SUBDIR}
)
install(DIRECTORY ${TEXTURE_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_TEXTURES_DIR}/${INSTALL_SUBDIR}
)
install(DIRECTORY ${THUMBNAIL_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}/Images/Vessels/Default
)
