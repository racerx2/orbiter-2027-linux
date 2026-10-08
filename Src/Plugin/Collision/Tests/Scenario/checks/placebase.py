# not upstream: Scn.PlaceBase, TESTPLACEBASE at pad 2's base-local position matches a ShuttlePB landed on pad 2 (design-C-T T0.7)
import math
from scnlib import dot, fail, norm3, rot, scale, sub

K = 3  # placed in pre-step 3: the dump of frame 3 holds the placed state


def check(ctx):
    d = ctx.first().dump
    moon = tuple(float(x) for x in d.frames[K]['B']['Moon']['p'].split(','))
    pl, pb = d.vec(K, 'PL', 'p'), d.vec(K, 'PB', 'p')
    up = scale(sub(pb, moon), 1 / norm3(sub(pb, moon)))
    rel = sub(pl, pb)
    horiz = norm3(sub(rel, scale(up, dot(rel, up))))
    if horiz > 0.1:
        fail('PL is %.3f m from pad 2 horizontally' % horiz)
    placed = [l for l in ctx.first().log if l.startswith('CollTestVessel placed \'PL\' at \'Brighton Beach\'')]
    if not placed:
        fail('no placed line')
    rad = float(placed[0].split('rad=')[1].split()[0])  # Moon radius + terrain elevation + 3
    h = norm3(sub(pl, moon)) - rad
    if abs(h) > 0.01:
        fail('PL is %.4f m off its commanded radius (3 m above the terrain)' % h)
    dv = norm3(sub(d.vec(K, 'PL', 'v'), d.vec(K, 'PB', 'v')))
    if dv > 1e-3:
        fail('PL moves at %.4g m/s against the landed PB: not the ground velocity' % dv)
    a, b = rot(d, K, 'PL'), rot(d, K, 'PB')
    err = max(abs(a[i][j] - b[i][j]) for i in range(3) for j in range(3))
    if err > 2e-3:
        fail('PL at heading 90 differs in attitude from PB at heading 90 by %.3g' % err)
