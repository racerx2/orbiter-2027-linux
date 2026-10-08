# not upstream: Coll.Base.KSC, CollTestVessels resting and sliding 2.4 m from a Cape Canaveral hangar: no contact, no write, no drift (review CA-F 1)
from scnlib import fail, norm3, sub, summary


def check(ctx):
    r = ctx.first()
    s = summary(r)
    if not s:
        fail('no summary line')
    for key in ('events', 'writes', 'contacts', 'spec', 'missed'):
        if int(s.get(key, '-1')) != 0:
            fail('summary %s=%s next to the hangar, 0 expected' % (key, s.get(key)))
    for l in r.log:
        if l.startswith(('Collision impact ', 'Collision write ')):
            fail('next to the hangar: %s' % l)
    placed = [l for l in r.log if l.startswith("CollTestVessel placed '") and "'Cape Canaveral'" in l]
    if len(placed) != 3:
        fail('%d of 3 vessels placed at Cape Canaveral' % len(placed))
    d = r.dump
    k0, k1 = 25, max(d.frames)
    e0 = norm3(sub(d.vec(k0, 'PE', 'p'), d.vec(k0, 'PW', 'p')))
    e1 = norm3(sub(d.vec(k1, 'PE', 'p'), d.vec(k1, 'PW', 'p')))
    if abs(e1 - e0) > 0.05:
        fail('PE and PW at rest moved %.3f m apart from frame %d to %d' % (e1 - e0, k0, k1))
