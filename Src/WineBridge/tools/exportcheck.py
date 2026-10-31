#!/usr/bin/env python3
# exportcheck.py <dll> <list> <stamp>: every name of the list (one per line, # comments) is exported by the DLL; else the build stops
import os, sys
sys.dont_write_bytecode = True # no __pycache__ in the source tree
sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from pe import exports

have = set (exports (sys.argv[1]))
want = [l.split ('#')[0].strip () for l in open (sys.argv[2])]
missing = [n for n in want if n and n not in have]
if missing:
    sys.exit ('%s lacks %d names of %s:\n  %s' % (os.path.basename (sys.argv[1]), len (missing), sys.argv[2], '\n  '.join (missing)))
open (sys.argv[3], 'w').write ('%d names\n' % len ([n for n in want if n]))
