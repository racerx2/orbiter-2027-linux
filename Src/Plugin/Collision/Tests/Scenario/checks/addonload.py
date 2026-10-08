# not upstream: Scn.AddonLoad, the addon on the fast close path (Design CA E4 7.10): load, A1, A3, ended (end), unloaded cmds=0
from scnlib import fail, summary


def check(ctx):
    r = ctx.first()
    want = ['Collision: loaded (addon ', 'Loading module Collision', 'Collision: session 1 created (', 'Collision: active version=',
            'Collision summary: ', 'Collision perf: ', 'Collision: session 1 ended (end)', 'Collision: unloaded cmds=0', '**** Fast process shutdown']
    pos = 0
    for w in want:
        for i in range(pos, len(r.log)):
            if r.log[i].startswith(w):
                pos = i + 1
                break
        else:
            fail('no %r after line %d of Orbiter.log' % (w, pos))
    s = summary(r)
    if int(s.get('frames', '0')) < ctx.args.frames:
        fail('summary frames=%s, at least %d expected' % (s.get('frames'), ctx.args.frames))
    if sum(1 for l in r.log if l.startswith('Collision: session ')) != 2:
        fail('one session expected (created and ended once)')
