# not upstream: Scn.Timeout, a run without a frame limit is killed at its timeout with its whole process group (design-C-T 2.5, T0.9)
import os
from scnlib import fail


def check(ctx):
    r = ctx.first()
    if r.status != 'TIMEOUT' or r.secs > ctx.args.timeout + 5:
        fail('status %s after %.1f s, TIMEOUT within %d s expected' % (r.status, r.secs, ctx.args.timeout + 5))
    left = []
    for d in os.listdir('/proc'):
        if d.isdigit():
            try:
                cwd = os.readlink('/proc/%s/cwd' % d)
            except OSError:
                continue
            if cwd == r.dir or cwd.startswith(r.dir + '/'):
                left.append(d)
    if left:
        fail('processes left in the run folder: %s' % ' '.join(left))
