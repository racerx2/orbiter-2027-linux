# not upstream: Coll.Stack.Hit and Coll.Stack.Attached, a vessel hits a docked stack or an attachment tree: P against the off run, L about the moving CG kept (review CA-F 2, 3)
from scnlib import fail, free_bodies, momentum, norm3, scale, sub

TOL = {'stack': (1e-6, 1e-3), 'attached': (1e-6, 1e-3)}


def cg(d, k, names):
    ms = [float(d.frames[k]['V'][n]['m']) for n in names]
    M = sum(ms)
    O = scale(tuple(map(sum, zip(*[scale(d.vec(k, n, 'p'), m) for n, m in zip(names, ms)]))), 1 / M)
    vc = scale(tuple(map(sum, zip(*[scale(d.vec(k, n, 'v'), m) for n, m in zip(names, ms)]))), 1 / M)
    return O, vc


def check(ctx):
    off, on = ctx.runs['off'], ctx.runs['on']
    kind = 'attached' if 'PL' in on.dump.frames[1]['V'] else 'stack'
    ptol, ltol = TOL[kind]
    d, do = on.dump, off.dump
    ks = sorted(d.frames)
    if kind == 'stack' and not d.frames[5]['V']['PB-A']['dk'].endswith('PB-B'):
        fail('PB-B not docked to PB-A at frame 5')
    if kind == 'attached' and d.frames[5]['V']['PL']['par'] != 'PB-A':
        fail('PL not attached to PB-A at frame 5')
    hits = [l for l in on.log if l.startswith('Collision impact ') and "'TC'" in l]
    if not hits:
        fail('no impact of TC')
    k0 = next((k for k in ks if d.frames[k]['lines'][1:] != do.frames[k]['lines'][1:]), None)
    if k0 is None:
        fail('the on run never differs from the off run: no response written')
    k0 -= 1
    if k0 < 5:
        fail('first write in frame %d, before TC was placed in frame 3' % (k0 + 1))
    k1 = ks[-1]
    names = free_bodies(d, k0)
    for k in (k0, k1):
        if free_bodies(d, k) != names:
            fail('free bodies changed: %s at frame %d' % (free_bodies(d, k), k))
    O0, v0 = cg(d, k0, names)
    O1, v1 = cg(d, k1, names)
    _, L0, sP, sL0 = momentum(d, k0, names, O0, v0)
    _, L1, _, sL1 = momentum(d, k1, names, O1, v1)
    Pn, _, _, _ = momentum(d, k1, names, O1, v1)
    Pf, _, _, _ = momentum(do, k1, names, O1, v1)
    dP, dL, sL = norm3(sub(Pn, Pf)), norm3(sub(L1, L0)), max(sL0, sL1)
    print('%s: first write frame %d, |dP| %.3g (scale %.3g), |dL| %.3g (scale %.3g)' % (kind, k0 + 1, dP, sP, dL, sL))
    if dP > ptol * sP:
        fail('momentum against the off run: |dP| %.3g > %.3g' % (dP, ptol * sP))
    if dL > ltol * sL:
        fail('angular momentum about the CG from frame %d to %d: |dL| %.3g > %.3g' % (k0, k1, dL, ltol * sL))
