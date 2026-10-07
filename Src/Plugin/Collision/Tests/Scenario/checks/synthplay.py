# not upstream: Scn.SynthPlay, a vessel played back from synthetic records follows them within 0.01 m (E4 7.5 step 6, T0.8)
import gen_scn
from scnlib import fail, norm3, sub


def check(ctx):
    d = ctx.first().dump
    worst = 0.0
    for k in sorted(d.frames):
        t = d.frames[k]['simt']
        earth = tuple(float(x) for x in d.frames[k]['B']['Earth']['p'].split(','))
        x, _ = gen_scn.orbit(t, gen_scn.R_P0, 1006.0 / gen_scn.R_P0)
        worst = max(worst, norm3(sub(sub(d.vec(k, 'PB-B', 'p'), earth), x)))
    if worst > 0.01:
        fail('PB-B is %.4g m off its synthetic record' % worst)
    print('synthplay: PB-B within %.3g m of its record' % worst)
