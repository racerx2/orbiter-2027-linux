# Copyright (c) Martin Schweiger
# Licensed under the MIT License

add_library(DlgCtrl STATIC
	DlgCtrl.cpp
	DlgCtrlPropertyList.cpp
	DlgCtrlSwitch.cpp
	${CMAKE_SOURCE_DIR}/Orbitersdk/include/DlgCtrl.h
)

target_include_directories(DlgCtrl
	PUBLIC ${CMAKE_SOURCE_DIR}/Orbitersdk/include
)

# the controls are Qt widgets; modules (shared objects) link this archive too
target_link_libraries(DlgCtrl PUBLIC Qt6::Widgets)

set_target_properties(DlgCtrl
	PROPERTIES
	FOLDER Core
	AUTOMOC ON
	POSITION_INDEPENDENT_CODE ON
)

# Copy library to its Orbitersdk/lib location so that external project integrated into the build can find it
add_custom_command(TARGET DlgCtrl
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory ${ORBITER_BINARY_SDK_DIR}/lib
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:DlgCtrl> ${ORBITER_BINARY_SDK_DIR}/lib/ 
)

# Installation to SDK lib dir for inclusion by addons
install(TARGETS DlgCtrl
	ARCHIVE
	DESTINATION ${ORBITER_INSTALL_SDK_DIR}/lib
)
