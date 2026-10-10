# not upstream: Pbr.Visual.Bench, the PBR material path of VulkanClient (design-pbr 9): bench shots with and without PBR, legacy scenes against the pre-PBR baseline
import os
from scnlib import fail, png_decode, read_text
from visualsmoke import BASELINE, vvl_ids

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), *(['..'] * 6)))
BASEDIR = os.path.join(ROOT, 'Tests', 'Pbr', 'baseline')
XS, YS, RAD = (391, 557, 722, 888), (111, 277, 442, 608), 45
PRECOMPILE = 'PBR precompile: 32 variants, 0 failed'
SMACK = ((400, 240, 770, 580), (800, 0, 1020, 140))  # the two gliders in the baseline frame
DGPAIR = ((350, 230, 930, 450),)


def shot(r, rid):
    p = r.path('Images', 'shot.png')
    if not os.path.isfile(p) or os.path.getmtime(p) < r.start:
        fail('run %s: no fresh Images/shot.png' % rid)
    w, h, ch, pix = png_decode(p)
    if (w, h) != (1280, 720):
        fail('run %s: image %dx%d, 1280x720 expected' % (rid, w, h))
    bad = {k: n for k, n in vvl_ids(r).items() if n > BASELINE.get(k, 0)}
    if bad:
        fail('run %s: validation messages beyond the baseline: %s' % (rid, bad))
    return w, ch, pix


def disk(img, k, rad=RAD):
    w, ch, pix = img
    cx, cy = XS[k % 4], YS[k // 4]
    out = []
    for y in range(cy - rad, cy + rad + 1):
        for x in range(cx - rad, cx + rad + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= rad * rad:
                i = (y * w + x) * ch
                out.append(tuple(pix[i:i + 3]))
    return out


def mean(px):
    return [sum(p[c] for p in px) / len(px) for c in range(3)]


def changed(a, b, tol=24):
    return sum(1 for p, q in zip(a, b) if max(abs(p[c] - q[c]) for c in range(3)) > tol)


def whole(img):
    w, ch, pix = img
    return [tuple(pix[i:i + 3]) for i in range(0, len(pix), ch)]


def box(img, bx):
    w, ch, pix = img
    x0, y0, x1, y1 = bx
    return [tuple(pix[(y * w + x) * ch:(y * w + x) * ch + 3]) for y in range(y0, y1) for x in range(x0, x1)]


def base(name):
    w, h, ch, pix = png_decode(os.path.join(BASEDIR, name))
    return w, ch, pix


def check(ctx):
    img = {rid: shot(ctx.runs[rid], rid) for rid in ('pbr', 'legacy', 'smack', 'dg')}
    if PRECOMPILE not in read_text(ctx.runs['pbr'].path('Orbiter.log')):
        fail('run pbr: no "%s" in Orbiter.log' % PRECOMPILE)
    notes = []
    for k in list(range(14)) + [15]:
        p, l = disk(img['pbr'], k), disk(img['legacy'], k)
        mp = mean(p)
        d = 100.0 * changed(p, l) / len(p)
        if d < 20:
            fail('slot %d: the PBR shot differs from the legacy shot in only %.1f %% of the disk' % (k, d))
        if sum(mp) / 3 < 25:
            fail('slot %d: the PBR sphere is nearly black (mean %s)' % (k, mp))
        notes.append('%d:%.0f%%' % (k, d))
    n = changed(disk(img['pbr'], 14), disk(img['legacy'], 14))
    if n > 0.01 * len(disk(img['pbr'], 14)):
        fail('slot 14 (legacy material) differs between the PBR and legacy shots in %d pixels' % n)
    glow = sum(1 for r, g, b in disk(img['pbr'], 6) if g > 230 and b > 230 and r < 150)
    if glow < 0.02 * len(disk(img['pbr'], 6)):
        fail('slot 6: %d emissive pixels, the grid does not glow' % glow)
    ref = base('bench_legacy.png')
    for k in range(15):
        a, b = disk(img['legacy'], k, 60), disk(ref, k, 60)
        n = changed(a, b)
        if n > 0.005 * len(a):
            fail('slot %d with PBR disabled differs from the pre-PBR baseline in %d pixels' % (k, n))
    for rid, name, boxes in (('smack', 'smack.png', SMACK), ('dg', 'dg_pair.png', DGPAIR)):
        ref = base(name)
        a, b = whole(img[rid]), whole(ref)
        n = changed(a, b)
        if n > 0.03 * len(a):  # the planet's tiles load at their own pace
            fail('run %s differs from the pre-PBR baseline %s in %d pixels' % (rid, name, n))
        for bx in boxes:
            a, b = box(img[rid], bx), box(ref, bx)
            m = changed(a, b)
            if m > 0.005 * len(a):
                fail('run %s: the vessel in %s differs from the pre-PBR baseline in %d pixels' % (rid, bx, m))
        notes.append('%s:%d' % (rid, n))
    print('pbrbench: slot differences %s' % ' '.join(notes))
