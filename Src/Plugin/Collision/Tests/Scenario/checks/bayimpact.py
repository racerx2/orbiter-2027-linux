from scnlib import fail, summary


def check(ctx):
    off, on = ctx.runs['off'], ctx.runs['on']
    s = summary(on)
    if not s or int(s.get('contacts', 0)) < 1 or int(s.get('writes', 0)) < 1:
        fail('bay impact did not produce contacts and response writes')
    hits = [line for line in on.log if line.startswith('Collision impact ')]
    if not hits or not any("'PB-A'" in h and "'PB-B'" in h for h in hits):
        fail('no Atlantis/payload impact')
    if on.dump.body_lines() == off.dump.body_lines():
        fail('collision enabled did not change the payload trajectory')
