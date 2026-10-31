#!/usr/bin/env python3
# every import of the list is exported by the helper, but the names of the other lists: coverage.py <Orbiter.exe> <api/imports.txt> <stamp> [api/later.txt gen/absent.txt]
import subprocess, sys
exe, lst, stamp = sys.argv[1:4]
rows = lambda f: {l.split ('#')[0].strip () for l in open (f)} - {''}
out = subprocess.run (['objdump', '-p', exe], capture_output = True, text = True, check = True).stdout
exported = {l.split ()[-1] for l in out.split ('[Ordinal/Name Pointer] Table')[-1].splitlines () if '+base[' in l}
want = rows (lst)
later = set ()
for f in sys.argv[4:]: later |= rows (f) & want  # api/later.txt, and what this Orbiter doesn't define (gen/absent.txt)
missing = sorted (want - later - exported)
if missing:
	sys.exit ('coverage: %d imports are not exported by the helper:\n  %s' % (len (missing), '\n  '.join (missing)))
msg = '%d of %d imports exported, %d not bridged yet (api/later.txt, gen/absent.txt)' % (len (want) - len (later), len (want), len (later))
open (stamp, 'w').write (msg + '\n')
print ('coverage: ' + msg)
