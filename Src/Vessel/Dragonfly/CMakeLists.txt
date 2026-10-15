set(MESH_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Meshes)
set(TEXTURE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Textures)
set(TEXTURE2_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Textures2)
set(CONFIG_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Config)
set(THUMBNAIL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Thumbnail)

# not upstream: the root lookup is left out (Vulkan graphics), but the ADI ball is drawn offscreen with OpenGL
find_package(OpenGL)
find_package(Qt6 COMPONENTS OpenGL)

if(OPENGL_FOUND AND OPENGL_GLU_FOUND AND TARGET Qt6::OpenGL) # OpenGL is required for this target; not upstream: GLU and Qt6::OpenGL too, else the vessel is left out

add_library(Dragonfly SHARED
	Dragonfly.cpp
	Internal.cpp
	instruments.cpp
	Matrix.cpp
	quaternion.cpp
	vectors.cpp
	Hsystems.cpp
	Thermal.cpp
	Esystems.cpp
)
orbiter_rc_resources(Dragonfly panel.rc)

set_target_properties(Dragonfly
	PROPERTIES
	FOLDER Vessels
)

target_include_directories(Dragonfly
	PUBLIC ${ORBITER_SOURCE_SDK_INCLUDE_DIR}
)

target_link_libraries(Dragonfly
	${ORBITER_LIB}
	${ORBITER_SDK_LIB}
	OpenGL::GL
	OpenGL::GLU
	Qt6::OpenGL # not upstream: QOpenGLContext/QOffscreenSurface/QOpenGLFramebufferObject replace WGL and the DIB section
)

add_dependencies(Dragonfly
	${OrbiterTgt}
	Orbitersdk
)

add_custom_command(
	TARGET Dragonfly PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${CONFIG_SOURCE_DIR}/ ${CMAKE_BINARY_DIR}/Config/Vessels # not upstream: copynewer.cmake keeps files changed in the build tree
)

add_custom_command(
	TARGET Dragonfly PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${MESH_SOURCE_DIR}/ ${CMAKE_BINARY_DIR}/Meshes/Dragonfly # not upstream: was Meshes, but the module loads "Dragonfly\Dragonfly" (install uses Meshes/Dragonfly); copynewer.cmake keeps files changed in the build tree
)

add_custom_command(
	TARGET Dragonfly PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${TEXTURE_SOURCE_DIR}/ ${ORBITER_BINARY_TEXTURES_DIR}/Dragonfly # not upstream: copynewer.cmake keeps files changed in the build tree
)

add_custom_command(
	TARGET Dragonfly PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${TEXTURE2_SOURCE_DIR}/ ${ORBITER_BINARY_TEXTURES2_DIR}/Dragonfly # not upstream: copynewer.cmake keeps files changed in the build tree
)

add_custom_command(
	TARGET Dragonfly PRE_BUILD
	COMMAND ${ORBITER_COPY_NEWER} ${THUMBNAIL_SOURCE_DIR}/ ${CMAKE_BINARY_DIR}/Images/Vessels/Default # not upstream: copynewer.cmake keeps files changed in the build tree
)

orbiter_link_depends_dirs(Dragonfly ${CONFIG_SOURCE_DIR} ${MESH_SOURCE_DIR} ${TEXTURE_SOURCE_DIR} ${TEXTURE2_SOURCE_DIR} ${THUMBNAIL_SOURCE_DIR}) # not upstream: a changed data file relinks the module, so its PRE_BUILD copy runs

install(TARGETS Dragonfly
	LIBRARY
	DESTINATION ${ORBITER_INSTALL_MODULE_DIR}
)
install(DIRECTORY ${CONFIG_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}/Config/Vessels
)
install(DIRECTORY ${MESH_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}/Meshes/Dragonfly
)
install(DIRECTORY ${TEXTURE_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_TEXTURES_DIR}/Dragonfly
)
install(DIRECTORY ${TEXTURE2_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_TEXTURES2_DIR}/Dragonfly
)
install(DIRECTORY ${THUMBNAIL_SOURCE_DIR}/
	DESTINATION ${ORBITER_INSTALL_ROOT_DIR}/Images/Vessels/Default
)

else()

message(WARNING "OpenGL libraries not found. Skipping Dragonfly build.")

endif()
