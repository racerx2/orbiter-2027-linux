# not upstream: Coll.Ground.Crash70 and Coll.Base.Crash70, a DG crash into open ground or the KSC hangar dents, breaks Blast cells and throws debris (design-CA-ground)
from scnlib import fail
from dmg3lib import BLAST, DEBRIS, DENT, debris_vessels, matches, no_error


def check(ctx):
    r = ctx.first()
    no_error(r)
    if not any(l.startswith("CollTestVessel placed 'DG' at 'Cape Canaveral'") for l in r.log):
        fail('DG not placed at Cape Canaveral')
    ground = 'Ground' in ctx.args.name
    hits = [l for l in r.log if l.startswith('Collision impact ') and "'DG'" in l and (("'ground'" in l) if ground else ('Cape Canaveral' in l))]
    if not hits:
        fail('no Collision impact line between DG and %s' % ('the ground' if ground else 'a Cape Canaveral building'))
    if not ground and not [l for l in r.log if l.startswith("Collision building t=") and "'Earth:Cape Canaveral'" in l]:
        fail('no Collision building damage line')
    dents = [m for m in matches(r, DENT) if m.group(2) == 'DG']
    if not dents:
        fail('no dent of DG')
    b = [m for m in matches(r, BLAST) if m.group(1) == 'DG']
    if not b:
        fail('no Blast break of DG')
    deb = [m for m in b if m.group(6) != '-']
    names = set(m.group(1) for m in matches(r, DEBRIS))
    if not any(m.group(6) in names for m in deb):
        fail('no Blast debris vessel of DG')
    dv = debris_vessels(r)
    if not dv:
        fail('no CollDebris vessel in the last dump frame')
    print('%s: %d impacts (first %s), %d dents (first mode %s), %d Blast breaks, %d debris alive at the end' % (ctx.args.name, len(hits), hits[0][:120], len(dents), dents[0].group(5), len(b), len(dv)))
