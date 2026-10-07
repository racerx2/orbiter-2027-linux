# not upstream: Scn.SanityCheck, a stock scenario runs headless to its frame limit (design-C-T 3.5)
from scnlib import fail


def check(ctx):
    r = ctx.first()
    for t in ('Finished setting up render state', '**** Fast process shutdown'):
        if not r.has(t):
            fail('no %r in Orbiter.log' % t)
