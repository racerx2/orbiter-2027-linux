# not upstream: collision addon, E2's units (geometry sourcing and the SDK interface)
list(APPEND COLL_UNITS CollSdk CollMeshFile CollSourceA CollBaseA CollBaseObjA)
# the real SDK binding compiles here; the integrator moves it into COLL_UNITS with the host test's SDK doubles
add_library(Coll_CollSdkOrbiter OBJECT CollSdkOrbiter.cpp)
target_include_directories(Coll_CollSdkOrbiter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${COLL_SDK_INCLUDE})
set_target_properties(Coll_CollSdkOrbiter PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON FOLDER Modules/Collision)
