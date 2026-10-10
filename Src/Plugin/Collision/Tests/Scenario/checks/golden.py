# not upstream: G1-G4 (addon off) and the G5 family (addon on, quiet) against the committed goldens (Design CA E4 7.6, design-C-T 4.5)
from scnlib import golden_compare, quiet, same_dumps


def check(ctx):
    name = ctx.args.name.rsplit('.', 1)[1]  # Coll.Off.Golden.Smack and G5.Smack share golden/Smack.dump.gz
    runs = [ctx.runs[rid] for rid in ctx.order]
    on = [r for r in runs if r.spec.addon == 'on']
    off = [r for r in runs if r.spec.addon == 'off']
    for r in on:  # the machine-independent checks first: a golden skipped on this machine must not hide them
        quiet(r)
    if on and off:
        same_dumps(off[0].dump, on[0].dump, 'run %s vs run %s' % (off[0].spec.id, on[0].spec.id))
    if on:
        print('golden: quiet %s%s passed' % (','.join(r.spec.id for r in on), ', off vs on' if off else ''))
    for r in runs:
        golden_compare(ctx, r, name)
