# not upstream: dump and log parsers, compares and the generic run checks of the collision addon scenario tests (Design CA E4 7, design-C-T 4)
import gzip
import os
import re

PREFIX = re.compile(r'^\d+\.\d{3}: ')
A1 = re.compile(r'^Collision: active version=(\S+) session=(\d+) model=(-?\d+) response=(-?\d+) check=(-?\d+) damage=(-?\d+) log=(-?\d+) render=(-?\d+)$')
A3 = re.compile(r'^Collision summary: (.*)$')
A3P = re.compile(r'^Collision perf: (.*)$')


class CheckFail(Exception):  # exit 1, one short line
    pass


class Skip(Exception):  # exit 77 from a check (golden header mismatch, golden missing)
    pass


def fail(msg):
    raise CheckFail(msg)


def read_text(p):
    try:
        with open(p, 'rb') as f:
            return f.read().decode('latin-1')
    except OSError:
        return ''


def read_lines(p):  # stdin script: "<log text>|<command>" per line
    out = []
    for l in read_text(p).splitlines():
        if '|' in l and not l.startswith('#'):
            w, c = l.split('|', 1)
            out.append((w, c))
    return out


def log_lines(text):
    out = []
    for l in text.splitlines():
        out.append(PREFIX.sub('', l).rstrip('\r\n '))
    return out


def kv(s):
    return dict(x.split('=', 1) for x in s.split() if '=' in x)


class Dump:
    def __init__(self, text):
        self.lines = [l for l in text.splitlines() if l and not l.startswith('#')]
        self.header, self.end, self.frames, self.fail = {}, None, {}, None
        cur = None
        for l in self.lines:
            t = l.split(' ', 1)
            if t[0] == 'H':
                self.header = kv(t[1])
            elif t[0] == 'F':
                w = t[1].split()
                cur = {'k': int(w[0]), 'simt': float(w[1]), 'mjd': float(w[2]), 'warp': float(w[3]), 'V': {}, 'B': {}, 'lines': [l]}
                self.frames[cur['k']] = cur
            elif t[0] == 'V' and cur is not None:  # V <name> <class> key=value ...
                w = t[1].split(' ', 2)
                f = kv(w[2] if len(w) > 2 else '')
                f['class'] = w[1] if len(w) > 1 else '-'
                cur['V'][w[0]] = f
                cur['lines'].append(l)
            elif t[0] == 'B' and cur is not None:  # B <name> key=value ...
                w = t[1].split(' ', 1)
                cur['B'][w[0]] = kv(w[1] if len(w) > 1 else '')
                cur['lines'].append(l)
            elif t[0] == 'END':
                self.end = int(t[1])
            elif t[0] == 'FAIL':
                self.fail = l

    def vec(self, k, name, key, kind='V'):
        return tuple(float(x) for x in self.frames[k][kind][name][key].split(','))

    def body_lines(self):  # every line except the header: what bitwise compares look at
        return [l for l in self.lines if not l.startswith('H ')]


def load_dump(path):
    if path.endswith('.gz'):
        with gzip.open(path, 'rb') as f:
            return Dump(f.read().decode('latin-1'))
    return Dump(read_text(path))


class RunResult:
    def __init__(self, spec, d, rc, status, secs, pins, runid, cleared=0):
        self.spec, self.dir, self.exit, self.status, self.secs, self.pins, self.runid, self.cleared = spec, d, rc, status, secs, pins, runid, cleared
        self.log = log_lines(read_text(os.path.join(d, 'Orbiter.log')))
        dp = os.path.join(d, 'TestOut', 'state.dump')
        self.dump = Dump(read_text(dp)) if spec.dump and os.path.isfile(dp) else None

    def path(self, *p):
        return os.path.join(self.dir, *p)

    def lines(self, prefix):
        return [l for l in self.log if l.startswith(prefix)]

    def coll(self):  # the addon's own lines, stripped before compares
        return [l for l in self.log if l.startswith(('Collision', 'Damage '))]

    def has(self, text):
        return any(text in l for l in self.log)


class Context:
    def __init__(self, args, golden_header):
        self.args, self.runs, self.order, self.golden_header = args, {}, [], golden_header

    def first(self):
        return self.runs[self.order[0]]


def a1(r):
    for l in r.log:
        m = A1.match(l)
        if m:
            return dict(zip(('version', 'session', 'model', 'response', 'check', 'damage', 'log', 'render'), m.groups()))
    return None


def summary(r):
    s = [kv(m.group(1)) for m in (A3.match(l) for l in r.log) if m]
    return s[-1] if s else None


def as_int(v):
    v = v.strip().upper()
    return 1 if v == 'TRUE' else 0 if v == 'FALSE' else int(float(v))


def check_pins(r):  # every on run: A1 with the pinned values (E4 7.2, A1)
    a = a1(r)
    if not a:
        fail('run %s: no A1 line (Collision: active ...)' % r.spec.id)
    for key, cfgkey in (('model', 'collisionmodel'), ('response', 'collisionresponse'), ('check', 'collisioncheck'), ('log', 'collisionlog')):
        want = as_int(r.pins[cfgkey])
        if int(a[key]) != want:
            fail('run %s: A1 %s=%s, pinned %d' % (r.spec.id, key, a[key], want))


def run_checks(ctx, r, a):  # what every run must show (T 2.5, E4 7.2, A2)
    sid = r.spec.id
    if r.status == 'TERMINATING':
        fail('run %s: >>> TERMINATING <<< in Orbiter.log' % sid)
    if r.status == 'TIMEOUT':
        fail('run %s: TIMEOUT after %.0f s' % (sid, r.secs))
    if r.status != 'ENDON' and r.exit != r.spec.exit:
        fail('run %s: exit %s, expected %d' % (sid, r.exit, r.spec.exit))
    if r.spec.endon and r.status != 'ENDON':
        fail('run %s: %r never logged' % (sid, r.spec.endon))
    for l in r.log:
        if l.startswith(('Collision check failed:', 'CollTestHarness error', 'CollTestHarness script error')):
            fail('run %s: %s' % (sid, l))
    if r.spec.addon == 'on':
        check_pins(r)
    elif any(l.startswith(('Collision:', 'Collision ', 'Loading module Collision')) for l in r.log):
        fail('run %s: addon off but the log has Collision lines' % sid)
    if r.spec.dump and r.spec.exit == 0 and not r.spec.endon:
        if r.dump is None:
            fail('run %s: no dump TestOut/state.dump' % sid)
        if r.dump.header.get('run') != r.runid:
            fail('run %s: stale dump (run id %s, expected %s)' % (sid, r.dump.header.get('run'), r.runid))
        if r.dump.fail:
            fail('run %s: %s' % (sid, r.dump.fail))
        if r.dump.end != a.frames:
            fail('run %s: dump END %s, expected %d' % (sid, r.dump.end, a.frames))


def first_diff(la, lb):
    for i, (x, y) in enumerate(zip(la, lb)):
        if x != y:
            return i, x, y
    if len(la) != len(lb):
        n = min(len(la), len(lb))
        return n, la[n] if n < len(la) else '<end>', lb[n] if n < len(lb) else '<end>'
    return None


def same_dumps(da, db, what):
    d = first_diff(da.body_lines(), db.body_lines())
    if d:
        fail('%s: dumps differ at line %d: %s | %s' % (what, d[0], d[1][:160], d[2][:160]))


def same_coll(ra, rb, what, strip=(r'_us=[^ ]*', r'_ms=[^ ]*')):  # addon lines without timings
    def norm(r):
        out = []
        for l in r.coll():
            for s in strip:
                l = re.sub(s, '', l)
            out.append(l)
        return out
    d = first_diff(norm(ra), norm(rb))
    if d:
        fail('%s: Collision lines differ at %d: %s | %s' % (what, d[0], d[1][:160], d[2][:160]))


def golden_path(ctx, name):
    return os.path.join(ctx.args.data, 'golden', name + '.dump.gz')


def golden_write(ctx, r, name):
    p = golden_path(ctx, name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    text = ctx.golden_header + '\n' + '\n'.join(r.dump.lines) + '\n'
    with gzip.GzipFile(p, 'wb', mtime=0) as f:
        f.write(text.encode('latin-1'))
    return p


def golden_compare(ctx, r, name):  # T 4.5: header mismatch skips, missing golden skips, body bitwise
    p = golden_path(ctx, name)
    if os.environ.get('COLL_GOLDEN_WRITE') == '1':
        golden_write(ctx, r, name)
        print('golden written: %s' % p)
    if not os.path.isfile(p):
        raise Skip('no golden %s (COLL_GOLDEN_WRITE=1 makes it)' % p)
    with gzip.open(p, 'rb') as f:
        text = f.read().decode('latin-1')
    head = text.split('\n', 1)[0]
    if head != ctx.golden_header:
        raise Skip('golden header differs: %s | this build: %s' % (head, ctx.golden_header))
    same_dumps(Dump(text), r.dump, 'run %s vs golden %s' % (r.spec.id, name))


def quiet(r):  # G5 family: A1 pinned, writes=0, notices=0, no Collision t= line (E4 7.6 item 7)
    s = summary(r)
    if s is None:
        fail('run %s: no Collision summary line' % r.spec.id)
    for k in ('writes', 'notices'):
        if s.get(k) != '0':
            fail('run %s: %s=%s in a quiet scene' % (r.spec.id, k, s.get(k)))
    for l in r.log:
        if l.startswith('Collision t='):
            fail('run %s: contact line in a quiet scene: %s' % (r.spec.id, l))


def norm3(v):
    return sum(x * x for x in v) ** 0.5


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))
