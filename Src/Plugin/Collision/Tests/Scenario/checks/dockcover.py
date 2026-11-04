# not upstream: Coll.Dock.Cover, two DeltaGliders nose to nose: closed nose cones collide and never dock, open ones dock (design dock-cover)
from scnlib import fail


def docked(d, k):
    return d.frames[k]['V']['PB-A']['dk'].endswith('PB-B')


def check(ctx):
    shut, opened = ctx.runs['closed'], ctx.runs['open']
    ks = sorted(shut.dump.frames)
    k = next((k for k in ks if docked(shut.dump, k)), None)
    if k is not None:
        fail('closed nose cones docked at frame %d' % k)
    if not any(l.startswith('Collision impact ') and "'PB-A'" in l and "'PB-B'" in l for l in shut.log):
        fail('closed nose cones never touched')
    if not any("'PB-A' port 0 is covered" in l for l in shut.log):
        fail('no covered port logged')
    ko = sorted(opened.dump.frames)
    if not docked(opened.dump, ko[-1]):
        fail('open nose cones did not dock by frame %d' % ko[-1])
    if any(' is covered' in l for l in opened.log):
        fail('an open nose cone was taken as covered')
