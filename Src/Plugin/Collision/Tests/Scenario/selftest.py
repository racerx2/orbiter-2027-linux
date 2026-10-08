# not upstream: Scn.Selftest, the checkers of the collision addon scenario tests on synthetic dumps and logs (Design CA E4 T0.3, design-C-T T0.3)
import argparse
import importlib.util
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import runner  # noqa: E402
import scnlib  # noqa: E402

RUNID = 'self-1'
PINS = {'collisionmodel': '1', 'collisionresponse': 'TRUE', 'collisioncheck': 'TRUE', 'collisionlog': '2'}
A1 = 'Collision: active version=0.1.0 session=1 model=1 response=1 check=1 damage=0 log=2 render=0'
QUIET = 'Collision summary: frames=10 contacts=0 events=0 writes=0 probes=0 vtx=0 matrix=0 notices=0 spec=0 free=0 missed=0'


class Spec:
    def __init__(self, addon='on', exit=0):
        self.id, self.addon, self.exit, self.endon, self.dump, self.expect = 'self', addon, exit, None, True, None


class Args:
    frames = 3


class FakeRun(scnlib.RunResult):
    def __init__(self, dump_text, log, addon='on'):
        self.spec, self.dir, self.exit, self.status, self.secs, self.pins, self.runid = Spec(addon), '.', 0, None, 0.0, PINS, RUNID
        self.log, self.dump = log, scnlib.Dump(dump_text)


def g(x):
    return '%.17g' % x


def vline(name, p, v, w=(0, 0, 0), R=(1, 0, 0, 0, 1, 0, 0, 0, 1), m=500.0, I=(2.28, 2.31, 0.79)):
    j = lambda t: ','.join(g(x) for x in t)
    return 'V %s ShuttlePB fs=0 m=%s p=%s v=%s w=%s R=%s I=%s ref=Earth dk=- par=-' % (name, g(m), j(p), j(v), j(w), j(R), j(I))


def pair_dump(frames=3, h=0.02, vA=0.5, vB=-0.5, kick=None, end=True):  # PB-A at the origin nose +z, PB-B 7 m ahead nose -z
    out = ['# synthetic', 'H scn=self h=%s version=0 run=%s' % (h, RUNID)]
    pa, pb = [0.0, 0.0, 0.0], [0.0, 0.0, 7.0]
    va, vb = [0.0, 0.0, vA], [0.0, 0.0, vB]
    for k in range(1, frames + 1):
        out.append('F %d %s 51982.5 1' % (k, g((k - 1) * h)))
        out.append(vline('PB-A', pa, va))
        out.append(vline('PB-B', pb, vb, R=(-1, 0, 0, 0, 1, 0, 0, 0, -1)))
        if kick and k == kick[0]:
            va[2] += kick[1] / 500.0
            vb[2] -= kick[2] / 500.0
        for p, v in ((pa, va), (pb, vb)):
            for i in range(3):
                p[i] += v[i] * h
    if end:
        out.append('END %d' % frames)
    return '\n'.join(out) + '\n'


class OneRun:
    def __init__(self, run):
        self.run = run

    def first(self):
        return self.run


def placebase_case(elev, ignore=False, td=1.4865):  # Moon at the origin, pad 2 on +x; PB landed by the core, PL placed 3 m above the terrain
    R, rb = 1737400.0, 1737400.0 + elev
    rpl = (R if ignore else rb) + 3.0
    d = ['H scn=self h=0.02 version=0 run=%s' % RUNID]
    for k in (1, 2, 3):
        d.append('F %d %s 51982.5 1' % (k, g((k - 1) * 0.02)))
        d.append('B Moon p=0,0,0')
        d.append(vline('PB', (rb + td, 0.0, 0.0), (0.0, 0.0, 4.6)))
        d.append(vline('PL', (rpl, 0.03, 0.0), (0.0, 0.0, 4.6)))
    d.append('END 3')
    placed = "CollTestVessel placed 'PL' at 'Brighton Beach' lng=0 lat=0 rad=%s base=%s elev=%s" % (g(rpl), g(R), g(elev))
    return OneRun(FakeRun('\n'.join(d) + '\n', [A1, placed]))


def check_placebase():  # M13: the check fails when TESTPLACEBASE ignores the terrain, skips without elevation data
    spec = importlib.util.spec_from_file_location('check_placebase', os.path.join(HERE, 'checks', 'placebase.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    e = expect_pass('placebase on terrain 2.6 km below the mean radius', lambda: m.check(placebase_case(-2600.0)))
    e += expect_fail('placebase ignoring the elevation', lambda: m.check(placebase_case(-2600.0, ignore=True)))
    e += expect_fail('placebase 0.1 m high', lambda: m.check(placebase_case(-2600.0, td=1.3865)))
    try:
        m.check(placebase_case(0.0))
        print('selftest: placebase without elevation data did not skip')
        e += 1
    except scnlib.Skip:
        pass
    return e


def expect_fail(what, fn):
    try:
        fn()
    except scnlib.CheckFail:
        return 0
    print('selftest: %s was not detected' % what)
    return 1


def expect_pass(what, fn):
    try:
        fn()
    except scnlib.CheckFail as e:
        print('selftest: %s failed: %s' % (what, e))
        return 1
    return 0


def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument('--work', default='')
    a = p.parse_args(argv)
    e = 0
    ok = FakeRun(pair_dump(), [A1, QUIET])

    # bitwise compare: equal passes, one ulp fails
    d1, d2 = scnlib.Dump(pair_dump()), scnlib.Dump(pair_dump())
    e += expect_pass('equal dumps', lambda: scnlib.same_dumps(d1, d2, 'self'))
    text = pair_dump()
    x = 0.5
    bumped = text.replace('v=0,0,0.5 ', 'v=0,0,%s ' % g(math.nextafter(x, 1.0)), 1)
    if bumped == text:
        print('selftest: no value to bump')
        e += 1
    e += expect_fail('a 1 ulp change', lambda: scnlib.same_dumps(d1, scnlib.Dump(bumped), 'self'))

    # generic run checks: a clean run passes; missing END, a check line, a missing or wrong A1 pin, a harness error fail
    e += expect_pass('a clean run', lambda: scnlib.run_checks(None, ok, Args))
    e += expect_fail('a missing END', lambda: scnlib.run_checks(None, FakeRun(pair_dump(end=False), [A1, QUIET]), Args))
    e += expect_fail('a stale dump', lambda: scnlib.run_checks(None, FakeRun(pair_dump().replace(RUNID, 'old'), [A1, QUIET]), Args))
    e += expect_fail('a Collision check failed: line', lambda: scnlib.run_checks(None, FakeRun(pair_dump(), [A1, 'Collision check failed: momentum'], ), Args))
    e += expect_fail('a missing A1 line', lambda: scnlib.run_checks(None, FakeRun(pair_dump(), [QUIET]), Args))
    for k, bad in (('model', 'model=0'), ('response', 'response=0'), ('check', 'check=0'), ('log', 'log=1')):
        e += expect_fail('an A1 %s off its pin' % k, lambda bad=bad, k=k: scnlib.run_checks(None, FakeRun(pair_dump(), [A1.replace(k + '=' + A1.split(k + '=')[1].split()[0], bad)]), Args))
    e += expect_fail('Collision lines in an off run', lambda: scnlib.run_checks(None, FakeRun(pair_dump(), [A1], addon='off'), Args))
    e += expect_fail('a Collision: error in line', lambda: scnlib.run_checks(None, FakeRun(pair_dump(), [A1, 'Collision: error in clbkPreStep: x; collisions off until the session ends']), Args))
    e += expect_fail('a harness error', lambda: scnlib.run_checks(None, FakeRun(pair_dump(), [A1, 'CollTestHarness error: x']), Args))

    # quiet scenes (G5): writes=0, notices=0, no contact line
    e += expect_pass('a quiet run', lambda: scnlib.quiet(ok))
    e += expect_fail('writes=1 in a quiet test', lambda: scnlib.quiet(FakeRun(pair_dump(), [A1, QUIET.replace('writes=0', 'writes=1')])))
    e += expect_fail('notices=1 in a quiet test', lambda: scnlib.quiet(FakeRun(pair_dump(), [A1, QUIET.replace('notices=0', 'notices=1')])))
    e += expect_fail('a contact line in a quiet test', lambda: scnlib.quiet(FakeRun(pair_dump(), [A1, QUIET, 'Collision t=1 PB-A PB-B'])))
    e += expect_fail('a missing summary', lambda: scnlib.quiet(FakeRun(pair_dump(), [A1])))

    # momentum (differential, T 4.4): an internal impulse passes, a residual of 1e-6 fails
    off = scnlib.Dump(pair_dump(frames=4))
    on = scnlib.Dump(pair_dump(frames=4, kick=(2, -500.0, -500.0)))
    e += expect_pass('an internal impulse', lambda: scnlib.momentum_check(off, on, 2))
    res = scnlib.Dump(pair_dump(frames=4, kick=(2, -500.0, -500.0 * (1 - 1e-6))))
    e += expect_fail('a momentum residual of 1e-6', lambda: scnlib.momentum_check(off, res, 2))

    # gap proxy and impact frame
    gap = scnlib.gap_pb(off, 1)
    if abs(gap - 1.0) > 1e-12:
        print('selftest: gap %r, expected 1' % gap)
        e += 1
    if scnlib.frame_of(off, 0.03) != 2 or scnlib.frame_of(off, 0.0) != 1 or scnlib.frame_of(off, -1) is not None:
        print('selftest: frame_of wrong')
        e += 1

    # PNG round trip through all five filters; camera: the target centre projects to the image centre
    w, h, ch = 7, 5, 3
    pix = bytes((x * 37 + y * 91 + c * 13) & 255 for y in range(h) for x in range(w) for c in range(ch))
    if scnlib.png_decode(scnlib.png_encode(w, h, ch, pix, filters=(0, 1, 2, 3, 4))) != (w, h, ch, pix):
        print('selftest: PNG round trip differs')
        e += 1
    r, phi, theta = scnlib.camera_for_dent((0.0, 1.0, 2.0), (0.0, 0.0, 1.0), 3.5)
    c0 = scnlib.camera_project((0.0, 0.0, 0.0), r, phi, theta, 3.5, 20)
    if not c0 or abs(c0[0] - 640) > 1e-6 or abs(c0[1] - 360) > 1e-6:
        print('selftest: camera centre %r' % (c0,))
        e += 1
    up = scnlib.camera_project((0.0, 0.5, 0.0), r, phi, theta, 3.5, 20)
    if not up or up[1] >= 360:
        print('selftest: camera up axis %r' % (up,))
        e += 1

    e += check_placebase()

    # the runner's own guards (T0.1)
    if a.work:
        e += runner.selftest_guards(a.work)
    print('selftest: %s (%d errors)' % ('FAIL' if e else 'PASS', e))
    return 1 if e else 0


if __name__ == '__main__':
    sys.exit(main())
