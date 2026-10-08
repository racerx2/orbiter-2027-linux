# not upstream: Scn.RecPlay, the recorder started by a simulated Ctrl-C records, and the playback follows the recording (design-C-T T0.8, E4 7.5)
import math
import re
from scnlib import fail, norm3, sub


def stem(ctx):
    return re.sub(r'[^A-Za-z0-9]+', '_', ctx.args.name).strip('_')


def recorded(r, s, vessels=('PB-A', 'PB-B')):
    for v in vessels:
        p = 'Flights/%s/%s.pos' % (s, v)
        if p not in r.files or r.files[p][0] < 100 or r.files[p][1] < r.start:
            fail('run %s: %s missing, empty or older than the run' % (r.spec.id, p))
    p = 'Scenarios/Playback/%s.scn' % s
    if p not in r.files or r.files[p][1] < r.start:
        fail('run %s: %s missing or older than the run' % (r.spec.id, p))
    for k in (1, ):
        if not any(l.startswith('CollTestVessel act k=%d ' % k) and 'TESTRECORD' in l for l in r.log):
            fail('run %s: no TESTRECORD act line at k=%d' % (r.spec.id, k))


def rel(d, k, v):
    return sub(d.vec(k, v, 'p'), tuple(float(x) for x in d.frames[k]['B']['Earth']['p'].split(',')))


def samples(r, s, v):
    return [float(l.split()[0]) for l in open(r.path('Flights', s, v + '.pos')) if l[:1].isdigit()]


def check(ctx):  # the core labels each sample, the state at SimT0, with SimT1 (FlightRecorder.cpp:164-176): playback is one frame of Earth rotation off
    rec, play = ctx.runs['rec'], ctx.runs['play']
    s = stem(ctx)
    recorded(rec, s)
    if not any(l.startswith('CollTestVessel act k=%d ' % ctx.args.frames) and 'TESTRECORD' in l for l in rec.log):
        fail('rec: the recorder was not stopped at k=%d' % ctx.args.frames)
    if 'FLIGHTDATA' not in open(play.path('Scenarios', 'Tests', 'Coll', s + '.scn')).read():
        fail('play: the playback scenario has no FLIGHTDATA vessel')
    h, w_earth = ctx.args.step, 2 * math.pi / 86164.0905
    worst = 0.0
    for v in ('PB-A', 'PB-B'):
        tend = samples(rec, s, v)[-2]  # the last interval ends at a sample taken at the stop (worse, see the commit)
        n = 0
        for k in sorted(play.dump.frames):
            if k < 2 or play.dump.frames[k]['simt'] > tend:
                continue
            n += 1
            res = norm3(sub(rel(play.dump, k, v), rel(rec.dump, k, v)))
            bound = w_earth * norm3(rel(rec.dump, k, v)) * h + 0.01
            if res > bound:
                fail('%s frame %d: playback %.4f m off the recording, bound %.4f m' % (v, k, res, bound))
            worst = max(worst, res - bound + 0.01)
        if 2 * n < len(play.dump.frames):  # at least half the frames compared
            fail('%s: only %d of %d playback frames compared' % (v, n, len(play.dump.frames)))
    print('recplay: playback within %.4g m of the recording beyond one frame of Earth rotation' % worst)
