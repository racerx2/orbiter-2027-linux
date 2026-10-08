# not upstream: Coll.MXCSR, the FP control bits (MXCSR without its sticky status flags) are equal with the addon off and on (E4 7.6, 7.10)
from scnlib import fail

TAGS = ('ready', 'post k=1', 'post k=5', 'last')  # creation, post-step of frames 1 and 5, the last post-step (M10)


def ctl(log):
    out = []
    for l in log:
        for t in TAGS:
            p = 'CollTestVessel ' + ('mxcsr ' if t != 'ready' else '') + t + ' '
            if l.startswith(p) and ' ctl=' in l:
                out.append((t, l.split(' ctl=')[1].strip()))
    return out


def check(ctx):
    lines = {}
    for rid in ctx.order:
        lines[rid] = ctl(ctx.runs[rid].log)
        for t in TAGS:
            if not any(x[0] == t for x in lines[rid]):
                fail('run %s: no MXCSR %s line' % (rid, t))
    if [x[1] for x in lines['off']] != [x[1] for x in lines['on']]:
        fail('MXCSR off %s, on %s' % (lines['off'], lines['on']))
