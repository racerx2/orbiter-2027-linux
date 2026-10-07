# not upstream: Scn.ExitCode, the runner sees the exit code of oapi.exit (design-C-T 3.5)
from scnlib import fail


def check(ctx):
    r = ctx.first()
    if r.exit != 3:
        fail('exit %s, expected 3' % r.exit)
