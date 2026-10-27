# not upstream: Scn.PlaceBase, TESTPLACEBASE at pad 2's base-local position matches a ShuttlePB landed on pad 2 (design-C-T T0.7)
import math

from scnlib import Skip, axis, dot, fail, norm3, scale, sub

K = 3  # placed in pre-step 3: the dump of frame 3 holds the placed state
PB_TD = 1.5  # ShuttlePB CG over its touchdown plane (ShuttlePB.cpp tdvtx y = -1.5), its skids on the ground at rest (design terrain-contact 1)
Y = 3.0  # TESTPLACEBASE y: metres above the local terrain


def placed_line(log):
    placed = [l for l in log if l.startswith('CollTestVessel placed \'PL\' at \'Brighton Beach\'')]
    if not placed:
        fail('no placed line')
    f = dict(x.split('=', 1) for x in placed[0].split() if '=' in x)
    try:
        return float(f['base']), float(f['elev'])
    except (KeyError, ValueError):
        fail('placed line without base= and elev=: %s' % placed[0])


def check(ctx):
    d = ctx.first().dump
    moon = tuple(float(x) for x in d.frames[K]['B']['Moon']['p'].split(','))
    pl, pb = d.vec(K, 'PL', 'p'), d.vec(K, 'PB', 'p')
    up = scale(sub(pb, moon), 1 / norm3(sub(pb, moon)))
    rel = sub(pl, pb)
    horiz = norm3(sub(rel, scale(up, dot(rel, up))))
    if horiz > 0.1:
        fail('PL is %.3f m from pad 2 horizontally' % horiz)
    rpl, rpb = norm3(sub(pl, moon)), norm3(sub(pb, moon))
    dh = (rpl - rpb) - (Y - PB_TD)  # the core put PB on the terrain: independent of TESTPLACEBASE's own numbers
    if abs(dh) > 0.05:
        fail('PL is %.4f m off 3 m above the terrain under the landed PB' % dh)
    base, elev = placed_line(ctx.first().log)
    h = rpl - (base + elev + Y)  # the radius from the logged base radius and terrain elevation, not from the logged rad=
    if abs(h) > 0.01:
        fail('PL is %.4f m off base radius + elevation %.3f m + 3' % (h, elev))
    dv = norm3(sub(d.vec(K, 'PL', 'v'), d.vec(K, 'PB', 'v')))
    if dv > 1e-3:
        fail('PL moves at %.4g m/s against the landed PB: not the ground velocity' % dv)
    def flat(v):  # a body axis in the horizontal plane at the pad
        h = sub(v, scale(up, dot(v, up)))
        return scale(h, 1 / norm3(h))
    fpl, fpb = flat(axis(d, K, 'PL', 2)), flat(axis(d, K, 'PB', 2))
    err = math.acos(max(-1.0, min(1.0, dot(fpl, fpb))))
    if err > 2e-3:
        fail('PL at heading 90 differs in heading from PB at heading 90 by %.3g rad' % err)
    tpl = math.acos(max(-1.0, min(1.0, dot(axis(d, K, 'PL', 1), up))))
    if tpl > 2e-3:
        fail('PL is tilted %.3g rad: TESTPLACEBASE places level' % tpl)
    tpb = math.acos(max(-1.0, min(1.0, dot(axis(d, K, 'PB', 1), up))))
    if tpb > 0.05:  # the PB stands on the terrain under its skids (design terrain-contact 2): tilted by that ground's slope only
        fail('PB is tilted %.3g rad on pad 2' % tpb)
    if abs(elev) < 1.0:  # without elevation tiles a placement that ignores the terrain looks the same
        raise Skip('Moon terrain elevation at Brighton Beach is %.3f m: the elevation term is untested (install the Moon elevation tiles)' % elev)
