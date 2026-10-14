# not upstream: draws Textures/Bldg (claude/design-bldg-textures.md) at build time; our own code, nothing downloaded

add_executable(bldgtex
	bldgtex.cpp
	${CMAKE_SOURCE_DIR}/OVP/VulkanClient/BldgGeom.cpp
	${CMAKE_SOURCE_DIR}/OVP/VulkanClient/AutoExpo.cpp # the night look map's mips (design-night-look 6)
)
target_include_directories(bldgtex PRIVATE ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)
target_link_libraries(bldgtex Qt6::Gui stb_dxt)
set_target_properties(bldgtex PROPERTIES FOLDER Tools BUILD_WITH_INSTALL_RPATH OFF) # runs at build time: finds the Qt it linked, also outside the system folders (the release build)

set(BLDGTEX_DIR ${ORBITER_BINARY_TEXTURES_DIR}/Bldg)
set(BLDGTEX_FILES ${BLDGTEX_DIR}/facade.dds ${BLDGTEX_DIR}/facade_emis.dds ${BLDGTEX_DIR}/facade_norm.dds ${BLDGTEX_DIR}/facade_nite.dds ${BLDGTEX_DIR}/roof.dds ${BLDGTEX_DIR}/roof_norm.dds)
add_custom_command(
	OUTPUT ${BLDGTEX_FILES}
	COMMAND ${CMAKE_COMMAND} -E make_directory ${BLDGTEX_DIR}
	COMMAND bldgtex ${BLDGTEX_DIR}
	DEPENDS bldgtex
	COMMENT "Drawing the streamed buildings' textures"
)
add_custom_target(BldgTextures ALL DEPENDS ${BLDGTEX_FILES})
set_target_properties(BldgTextures PROPERTIES FOLDER Tools)

install(FILES ${BLDGTEX_FILES} DESTINATION ${ORBITER_INSTALL_TEXTURES_DIR}/Bldg)
