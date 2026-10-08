# not upstream: Coll.Smoke.HeadOn, two ShuttlePBs closing at 2 m/s: one impact line per hit, PB-A and PB-B named, the response written, the pair separating
import re
from scnlib import fail, summary

IMPACT = re.compile(r"^Collision impact t=(\S+) '([^']*)' '([^']*)' vn=(\S+) m/s vsep=(\S+) m/s E=(\S+) J")


def check(ctx):
    r = ctx.first()
    hits = [IMPACT.match(l) for l in r.log if l.startswith('Collision impact ')]
    if not hits or not all(hits):
        fail('no well-formed "Collision impact" line')
    t, a, b, vn, vsep, e = hits[0].groups()
    if {a, b} != {'PB-A', 'PB-B'}:
        fail('first impact names %r and %r, PB-A and PB-B expected' % (a, b))
    if not 1.0 < float(vn) < 3.0:
        fail('first impact approach %s m/s, about 2 expected' % vn)
    if float(e) <= 0:
        fail('first impact energy %s J, positive expected' % e)
    s = summary(r)
    if not s or int(s.get('writes', '0')) == 0:
        fail('summary writes=0: no response was written')
