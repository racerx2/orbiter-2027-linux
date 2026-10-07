# not upstream: Scn.Visual.Smoke, the visual pipeline: a PNG at frame 60, twice, stable, and the validation layer's message count (design-C-T 5, T0.6)
import os
import re
from scnlib import fail, png_decode, read_text

BASELINE = {'VUID-vkCmdDraw-None-09600': 2}  # T0.6 on lavapipe: the client's back-buffer read for a screenshot leaves an image in the wrong layout; allowed list (T 5.5)


def vvl_ids(r):
    text = read_text(r.path('vvl.txt')) + read_text(r.path('stdout.txt')) + read_text(r.path('stderr.txt'))
    ids = {}
    for m in re.finditer(r'(VUID-[A-Za-z0-9_-]+)|Validation Error', text):
        k = m.group(1) or 'Validation Error'
        ids[k] = ids.get(k, 0) + 1
    return ids


def vvl_count(r):
    return sum(vvl_ids(r).values())


def check(ctx):
    img = {}
    for rid in ('a', 'b'):
        r = ctx.runs[rid]
        p = r.path('Images', 'smoke.png')
        if not os.path.isfile(p) or os.path.getmtime(p) < r.start:
            fail('run %s: no fresh Images/smoke.png' % rid)
        w, h, ch, pix = png_decode(p)
        if (w, h) != (1280, 720):
            fail('run %s: image %dx%d, 1280x720 expected' % (rid, w, h))
        lo, hi = [min(pix[c::ch]) for c in range(min(ch, 3))], [max(pix[c::ch]) for c in range(min(ch, 3))]
        if max(b - a for a, b in zip(lo, hi)) < 32:
            fail('run %s: the image is nearly uniform (channel ranges %s..%s)' % (rid, lo, hi))
        img[rid] = (w, h, ch, pix)
        bad = {k: n for k, n in vvl_ids(r).items() if n > BASELINE.get(k, 0)}
        if bad:
            fail('run %s: validation messages beyond the baseline: %s' % (rid, bad))
    (w, h, ch, a), b = img['a'], img['b'][3]
    changed = sum(1 for i in range(0, w * h * ch, ch) if max(abs(a[i + c] - b[i + c]) for c in range(min(ch, 3))) > 24)
    if changed > 0.001 * w * h:
        fail('the two images differ in %d pixels, more than 0.1 %%' % changed)
    print('visualsmoke: %d of %d pixels differ; validation messages %d, %d' % (changed, w * h, vvl_count(ctx.runs['a']), vvl_count(ctx.runs['b'])))
