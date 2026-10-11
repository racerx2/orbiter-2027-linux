# not upstream: Gltf.Visual.NestedGlass (claude/design-pbr-nested-glass.md 7): a green transmission HUD inside a tinted transmission canopy shows through it
from scnlib import camera_project, fail, read_text
from pbrbench import box, shot

LOADED = 'glTF mesh GltfTest\\nestedglass.glb: 4 groups, 3 materials, 0 textures'
CAM = (4.0, 20.0, 10.0, 1.0, 40)  # gen:pair camera POS r phi theta, Size 1, FOV 40
HUD, BACK, REF = (0.0, 0.0, 0.0), (0.0, 0.30, 0.3), (0.0, 0.87, 0.0)


def mean_at(img, pt, h=3):
    x, y = (int(round(v)) for v in camera_project(pt, *CAM))
    px = box(img, (x - h, y - h, x + h + 1, y + h + 1))
    return tuple(sum(p[c] for p in px) / len(px) for c in range(3)), (x, y)


def check(ctx):
    r = ctx.runs['glass']
    img = shot(r, 'glass')
    text = read_text(r.path('Orbiter.log'))
    if LOADED not in text:
        fail('no "%s" in Orbiter.log' % LOADED)
    bad = [l for l in text.splitlines() if 'glTF' in l and LOADED not in l]
    if bad:
        fail('glTF warnings or errors: %s' % bad[:3])
    hud, ph = mean_at(img, HUD)
    back, pb = mean_at(img, BACK)
    ref, pr = mean_at(img, REF)
    green = lambda c: c[1] - max(c[0], c[2])
    lum = lambda c: 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
    print('nestedglass: HUD %s at %s, backdrop %s at %s, reference %s at %s' % (
        tuple(round(v) for v in hud), ph, tuple(round(v) for v in back), pb, tuple(round(v) for v in ref), pr))
    if green(hud) < 40:
        fail('the HUD does not show through the canopy: %s at %s' % (tuple(round(v) for v in hud), ph))
    if green(back) >= 20:
        fail('the backdrop through the canopy is green: %s at %s' % (tuple(round(v) for v in back), pb))
    if lum(back) >= 0.8 * lum(ref):
        fail('the canopy tint is missing: backdrop %.0f, reference %.0f' % (lum(back), lum(ref)))
