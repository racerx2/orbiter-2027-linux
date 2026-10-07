# not upstream: Scn.RecTwice, the same recording run twice in its fixed folder records both times: the runner cleared Flights/ and Playback/ (E4 7.5)
from recplay import recorded, stem
from scnlib import fail


def check(ctx):
    runs = [r for r in ctx.history if r.spec.id == 'rec']
    if len(runs) != 2:
        fail('%d executions of run rec, 2 expected' % len(runs))
    for r in runs:
        recorded(r, stem(ctx))
    if runs[1].cleared < 2:
        fail('the second execution cleared %d entries, the first one\'s record and playback scenario expected' % runs[1].cleared)
