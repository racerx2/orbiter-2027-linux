#!/usr/bin/env python3
# interpnames.py <SDK include dir> <clang resource dir> <Wine include dir>: the MSVC names of the Interpreter class's members that
# SDK's Interpreter.h declares (what a module built with it may import from LuaInterpreter.dll), one per line
import os, re, sys
import clang.cindex as ci

inc, res, wine = sys.argv[1:4]
shim = os.path.join (os.path.dirname (os.path.abspath (__file__)), '..', 'helper', 'shim')
# the 2010 and 2016 headers include lua\lua.h: the folder separator as Windows reads it
files = []
for name in ('Interpreter.h',):
	text = open (os.path.join (inc, name), encoding = 'latin-1').read ()
	files.append ((os.path.join (inc, name), re.sub (r'(#include\s*[<"][^>"]*)\\', r'\1/', text)))
args = ['-x', 'c++', '--target=i686-windows', '-fms-extensions', '-fms-compatibility', '-nostdinc', '-isystem', res + '/include',
	'-isystem', wine + '/msvcrt', '-isystem', wine + '/windows', '-isystem', shim, '-isystem', inc, '-DSTRICT', '-DNOMINMAX']
tu = ci.Index.create ().parse ('names.cpp', args = args, unsaved_files = [('names.cpp', '#include "Interpreter.h"\n')] + files)
errs = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
if errs: sys.exit ('interpnames: ' + '; '.join (str (d) for d in errs[:5]))
names = set ()
def walk (c):
	for k in c.get_children ():
		if k.kind == ci.CursorKind.CLASS_DECL and k.spelling == 'Interpreter' and k.is_definition ():
			for m in k.get_children ():
				if m.kind in (ci.CursorKind.CXX_METHOD, ci.CursorKind.CONSTRUCTOR, ci.CursorKind.VAR_DECL): # the destructor: ??1 in the DLL's list
					if m.mangled_name: names.add (m.mangled_name)
		elif k.kind == ci.CursorKind.FUNCTION_DECL and k.spelling == 'lua_tovector':
			names.add (k.mangled_name)
		elif k.kind in (ci.CursorKind.NAMESPACE, ci.CursorKind.UNEXPOSED_DECL):
			walk (k)
walk (tu.cursor)
for n in sorted (names): print (n)
