# not upstream: Scn.NormalClose, the addon on the normal close path with a damaged-vessel block and a running recording (E4 7.10, 11.1)
import re
from teardown import CLOSE, order
from scnlib import fail, read_text

BLOCK = ['COLLA 1', 'VESSEL 0 PB-A ShuttlePB', 'XDMG 1 500000 0', 'END_VESSEL']


def check(ctx):
    r = ctx.first()
    order(r, ['Collision: session 1 created (load)', 'Collision: scenario block adopted, lines=4', 'Collision: active ',
              'Collision summary: ', 'Collision perf: ', 'Collision: session 1 ended (end)', CLOSE])
    end = max(i for i, l in enumerate(r.log) if l.startswith('Collision: session 1 ended (end)'))
    if any(l.startswith('Collision write') for l in r.log[end:]):
        fail('a Collision write line after the session ended')
    text = read_text(r.path('Scenarios', '(Current state).scn'))
    m = re.search(r'(?ms)^BEGIN_Collision\n(.*?)^END$', text)
    if not m or [l.strip() for l in m.group(1).splitlines() if l.strip()] != BLOCK:
        fail('the exit save does not hold the addon block %s' % BLOCK)
    stem = re.sub(r'[^A-Za-z0-9]+', '_', ctx.args.name).strip('_')
    pos = read_text(r.path('Flights', stem, 'PB-A.pos'))
    if not pos.endswith('\n') or len(pos.splitlines()) < 5:
        fail('the recording of PB-A is missing or ends without a complete line')
