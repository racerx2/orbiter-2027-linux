# not upstream: Scn.Twice, the same run twice gives the same dump, with the addon off and on (design-C-T T0.2, E4 T0.2)
from scnlib import fail, same_coll, same_dumps


def check(ctx):
    for a, b in (('off1', 'off2'), ('on1', 'on2')):
        ra, rb = ctx.runs[a], ctx.runs[b]
        same_dumps(ra.dump, rb.dump, '%s vs %s' % (a, b))
        if not ra.dump.frames:
            fail('run %s: empty dump' % a)
    same_coll(ctx.runs['on1'], ctx.runs['on2'], 'on1 vs on2')
