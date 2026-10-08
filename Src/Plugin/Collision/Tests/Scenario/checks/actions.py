# not upstream: Scn.Actions, every action of the smoke list logs its act line at its frame and has its effect (E4 T0.7)
import os
from scnlib import fail

ONCE = ('TESTMESH', 'TESTREPAIRAPI', 'TESTCREATE', 'TESTDV', 'TESTPLACEREL', 'TESTPLACEDOCK', 'TESTATT', 'TESTSPIN', 'TESTSHIFTCG',
        'TESTSHIFTCOM', 'TESTTHRUST', 'TESTKEY', 'TESTMOVEDOCK', 'TESTSLEEP', 'TESTJUMP', 'TESTWARP', 'TESTJUMPPOST', 'TESTDELETEPOST')


def check(ctx):
    r = ctx.first()
    acts = [l for l in r.log if l.startswith('CollTestVessel act ') or l.startswith('CollTestHarness act ')]
    want = [l.strip() for l in open(os.path.join(ctx.args.data, 'act', 'actions.txt')) if l.strip() and not l.startswith(';')]
    for w in want:
        t = w.split()
        if t[0] in ONCE and t[2] != 'cycle':
            if not any(a.startswith('CollTestVessel act k=%s ' % t[1]) and a.endswith(' ' + w) for a in acts):
                fail('no act line for %r' % w)
    for a in ('TESTTHRUSTRAMP 20 30', 'TESTMASS 74 76'):
        n = sum(1 for l in acts if (' ' + a + ' ') in l)
        if n != 2:
            fail('%s: %d act lines, 2 expected (start and end)' % (a, n))
    for kind in ('LUACALL PB-B create_propellantresource', 'LUASTATUS PB-B', 'LUASAVE smoke'):
        if not any(l.startswith('CollTestHarness act k=') and l.endswith(kind) for l in acts):
            fail('no harness act line for %s' % kind)
    if sum(1 for l in acts if 'TESTMESH cycle ' in l) < 10:
        fail('TESTMESH cycle ran fewer than 10 steps')
    if not r.has('CollTestVessel collaRepairVessel missing'):
        fail('TESTREPAIRAPI: no "collaRepairVessel missing" line before E3 adds the export')
    slots = [l for l in r.log if l.startswith('CollTestVessel slots k=')]
    if len(slots) < ctx.args.frames:
        fail('TESTMESHLOG: %d slot lines' % len(slots))
    if not any(l.startswith('CollTestVessel slots k=4 ') and ' 0:- ' in l + ' ' for l in slots):
        fail('TESTMESH del 0: slot 0 not a hole at k=4')
    if not any(l.startswith('CollTestVessel rng k=25 ') for l in r.log):
        fail('TESTRNGPROBE: no rng line at k=25')
    d = r.dump
    if 'TC' in d.frames[13]['V'] or 'TC' not in d.frames[14]['V'] or 'TC' not in d.frames[70]['V'] or 'TC' in d.frames[71]['V']:
        fail('TC must be in the dumps of frames 14 (created in pre-step 14) to 70 (deleted in post-step 70)')
    if abs(d.frames[60]['simt'] - d.frames[59]['simt'] - 10 - 0.02) > 1e-6:  # the jump frame's SimT0 holds the jump (its step then has length 0)
        fail('TESTJUMP 60 10: simt %r -> %r' % (d.frames[59]['simt'], d.frames[60]['simt']))
    if float(d.frames[63]['warp']) != 2 or float(d.frames[65]['warp']) != 1:
        fail('TESTWARP: warp %s at 63, %s at 65' % (d.frames[63]['warp'], d.frames[65]['warp']))
    if float(d.frames[75]['V']['PB-B']['m']) - float(d.frames[73]['V']['PB-B']['m']) < 99:
        fail('TESTMASS: PB-B mass %s at 73, %s at 75' % (d.frames[73]['V']['PB-B']['m'], d.frames[75]['V']['PB-B']['m']))
    for m in (('Modules', 'CollTestVessel.so'), ('Modules', 'CollTestAnim.so'), ('Modules', 'Plugin', 'CollTestHarness.so')):
        if os.path.lexists(os.path.join(ctx.args.root, *m)):
            fail('test module %s in the build root: the Launchpad would list it' % os.path.join(*m))
    if not os.path.isfile(r.path('Scenarios', 'Tests', 'Coll', 'Saved', 'smoke.scn')):
        fail('LUASAVE: Scenarios/Tests/Coll/Saved/smoke.scn missing')
