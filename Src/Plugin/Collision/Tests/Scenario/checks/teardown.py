# not upstream: Scn.Teardown, the normal close path (DestroyWorld, back to the Launchpad) off and on, and the fast path on (design-C-T 3.5, E4 7.9.4)
from scnlib import fail, summary

CLOSE = '**** Closing simulation session'


def order(r, want):
    pos = 0
    for w in want:
        for i in range(pos, len(r.log)):
            if r.log[i].startswith(w):
                pos = i + 1
                break
        else:
            fail('run %s: no %r after line %d' % (r.spec.id, w, pos))


def check(ctx):
    off, on, fast = ctx.runs['off'], ctx.runs['on'], ctx.runs['onfast']
    for r in (off, on):
        if r.dump is None or r.dump.end != ctx.args.frames:
            fail('run %s: dump without END %d' % (r.spec.id, ctx.args.frames))
    order(on, ['Collision summary: ', 'Collision perf: ', 'Collision: session 1 ended (end)', CLOSE])
    if on.has('Collision: unloaded'):
        fail('run on: the normal close path unloaded the addon')
    order(fast, ['Collision summary: ', 'Collision perf: ', 'Collision: session 1 ended (end)', 'Collision: unloaded cmds=0', '**** Fast process shutdown'])
    if summary(on) is None or summary(fast) is None:
        fail('no A3 summary')
