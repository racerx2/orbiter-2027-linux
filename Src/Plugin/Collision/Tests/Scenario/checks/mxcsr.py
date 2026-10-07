# not upstream: Coll.MXCSR, the FP environment the test module sees is equal with the addon off and on (E4 7.6, 7.10)
from scnlib import fail


def check(ctx):
    lines = {}
    for rid in ctx.order:
        lines[rid] = [l.split(' mxcsr=')[1] for l in ctx.runs[rid].log if l.startswith('CollTestVessel ready ') and ' mxcsr=' in l]
        if not lines[rid]:
            fail('run %s: no MXCSR line' % rid)
    if lines['off'] != lines['on']:
        fail('MXCSR off %s, on %s' % (lines['off'], lines['on']))
