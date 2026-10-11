# not upstream: Gltf.Visual.DGTest, the glTF test DG (claude/design-gltf-loader.md 6): loads clean, lands in its scenario, and matches the .msh DG in the same pose
import re
from scnlib import fail, read_text
from pbrbench import box, shot
from visualsmoke import vvl_ids

LOADED = 'glTF mesh DGTest\\dgTest.glb: 122 groups, 27 materials, 10 textures'
BOX = (350, 230, 930, 450)  # PB-A in the gen:pair frame


def log_ok(r, rid):
    text = read_text(r.path('Orbiter.log'))
    if LOADED not in text:
        fail('run %s: no "%s" in Orbiter.log' % (rid, LOADED))
    bad = [l for l in text.splitlines() if ('glTF' in l and LOADED not in l) or '@gltf:' in l]
    if bad:
        fail('run %s: glTF warnings or errors: %s' % (rid, bad[:3]))


def check(ctx):
    img = {rid: shot(ctx.runs[rid], rid) for rid in ('scn', 'gltf', 'msh')}
    log_ok(ctx.runs['scn'], 'scn')
    log_ok(ctx.runs['gltf'], 'gltf')
    w, ch, pix = img['scn']
    lo = [min(pix[c::ch]) for c in range(3)]
    hi = [max(pix[c::ch]) for c in range(3)]
    if max(b - a for a, b in zip(lo, hi)) < 32:
        fail('run scn: the image is nearly uniform')
    a, b = box(img['gltf'], BOX), box(img['msh'], BOX)
    diff = [max(abs(p[c] - q[c]) for c in range(3)) for p, q in zip(a, b)]
    mean = sum(diff) / len(diff)
    over = 100.0 * sum(1 for d in diff if d > 48) / len(diff)
    if mean > 30 or over > 25:  # a mirrored or turned model gives about 100 and 80 %
        fail('the glTF DG does not match the .msh DG: mean difference %.1f, %.1f %% of pixels over 48' % (mean, over))
    print('gltfdg: glTF vs .msh DG mean difference %.1f, %.1f %% over 48; validation messages %s' % (mean, over, {k: sum(vvl_ids(ctx.runs[k]).values()) for k in img}))
