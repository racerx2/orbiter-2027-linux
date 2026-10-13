# not upstream: Coll.Bldg.Roof, a DG set down on a synthetic 40 x 30 m flat-roof streamed building rests there: its radius steady within 0.1 m over 20 s (design bldg-collision 7)
from scnlib import fail, norm3, sub
from dmg3lib import no_error

SPAN, TOL, DROP = 20.0, 0.1, 2.0  # s of rest checked, m it may move, m below the placement it may end (the roof is 10 m up)


def check(ctx):
    r = ctx.first()
    no_error(r)
    if not any(l.startswith("CollTestVessel placed 'DG' at 'Cape Canaveral'") for l in r.log):
        fail('DG not placed at Cape Canaveral')
    if not any(l.startswith('Collision bldg: Earth z14 ') and ' loaded in ' in l for l in r.log):
        fail('no "Collision bldg: ... loaded in" line: the streamed building tile was not loaded')
    d = r.dump
    ks = sorted(d.frames)
    alt = {k: norm3(sub(d.vec(k, 'DG', 'p'), d.vec(k, 'Earth', 'p', 'B'))) for k in ks if 'DG' in d.frames[k]['V'] and 'Earth' in d.frames[k]['B']}
    placed = [k for k in ks if k > 1 and k in alt]
    if not placed:
        fail('no dump frame after the placement')
    t1 = d.frames[ks[-1]]['simt']
    if t1 - d.frames[placed[0]]['simt'] < SPAN + 2.0:
        fail('run of %.1f s: %g s of rest cannot be checked' % (t1, SPAN))
    rest = [alt[k] for k in placed if d.frames[k]['simt'] >= t1 - SPAN]
    span = max(rest) - min(rest)
    if span > TOL:
        fail('DG radius moves %.3f m over the last %g s (at most %g)' % (span, SPAN, TOL))
    if rest[-1] < alt[placed[0]] - DROP:
        fail('DG ends %.2f m below where it was placed: not on the roof' % (alt[placed[0]] - rest[-1]))
    print('%s: DG at rest on the roof %.3f m below its placement, radius span %.4f m over %g s' % (ctx.args.name, alt[placed[0]] - rest[-1], span, SPAN))
