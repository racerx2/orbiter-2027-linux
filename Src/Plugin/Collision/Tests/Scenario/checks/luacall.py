# not upstream: Scn.LuaCall, LUACALL <k> GL Retro 0 closes a DeltaGlider's open retro covers (E4 7.4, T0.7)
import re
from scnlib import fail, read_text


def rcover(r):  # (state, speed) of GL's retro covers in the exit save; no line: closed (AnimState2 saves a non-zero state only)
    text = read_text(r.path('Scenarios', '(Current state).scn'))
    block = re.search(r'(?ms)^GL:DeltaGlider\s.*?^END$', text)
    m = re.search(r'(?m)^\s*RCOVER (\S+) (\S+)', block.group(0)) if block else None
    return (float(m.group(1)), float(m.group(2))) if m else (0.0, 0.0)


def check(ctx):
    call, ref = ctx.runs['call'], ctx.runs['ref']
    if not any(l.startswith('CollTestHarness act k=5 ') and l.endswith('LUACALL GL Retro') for l in call.log):
        fail('no LUACALL act line at k=5')
    if rcover(ref)[0] != 1.0:
        fail('ref: retro covers %r, open expected' % (rcover(ref),))
    if rcover(call)[0] != 0.0:
        fail('call: retro covers %r after LUACALL GL Retro 0, closed expected' % (rcover(call),))
