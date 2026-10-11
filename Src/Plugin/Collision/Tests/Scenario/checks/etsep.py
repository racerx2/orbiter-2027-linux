# not upstream: Coll.Atlantis.ETSep, the ascent AP separates the ET with collisions on; the tank leaves, no impact between orbiter and tank (design-etsep)
import math
from scnlib import fail
from dmg3lib import no_error


def check(ctx):
    r = ctx.first()
    no_error(r)
    d = r.dump
    ks = [k for k in sorted(d.frames) if 'STS-101' in d.frames[k].get('V', {}) and 'STS-101_ET' in d.frames[k].get('V', {})]
    if len(ks) < 3:
        fail('orbiter or tank missing from the dump')
    dist = {k: math.dist(d.vec(k, 'STS-101', 'p'), d.vec(k, 'STS-101_ET', 'p')) for k in ks}
    ksep = next((k for k in ks if abs(dist[k] - dist[ks[0]]) > 0.01), None)  # docked: rigid, the distance holds
    if ksep is None:
        fail('the tank never left (distance %.3f m throughout)' % dist[ks[0]])
    kend = next((k for k in ks if (k - ksep) * ctx.args.step >= 20.0), None)
    if kend is None:
        fail('less than 20 s of run after the separation')
    gain = dist[kend] - dist[ksep]
    hits = [l for l in r.log if l.startswith('Collision impact ') and "'STS-101'" in l and "'STS-101_ET'" in l]
    if hits:
        fail('impact between orbiter and tank: %s' % hits[0][:160])
    if not any('split scope start' in l for l in r.log):
        fail('no split scope at the separation')
    ends = [l for l in r.log if 'split scope end' in l]
    if not ends or 'clear' not in ends[0]:
        fail('split scope did not end clear: %s' % (ends[0][:160] if ends else 'no end line'))
    if gain < 1.0:  # 0.2 m/s along the port axis is about 0.1 m/s of centre distance here (2.3 m); stuck: 0.2 m
        fail('orbiter-tank centre distance grew %.2f m in the 20 s after the separation' % gain)
    print('%s: separated at frame %d, +%.2f m in 20 s' % (ctx.args.name, ksep, gain))
