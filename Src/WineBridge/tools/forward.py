#!/usr/bin/env python3
# forward.py <dll> <target> <def>: a .def whose every export forwards to the same name in <target>.dll (lua5.1.dll: one Lua, lua.dll's)
import os, sys
sys.dont_write_bytecode = True # no __pycache__ in the source tree
sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from pe import exports

names = exports (sys.argv[1])
if not names: sys.exit ('%s exports nothing' % sys.argv[1])
with open (sys.argv[3], 'w') as f:
    f.write ('EXPORTS\n')
    for n in names: f.write ('\t%s=%s.%s\n' % (n, sys.argv[2], n))
