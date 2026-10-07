# not upstream: G1-G4 (addon off) and the G5 family (addon on, quiet) against the committed goldens (Design CA E4 7.6, design-C-T 4.5)
from scnlib import golden_compare, quiet


def check(ctx):
    name = ctx.args.name.rsplit('.', 1)[1]  # Coll.Off.Golden.Smack and G5.Smack share golden/Smack.dump.gz
    for rid in ctx.order:
        r = ctx.runs[rid]
        golden_compare(ctx, r, name)
        if r.spec.addon == 'on':
            quiet(r)
