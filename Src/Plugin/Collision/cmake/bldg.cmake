# not upstream: collision addon, streamed 3D buildings as colliders (design bldg-collision 8): the client's BldgGeom.cpp as Coll_BldgGeom when its folder is there, and the CollBldgA unit
set(COLL_BLDGGEOM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../OVP/VulkanClient" CACHE PATH "Folder with BldgGeom.h and BldgGeom.cpp (OVP/VulkanClient of the Orbiter source tree); without them streamed buildings are not solid")
get_filename_component(COLL_BLDG_DIR "${COLL_BLDGGEOM_DIR}" ABSOLUTE)
set(COLL_HAVE_BLDGGEOM OFF)
set(COLL_BLDG_OBJS "")
if (EXISTS ${COLL_BLDG_DIR}/BldgGeom.h AND EXISTS ${COLL_BLDG_DIR}/BldgGeom.cpp)
	set(COLL_HAVE_BLDGGEOM ON)
	if (NOT TARGET Coll_BldgGeom)
		add_library(Coll_BldgGeom OBJECT ${COLL_BLDG_DIR}/BldgGeom.cpp)
		target_include_directories(Coll_BldgGeom PRIVATE ${COLL_BLDG_DIR}) # that folder only: no client header reaches the addon
		if (WIN32)
			target_compile_definitions(Coll_BldgGeom PRIVATE _USE_MATH_DEFINES) # M_PI: MSVC and strict mingw define it only on request
		endif ()
		set_target_properties(Coll_BldgGeom PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON FOLDER Modules/Collision)
	endif ()
	set(COLL_BLDG_OBJS $<TARGET_OBJECTS:Coll_BldgGeom>)
	message(STATUS "Collision: streamed buildings with ${COLL_BLDG_DIR}/BldgGeom.cpp")
else ()
	message(STATUS "Collision: no BldgGeom.cpp in ${COLL_BLDG_DIR} (COLL_BLDGGEOM_DIR); streamed buildings are not solid")
endif ()
list(APPEND COLL_UNITS CollBldgA)

# after the units' object libraries: BldgGeom.h for the two units that use it
function(coll_bldg_units)
	if (NOT COLL_HAVE_BLDGGEOM)
		return ()
	endif ()
	foreach (u CollBaseA CollBldgA)
		if (TARGET Coll_${u})
			target_compile_definitions(Coll_${u} PRIVATE COLL_HAVE_BLDGGEOM=1)
			target_include_directories(Coll_${u} PRIVATE ${COLL_BLDG_DIR}) # after the addon's own folder: its headers win
		endif ()
	endforeach ()
endfunction()
