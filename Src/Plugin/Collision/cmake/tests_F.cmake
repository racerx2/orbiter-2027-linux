# not upstream: E1 (Phase F) unit tests of the collision addon
coll_unit_test(CollOrbMirror.Test 60 UNITS CollOrbMirror Vecmat)
coll_unit_test(CollAddonFrame.Test 300 UNITS CollAddonFrame CollOrbMirror CollSolveFrame CollSolve CollDetect CollGeom Vecmat)
coll_unit_test(CollWorldA.Test 60 UNITS CollWorldA CollAddonFrame CollOrbMirror CollSolveFrame CollSolve CollDetect CollGeom CollSdk Vecmat)
