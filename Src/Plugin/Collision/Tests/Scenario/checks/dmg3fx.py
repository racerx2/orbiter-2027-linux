# not upstream: Coll.Dmg3.Fx, the 70 m/s DG crash headless requests effects: fx= of the dmg3 line above 0 (dmg3 F)
from scnlib import fail
from dmg3lib import dmg3, no_error


def check(ctx):
    r = ctx.first()
    no_error(r)
    s = dmg3(r)
    if s['fx'] < 1:
        fail('dmg3 fx=%d, effect requests expected' % s['fx'])
    lines = [l for l in r.log if l.startswith('Collision fx t=')]
    if not lines:
        fail('no "Collision fx t=" line at CollisionLog 2')
    print('fx: dmg3 %s, %d fx lines; first %s' % (s, len(lines), lines[0]))
