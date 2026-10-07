# not upstream: Coll.Landed.Wake, a vessel hits a LANDED one at 1 m/s: the landed one is woken (FREEFLIGHT, WOKE notice) and moves (E1 6.6, review CA-F 5)
from scnlib import add, fail, norm3, scale, sub

WOKE = 0x10  # COLLA_CON_WOKE


def rel(d, k, n):
    return sub(d.vec(k, n, 'p'), d.vec(k, 'Moon', 'p', 'B')), sub(d.vec(k, n, 'v'), d.vec(k, 'Moon', 'v', 'B'))


def check(ctx):
    r = ctx.first()
    d = r.dump
    k0, k1 = 1, max(d.frames)
    if d.frames[k0]['V']['LP']['fs'] != '1':
        fail('LP not LANDED at frame %d (fs=%s)' % (k0, d.frames[k0]['V']['LP']['fs']))
    if not any(l.startswith('Collision impact ') and "'LP'" in l and "'HT'" in l for l in r.log):
        fail('no impact of HT on LP')
    if d.frames[k1]['V']['LP']['fs'] != '0':
        fail('LP still LANDED after a 1 m/s hit (fs=%s): not woken' % d.frames[k1]['V']['LP']['fs'])
    def flags(n):
        return [int(l.split(' flags=')[1].split()[0], 16) for l in r.log if l.startswith("CollTestVessel '%s' msg=" % n) and ' kind=1 ' in l and ' flags=' in l]
    if not any(f & WOKE for f in flags('LP')):
        fail('no CONTACT notice to LP with COLLA_CON_WOKE')
    if not flags('HT') or any(f & WOKE for f in flags('HT')):
        fail('HT: no CONTACT notice, or one with COLLA_CON_WOKE (only the woken side)')
    (r0, u0), (r1, _) = rel(d, k0, 'LP'), rel(d, k1, 'LP')
    dt = d.frames[k1]['simt'] - d.frames[k0]['simt']
    disp = norm3(sub(r1, add(r0, scale(u0, dt))))
    if disp < 0.02:
        fail('LP moved %.4f m over the ground after the hit, at least 0.02 expected' % disp)
