# not upstream: Coll.Dmg3.Crash70 and Coll.Dmg3.Crash15, DGs head-on: 70 m/s crushes and breaks the noses with debris, 15 m/s only dents (dmg3)
from scnlib import fail
from dmg3lib import BREAK, DEBRIS, broke, crush_each, debris_vessels, dmg3, first_debris_frame, matches, no_error


def check(ctx):
    r = ctx.first()
    if 'Crash70' in ctx.args.name:
        brk, deb, s = broke(r)
        f = first_debris_frame(r)
        print('crash70: %d break lines, %d debris, first debris at simt %.3f, dmg3 %s' % (len(brk), len(deb), f['simt'], s))
        return
    no_error(r)
    crush_each(r)
    if matches(r, BREAK):
        fail('a break line at 15 m/s: %s' % matches(r, BREAK)[0].group(0))
    if matches(r, DEBRIS) or debris_vessels(r):
        fail('debris at 15 m/s')
    if first_debris_frame(r) is not None:
        fail('a CollDebris vessel in the dump at 15 m/s')
    s = dmg3(r)
    if s['breaks']:
        fail('dmg3 breaks=%d at 15 m/s, 0 expected' % s['breaks'])
    print('crash15: dents only, dmg3 %s' % s)
