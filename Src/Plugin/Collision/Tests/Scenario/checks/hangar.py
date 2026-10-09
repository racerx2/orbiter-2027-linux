# not upstream: Coll.Base.Hangar, a vessel sliding into a Cape Canaveral hangar wall hits the building and damages it (review CA-adversarial-2, fix2 E)
from scnlib import fail, summary


def check(ctx):
    r = ctx.first()
    s = summary(r)
    if not s:
        fail('no summary line')
    if int(s.get('contacts', '0')) < 1:
        fail('summary contacts=%s, a building contact expected' % s.get('contacts'))
    hits = [l for l in r.log if l.startswith('Collision impact ') and "'HB'" in l and 'Cape Canaveral' in l]
    if not hits:
        fail('no Collision impact line between HB and a Cape Canaveral building')
    dmg = [l for l in r.log if l.startswith("Collision building t=") and "'Earth:Cape Canaveral'" in l]
    if not dmg:
        fail('no Collision building damage line for Earth:Cape Canaveral')
    if not any(l.startswith("CollTestVessel placed 'HB' at 'Cape Canaveral'") for l in r.log):
        fail('HB not placed at Cape Canaveral')
    print('hangar: %d impacts, %d building damage lines; first %s' % (len(hits), len(dmg), hits[0]))
