# custom: launcher skins; the executable's side, the Skins data copy and its install rule

target_sources(Orbiter PRIVATE
	Custom/LauncherFacts.cpp
	Custom/ClassicHider.cpp
	Custom/LauncherApi.cpp
	Custom/LauncherSkin.cpp
	Custom/LauncherItem.cpp
	Custom/ResetKey.cpp
	Custom/UiForm.cpp
	Custom/LayoutApply.cpp
	Custom/LayoutSkin.cpp
	Custom/SkinCopy.cpp
	Custom/SkinStyle.cpp
)

add_custom_target(CopySkins ALL
	COMMAND ${ORBITER_COPY_NEWER} ${CMAKE_SOURCE_DIR}/Skins/ ${CMAKE_BINARY_DIR}/Skins # not upstream: copynewer.cmake keeps files changed in the build tree
)
set_target_properties(CopySkins PROPERTIES FOLDER Data)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/Skins DESTINATION ${ORBITER_INSTALL_ROOT_DIR})
