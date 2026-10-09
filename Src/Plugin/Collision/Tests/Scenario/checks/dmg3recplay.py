# not upstream: Coll.Dmg3.RecPlay, the 70 m/s crash recorded and played back: the break at the recorded time, debris spawned in playback (dmg3 P)
from scnlib import fail
from dmg3lib import DEBRIS, broke, first_debris_frame, matches, no_error

TOL = 0.15  # s of MJD between the first debris frame of the recording and of the playback


def check(ctx):
    rec, play = ctx.runs['rec'], ctx.runs['play']
    broke(rec)
    no_error(play)
    ev = [l for l in play.log if l.startswith('Collision playback: link ') and ' events=' in l]
    if not ev:
        fail('play: no "Collision playback: link .. events=" line')
    if int(ev[0].split(' events=')[1].split()[0]) < 1:
        fail('play: %s, events expected' % ev[0])
    if not matches(play, DEBRIS):
        fail('play: no "Collision debris" line, no debris spawned in playback')
    fr, fp = first_debris_frame(rec), first_debris_frame(play)
    if fp is None:
        fail('play: no CollDebris vessel in the dump')
    dt = (fp['mjd'] - fr['mjd']) * 86400.0
    if abs(dt) > TOL:
        fail('play: first debris %.3f s off the recording, %.2f s allowed' % (dt, TOL))
    print('dmg3 recplay: debris in playback %.3f s from the recording' % dt)
