# not upstream: Coll.Bldg.Crash70, a DG at 70 m/s into a synthetic streamed building near KSC: the impact names the building, the DG dents (design bldg-collision 7)
from scnlib import fail
from dmg3lib import DENT, matches, no_error


def check(ctx):
    r = ctx.first()
    no_error(r)
    if not any(l.startswith("CollTestVessel placed 'DG' at 'Cape Canaveral'") for l in r.log):
        fail('DG not placed at Cape Canaveral')
    loads = [l for l in r.log if l.startswith('Collision bldg: Earth z14 ') and ' loaded in ' in l]
    if not loads:
        fail('no "Collision bldg: ... loaded in" line: the streamed building tile was not loaded')
    hits = [l for l in r.log if l.startswith('Collision impact ') and "'DG'" in l and "'building Earth z14 " in l]
    if not hits:
        ground = [l for l in r.log if l.startswith('Collision impact ') and "'DG'" in l]
        fail('no Collision impact line between DG and a streamed building%s' % (' (first impact: %s)' % ground[0][:160] if ground else ''))
    dents = [m for m in matches(r, DENT) if m.group(2) == 'DG']
    if not dents:
        fail('no dent of DG')
    print('%s: %s; %d building impacts (first %s), %d dents' % (ctx.args.name, loads[0], len(hits), hits[0][:140], len(dents)))
