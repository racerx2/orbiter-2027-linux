from scnlib import fail, summary


def check(ctx):
    off, on = ctx.runs['off'], ctx.runs['on']
    for r in (off, on):
        d = r.dump
        if d.frames[5]['V']['PL']['par'] != 'PB-A':
            fail('payload was not initially attached')
        if d.frames[150]['V']['PL']['par'] == 'PB-A':
            fail('payload remained attached after detachment')
        if not any('TESTDETACH' in line for line in r.log):
            fail('no detachment action logged')
    s = summary(on)
    if not s or int(s.get('contacts', 0)) < 1 or int(s.get('writes', 0)) < 1:
        fail('no contact response before or after payload release')
    if on.dump.body_lines() == off.dump.body_lines():
        fail('collision enabled had no trajectory effect')
