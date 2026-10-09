# not upstream: helpers of the Coll.Dmg3.* checks, log lines of CollDamageA.cpp, CollBreakA.cpp, CollFxA.cpp and CollSession.cpp (dmg3)
import re
from scnlib import fail

DENT = re.compile(r"^Collision dent t=(\S+) '([^']*)' mesh=(\d+) grp=(\d+) .* mode=(\d+) ")
BREAK = re.compile(r"^Collision break '([^']*)' kind=(-?\d+) slot=(\d+) groups=(\S*) debris=(\S*) vn=(\S+)")
DEBRIS = re.compile(r"^Collision debris '([^']*)' from '([^']*)' slot=(\d+) mass=(\S+) fnv=([0-9a-f]{8})")
RESTORED = re.compile(r"^Collision break restored '([^']*)' fnv=([0-9a-f]{8})")
TEAR = re.compile(r"^Collision tear '([^']*)' slot=(\d+) d=(\S+) f=(\S+) R=(\S+) groups=(\S+) debris=(\S+) eSpec=(\S+) vn=(\S+)")
DMG3 = re.compile(r"^Collision dmg3: fx=(\d+) streams=(\d+) breaks=(\d+) reasserts=(\d+)")
PAIR = ('PB-A', 'PB-B')
DEBRIS_CLASS = 'CollDebris'


def matches(r, rx):
    return [m for m in (rx.match(l) for l in r.log) if m]


def dmg3(r):  # the last dmg3 summary line as a dict of ints
    m = matches(r, DMG3)
    if not m:
        fail('run %s: no "Collision dmg3:" line' % r.spec.id)
    return dict(zip(('fx', 'streams', 'breaks', 'reasserts'), (int(x) for x in m[-1].groups())))


def no_error(r):
    bad = [l for l in r.log if l.startswith('Collision: error in')]
    if bad:
        fail('run %s: %s' % (r.spec.id, bad[0]))
    if r.exit != 0:
        fail('run %s: exit %s, 0 expected' % (r.spec.id, r.exit))


def dents(r):  # vessel -> list of dent modes
    out = {}
    for m in matches(r, DENT):
        out.setdefault(m.group(2), []).append(int(m.group(5)))
    return out


def crush_each(r):
    d = dents(r)
    for v in PAIR:
        if v not in d:
            fail('run %s: no Collision dent line for %s' % (r.spec.id, v))
        if 1 not in d[v]:
            fail('run %s: %s dents have modes %s, a crush (mode=1) expected' % (r.spec.id, v, sorted(set(d[v]))))
    return d


def debris_vessels(r, k=None):  # names of CollDebris vessels in dump frame k (default: the last)
    if not r.dump or not r.dump.frames:
        fail('run %s: no dump' % r.spec.id)
    k = max(r.dump.frames) if k is None else k
    return sorted(n for n, f in r.dump.frames[k]['V'].items() if f.get('class') == DEBRIS_CLASS)


def first_debris_frame(r):
    for k in sorted(r.dump.frames):
        if any(f.get('class') == DEBRIS_CLASS for f in r.dump.frames[k]['V'].values()):
            return r.dump.frames[k]
    return None


def broke(r):  # the crash of 70 m/s: dents of mode 1, break lines, a debris vessel
    no_error(r)
    crush_each(r)
    brk, tr = matches(r, BREAK), tears(r)  # a section tear takes the small parts with it, so tear lines count as breaks
    if not brk and not tr:
        miss = [l for l in r.log if l.startswith('Collision: Config/Vessels/CollDebris.cfg missing')]
        fail('run %s: no "Collision break" or "Collision tear" line%s' % (r.spec.id, ' (%s)' % miss[0] if miss else ''))
    if not tr and not any(m.group(1) in PAIR and m.group(4) not in ('', '-') for m in brk):
        fail('run %s: no break line of PB-A or PB-B with groups' % r.spec.id)
    deb = matches(r, DEBRIS)
    if not deb:
        fail('run %s: no "Collision debris" line' % r.spec.id)
    s = dmg3(r)
    if s['breaks'] < 1 and not tr:
        fail('run %s: dmg3 breaks=%d, at least 1 expected' % (r.spec.id, s['breaks']))
    if first_debris_frame(r) is None:
        fail('run %s: no vessel of class %s in the dump' % (r.spec.id, DEBRIS_CLASS))
    return brk, deb, s


def tears(r):  # section tear lines of the pair
    return [m for m in matches(r, TEAR) if m.group(1) in PAIR]


def tear70(r):  # the 70 m/s crash tears a section: d in [5, 8] m, its debris at least 1000 kg
    t = tears(r)
    if not t:
        fail('run %s: no "Collision tear" line of PB-A or PB-B' % r.spec.id)
    ok = [m for m in t if 5.0 <= float(m.group(3)) <= 8.0]
    if not ok:
        fail('run %s: tear depth d=%s, [5, 8] m expected' % (r.spec.id, ','.join(m.group(3) for m in t)))
    mass = dict((m.group(1), float(m.group(4))) for m in matches(r, DEBRIS))
    heavy = [m for m in ok if m.group(7) != '-' and mass.get(m.group(7), 0.0) >= 1000.0]
    if not heavy:
        fail('run %s: no tear debris of 1000 kg or more (%s)' % (r.spec.id, ','.join('%s=%s' % (m.group(7), mass.get(m.group(7))) for m in ok)))
    return ok
