# not upstream: Scn.Anim, CollTestAnim makes its parent and child animation and cycles them; CollTestAnimVC waits for a visual (E4 7.4)
from scnlib import fail


def check(ctx):
    r = ctx.first()
    if not r.has("CollTestAnim 'PB-A' animations parent="):
        fail('CollTestAnim made no animations in clbkSetClassCaps')
    if r.has("CollTestAnim 'PB-B' animations"):
        fail('CollTestAnimVC made its animations without a visual')
    if not r.has("CollTestAnim 'TA' animations parent="):
        fail('the CollTestAnim created by TESTCREATE made no animations')
    n = sum(1 for l in r.log if l.startswith('CollTestAnim act k=') and "TESTANIMCYCLE 'PB-A' 20" in l)
    if n < ctx.args.frames // 20:
        fail('TESTANIMCYCLE logged %d periods in %d frames' % (n, ctx.args.frames))
