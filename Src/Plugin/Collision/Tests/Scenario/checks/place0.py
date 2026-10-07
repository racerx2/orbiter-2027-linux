# not upstream: Scn.Place0, a pre-step placement is exact in the dump of the same frame (design-C-T 4.7, E4 T0.7)
import math
from scnlib import axis, fail, norm3, rot, sub

K = 5


def check(ctx):
    r = ctx.first()
    d = r.dump
    if not any(l.startswith('CollTestVessel act k=%d ' % K) and 'TESTPLACEREL' in l for l in r.log):
        fail('no TESTPLACEREL act line at k=%d' % K)
    gap = sub(sub(d.vec(K, 'PB-B', 'p'), d.vec(K, 'PB-A', 'p')), tuple(6 * x for x in axis(d, K, 'PB-A', 2)))
    ulp = math.ulp(max(abs(x) for x in d.vec(K, 'PB-A', 'p')))  # global positions sit on this grid (about 3e-5 m at 1 AU, T 4.4)
    if norm3(gap) > 4 * ulp:
        fail('|p_B - p_A - 6 z_A| = %.3g m at frame %d, more than 4 ulp (%.3g m)' % (norm3(gap), K, 4 * ulp))
    if norm3(sub(d.vec(K - 1, 'PB-B', 'p'), d.vec(K - 1, 'PB-A', 'p'))) < 100:
        fail('PB-B was already near PB-A before frame %d' % K)
    RA, RB = rot(d, K, 'PB-A'), rot(d, K, 'PB-B')
    want = [[RA[i][0] * -1, RA[i][1], RA[i][2] * -1] for i in range(3)]  # R_A R(AROT 0 180 0) = R_A diag(-1, 1, -1)
    err = max(abs(want[i][j] - RB[i][j]) for i in range(3) for j in range(3))
    if err > 1e-12:
        fail('R_B differs from R_A R(AROT 0 180 0) by %.3g' % err)
    dv = norm3(sub(d.vec(K, 'PB-B', 'v'), d.vec(K, 'PB-A', 'v')))
    if dv > 1e-9:
        fail('v_B - v_A = %.3g m/s after the placement' % dv)
