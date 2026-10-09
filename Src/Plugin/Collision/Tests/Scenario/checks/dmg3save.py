# not upstream: Coll.Dmg3.SaveLoad, the 70 m/s crash saved and reloaded: torn rows and debris rows saved, debris rebuilt with the same fnv (dmg3 P)
import re
from scnlib import fail
from dmg3lib import DEBRIS, RESTORED, broke, debris_vessels, matches, no_error

SAVE = ('Scenarios', 'Tests', 'Coll', 'Saved', 'dmg3.scn')


def check(ctx):
    crash, load = ctx.runs['crash'], ctx.runs['load']
    broke(crash)
    text = open(crash.path(*SAVE), encoding='latin-1').read()
    if 'XDMGM T ' not in text:
        fail('crash: the saved scenario has no torn row (XDMGM T)')
    alive = sorted(re.findall(r'(?m)^\s*([^:\s]+):CollDebris\s*$', text))
    if not alive:
        fail('crash: no CollDebris vessel in the saved scenario')
    no_error(load)
    if not any(l.startswith('Collision damage loaded: ') for l in load.log):
        fail('load: no "Collision damage loaded" line')
    made = {m.group(1): m.group(5) for m in matches(crash, DEBRIS)}
    back = {m.group(1): m.group(2) for m in matches(load, RESTORED)}
    for n in alive:
        if n not in back:
            fail('load: debris %s not restored' % n)
        if n in made and made[n] != back[n]:
            fail('load: debris %s fnv=%s, saved as fnv=%s' % (n, back[n], made[n]))
    if sorted(debris_vessels(load, min(load.dump.frames))) != alive:
        fail('load: debris %s in the first frame, %s saved' % (debris_vessels(load, min(load.dump.frames)), alive))
    if any(l.startswith(('Collision: debris row ', "Collision: debris '")) for l in load.log):
        fail('load: %s' % next(l for l in load.log if l.startswith(('Collision: debris row ', "Collision: debris '"))))
    print('saveload: %d debris restored with their fnv: %s' % (len(alive), back))
