# not upstream: G5.Land and G5.Warp, addon on equals addon off bitwise in a quiet scene with module writes (E4 7.6 item 7, 7.10)
from scnlib import fail, quiet, same_dumps


def check(ctx):
    off, on = ctx.runs['off'], ctx.runs['on']
    same_dumps(off.dump, on.dump, 'off vs on')
    quiet(on)
    for l in on.log:
        if l.startswith('Collision warp:'):
            fail('run on: %s' % l)
    acts = [l for l in off.log if l.startswith('CollTestVessel act ')]
    if not acts or acts != [l for l in on.log if l.startswith('CollTestVessel act ')]:
        fail('the actions differ between off and on, or none ran')
