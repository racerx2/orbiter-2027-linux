#!/usr/bin/env python3
# the Orbiter.exe names a Windows module may import: the 2010 and 2016 exes' export tables and the 2024 SDK's exported declarations,
# without the graphics client's parts (no Windows graphics client runs through the bridge)
#   apilist.py <api dir> <Windows SDK include dir> <clang resource dir> <wine include dir> <shim dir>  > api/imports.txt
import re, sys
import clang.cindex as ci
api, inc, res, wine, shim = sys.argv[1:6]
FUNCS = (ci.CursorKind.FUNCTION_DECL, ci.CursorKind.CXX_METHOD, ci.CursorKind.CONSTRUCTOR, ci.CursorKind.DESTRUCTOR)
EXP = (ci.CursorKind.DLLEXPORT_ATTR, ci.CursorKind.DLLIMPORT_ATTR)
CLIENT = re.compile (r'(GraphicsClient|ParticleStream)@oapi@@|ElevationTile@@') # as scope or parameter (ModuleNV is Module's base too)
def exported (c):
	if any (k.kind in EXP for k in c.get_children ()): return True
	p = c.semantic_parent
	while p is not None and p.kind in (ci.CursorKind.CLASS_DECL, ci.CursorKind.STRUCT_DECL):
		if any (k.kind in EXP for k in p.get_children ()): return True
		p = p.semantic_parent
	return False
names = set ()
for f in ('exports2010.txt', 'exports2016.txt'):
	names |= {l.strip () for l in open (api + '/' + f) if l.strip ()}
text = ''.join ('#include "%s"\n' % h for h in ('Orbitersdk.h', 'MFDAPI.h', 'CelBodyAPI.h', 'DrawAPI.h', 'CamAPI.h', 'DlgCtrl.h'))
args = ['-x', 'c++', '--target=i686-windows', '-fms-extensions', '-fms-compatibility', '-nostdinc', '-isystem', res + '/include',
	'-isystem', wine + '/msvcrt', '-isystem', wine + '/windows', '-isystem', shim, '-isystem', inc, '-DSTRICT']
tu = ci.Index.create ().parse ('t.cpp', args = args, unsaved_files = [('t.cpp', text)])
errs = [str (d) for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
if errs: sys.exit ('apilist: ' + '; '.join (errs[:5]))
for c in tu.cursor.walk_preorder ():
	if c.kind in FUNCS and c.location.file and c.location.file.name.startswith (inc) and exported (c) and not c.is_pure_virtual_method ():
		names.add (c.mangled_name)
for m in sorted (n for n in names if not CLIENT.search (n)):
	print (m)
