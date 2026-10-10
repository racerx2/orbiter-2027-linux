# not upstream: Coll.Dmg3.Crash70, Crash30 and Crash15, DGs head-on: 70 m/s crushes and breaks (Blast cells or a section tear), 15 m/s only dents (dmg3, blast)
from scnlib import fail
from dmg3lib import BLAST, BREAK, DEBRIS, blast70, blasts, broke, crash_speed, crush_each, debris_vessels, dmg3, first_debris_frame, matches, no_error, tear70, tears


def check(ctx):
    r = ctx.first()
    u = crash_speed(ctx)  # the scenario picks the branch, not the test name (Coll.Dmg3.Tear.Client is a 70 m/s crash)
    if u == 70:
        brk, deb, s = broke(r)
        f = first_debris_frame(r)
        if blasts(r):  # CollisionBlast: Blast decides, no section tear
            b = blast70(r)
            print('crash70: %d break lines, %d debris, first debris at simt %.3f, blast breaks %d (%s), dmg3 %s' % (len(brk), len(deb), f['simt'], len(blasts(r)), ' '.join('%s:%s' % (m.group(6), m.group(5)) for m in b), s))
            return
        t = tear70(r)
        print('crash70: %d break lines, %d debris, first debris at simt %.3f, tear d=%s, dmg3 %s' % (len(brk), len(deb), f['simt'], t[0].group(3), s))
        return
    no_error(r)
    crush_each(r)  # the ships met: a crash with no contact passes nothing
    if tears(r):
        fail('a tear line at %d m/s: %s' % (u, tears(r)[0].group(0)))
    if u == 30:
        print('crash30: no tear, %d blast breaks, dmg3 %s' % (len(blasts(r)), dmg3(r)))
        return
    if matches(r, BREAK):
        fail('a break line at 15 m/s: %s' % matches(r, BREAK)[0].group(0))
    if matches(r, BLAST):
        fail('a Blast break at 15 m/s: %s' % matches(r, BLAST)[0].group(0))
    if matches(r, DEBRIS) or debris_vessels(r):
        fail('debris at 15 m/s')
    if first_debris_frame(r) is not None:
        fail('a CollDebris vessel in the dump at 15 m/s')
    s = dmg3(r)
    if s['breaks']:
        fail('dmg3 breaks=%d at 15 m/s, 0 expected' % s['breaks'])
    print('crash15: dents only, dmg3 %s' % s)
