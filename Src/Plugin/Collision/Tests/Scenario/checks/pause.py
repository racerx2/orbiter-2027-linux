# not upstream: Scn.Pause, TESTPAUSE pauses, the runner resumes through stdin, and the running frames equal a run without the pause (design-C-T 3.5, T0.9)
from scnlib import fail, same_dumps


def check(ctx):
    ref, p = ctx.runs['ref'], ctx.runs['pause']
    if not any(l.startswith('CollTestVessel act k=50 ') and l.endswith('TESTPAUSE 50') for l in p.log):
        fail('no TESTPAUSE act line at k=50')
    if p.secs < ref.secs + 0.4:
        fail('the paused run took %.2f s, the reference %.2f s: no pause seen' % (p.secs, ref.secs))
    same_dumps(ref.dump, p.dump, 'ref vs pause')
