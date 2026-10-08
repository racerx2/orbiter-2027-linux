# not upstream: Coll.HvC.Stock, headless and client runs give the same dumps and addon lines, client first and addon first (E4 7.7, T 4.5)
import re
from scnlib import fail, same_coll, same_dumps


def probe(r):  # rng lines and the MXCSR control bits; the sticky status flags may differ (the client divides by zero somewhere)
    return [re.sub(r' mxcsr=\S+', '', l) for l in r.log if l.startswith(('CollTestVessel rng ', 'CollTestVessel ready '))]


def check(ctx):
    h, cf, af = ctx.runs['h'], ctx.runs['cf'], ctx.runs['af']
    if not probe(h) or probe(cf) != probe(af):
        fail('the probe lines differ between the client orders')
    for c in (cf, af):
        same_dumps(h.dump, c.dump, 'h vs %s' % c.spec.id)
        same_coll(h, c, 'h vs %s' % c.spec.id, strip=(r'_us=[^ ]*', r'_ms=[^ ]*', r' render=\S+'))  # A1 render= is 0 headless, 2 with the client
    ready = [l for l in probe(h) if ' ready ' in l]
    if ready != [l for l in probe(cf) if ' ready ' in l]:
        fail('MXCSR control bits differ between headless and client')
    if probe(h) != probe(cf):  # documented, not a failure: the dumps above are what must agree
        print('hvc: rand() probe differs headless vs client (pre-existing: VulkanClient draws 32100 values in Scene::Scene, Scene.cpp:176-186, '
              'and one per Sketchpad flush, D3D9Pad.cpp:500): %s | %s' % ([l for l in probe(h) if ' rng ' in l][0], [l for l in probe(cf) if ' rng ' in l][0]))
    if not any(l.startswith('Collision: active ') and l.endswith('render=0') for l in h.log):
        fail('run h: A1 without render=0')
