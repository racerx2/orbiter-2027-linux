# not upstream: Scn.Jitter, a paced run follows TESTPACE and the slept frame is long (E4 7.10; harness proof for E1's jitter tests)
from scnlib import fail

PACE, SLEEP_K = 0.0167, 30


def check(ctx):
    d = ctx.first().dump
    ks = sorted(d.frames)
    h = {k: d.frames[ks[i + 1]]['simt'] - d.frames[k]['simt'] for i, k in enumerate(ks[:-1])}
    slept = SLEEP_K + 1  # TESTSLEEP in pre-step k lengthens the next frame's SysDT (Orbiter.cpp:1799-1812), capped at 0.1 s
    if not 0.08 <= h[slept] <= 0.1 + 1e-9:
        fail('frame %d: h=%.4f s, about 0.1 s expected after TESTSLEEP 100 ms' % (slept, h[slept]))
    bad = [(k, round(v, 4)) for k, v in h.items() if k > 4 and k != slept and not PACE / 2 <= v <= 2 * PACE]
    if len(bad) > 2 or any(v > 0.05 for _, v in bad):  # real time: a loaded machine may stretch a frame or two
        fail('frames off the 16.7 ms pace by more than 2x: %s' % bad[:6])
    if bad:
        print('jitter: frames off the pace (tolerated): %s' % bad)
