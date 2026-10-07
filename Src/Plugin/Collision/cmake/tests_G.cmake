# not upstream: collision addon, E2's unit tests
coll_unit_test(CollSdk.Test 30 UNITS CollSdk Vecmat)
coll_unit_test(CollMeshFile.Test 120 DATA UNITS CollMeshFile CollShape CollAnim CollGeom CollSdk Vecmat)
coll_unit_test(CollSourceA.Test 60 UNITS CollSourceA CollBaseA CollBaseObjA CollMeshFile CollShape CollAnim CollGeom CollSdk DentMath Vecmat)
coll_unit_test(CollBaseA.Test 60 DATA UNITS CollBaseA CollBaseObjA CollMeshFile CollShape CollAnim CollGeom CollSdk DentMath Vecmat)
add_dependencies(CollSdk.Test Coll_CollSdkOrbiter) # keeps the real binding compiled by the addon-only build
