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
        self.files = {}  # Flights/ and Scenarios/Playback/ at the end of the run: relpath -> (size, mtime)
        for top in ('Flights', os.path.join('Scenarios', 'Playback')):
            for base, _, files in os.walk(os.path.join(d, top)):
                for f in files:
                    p = os.path.join(base, f)
                    st = os.stat(p)
                    self.files[os.path.relpath(p, d)] = (st.st_size, st.st_mtime)

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
        self.args, self.runs, self.order, self.golden_header, self.history = args, {}, [], golden_header, []  # history: every execution, a run id may repeat

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
    for l in r.log:  # M6: the addon turned itself off after an exception
        if l.startswith('Collision: error in'):
            fail('run %s: %s' % (sid, l))
    if r.spec.expect:
        if r.status != r.spec.expect:
            fail('run %s: ended %s, %s expected' % (sid, r.status or 'by itself', r.spec.expect))
        return
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
    text = ctx.golden_header + '\n' + '\n'.join(r.dump.body_lines()) + '\n'  # no H line: its run id changes every run
    with gzip.GzipFile(p, 'wb', mtime=0) as f:
        f.write(text.encode('latin-1'))
    return p


def golden_compare(ctx, r, name):  # T 4.5: header mismatch skips, missing golden skips, body bitwise
    p = golden_path(ctx, name)
    if os.environ.get('COLL_GOLDEN_WRITE') == '1' and r.spec.addon == 'off':  # goldens come from addon-off runs only (addon: none)
        golden_write(ctx, r, name)
        print('golden written: %s' % p)
    if not os.path.isfile(p):
        raise Skip('no golden %s (COLL_GOLDEN_WRITE=1 makes it)' % p)
    with gzip.open(p, 'rb') as f:
        text = f.read().decode('latin-1')
    head = text.split('\n', 1)[0]
    if head != ctx.golden_header:
        raise Skip('golden header differs: %s | this build: %s' % (head, ctx.golden_header))
    same_dumps(Dump(text.split('\n', 1)[1]), r.dump, 'run %s vs golden %s' % (r.spec.id, name))


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


# ---- geometry, impact frame, momentum (design-C-T 4.4)

def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(a, s):
    return tuple(x * s for x in a)


def rot(d, k, name):  # R row-major m11..m33: global = R local
    r = d.vec(k, name, 'R')
    return (r[0:3], r[3:6], r[6:9])


def rot_apply(R, v):
    return tuple(dot(row, v) for row in R)


def axis(d, k, name, i):  # body axis i (0 x, 1 y, 2 z) in the global frame: column i of R
    R = rot(d, k, name)
    return (R[0][i], R[1][i], R[2][i])


def frame_of(d, t):  # the frame whose step holds t: SimT0(k) <= t < SimT0(k+1)
    ks = sorted(d.frames)
    for i, k in enumerate(ks):
        nxt = d.frames[ks[i + 1]]['simt'] if i + 1 < len(ks) else float('inf')
        if d.frames[k]['simt'] <= t < nxt:
            return k
    return None


def gap_pb(d, k, a='PB-A', b='PB-B', touch=6.0):  # head-on ShuttlePB pair: noses touch at a centre distance of 6 m (T 4.4, 7.2)
    return dot(sub(d.vec(k, b, 'p'), d.vec(k, a, 'p')), axis(d, k, a, 2)) - touch


def free_bodies(d, k, names=None):  # bodies that are not attached children
    f = d.frames[k]['V']
    return [n for n in (names or sorted(f)) if f[n].get('par', '-') == '-']


def momentum(d, k, names, O, vc):  # P and L about O in the frame moving at vc (T 4.4 items 2-3)
    P, L, sP, sL = (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0.0, 0.0
    for n in names:
        m = float(d.frames[k]['V'][n]['m'])
        x, v, w, I = (d.vec(k, n, key) for key in ('p', 'v', 'w', 'I'))
        rel, vr = sub(x, O), sub(v, vc)
        spin = rot_apply(rot(d, k, n), tuple(m * i * wi for i, wi in zip(I, w)))
        P = add(P, scale(v, m))
        L = add(L, sub(cross(rel, scale(vr, m)), spin))  # Orbiter's point velocity is crossp (r, w): the spin term enters negated
        sP += m * norm3(vr)
        sL += norm3(rel) * m * norm3(vr) + norm3(spin)
    return P, L, sP, sL


def momentum_check(doff, don, k, names=None, ptol=1e-9, ltol=2e-5):  # differential: on vs off across the impact frame k (T 4.4)
    names = free_bodies(doff, k, names)
    ms = [float(doff.frames[k]['V'][n]['m']) for n in names]
    M = sum(ms)
    O = scale(tuple(map(sum, zip(*[scale(doff.vec(k, n, 'p'), m) for n, m in zip(names, ms)]))), 1 / M)
    vc = scale(tuple(map(sum, zip(*[scale(doff.vec(k, n, 'v'), m) for n, m in zip(names, ms)]))), 1 / M)
    Pf, Lf, sP, sL = momentum(doff, k + 1, names, O, vc)
    Pn, Ln, _, _ = momentum(don, k + 1, names, O, vc)
    dP, dL = norm3(sub(Pn, Pf)), norm3(sub(Ln, Lf))
    if dP > ptol * sP:
        fail('momentum: |dP| %.3g > %.3g at frame %d' % (dP, ptol * sP, k + 1))
    if dL > ltol * sL:
        fail('angular momentum: |dL| %.3g > %.3g at frame %d' % (dL, ltol * sL, k + 1))
    return dP, dL


# ---- PNG (pure Python: zlib and the five filter types) and the camera of design-C-T 5.3

def png_decode(path_or_bytes):
    import struct
    import zlib
    data = path_or_bytes if isinstance(path_or_bytes, (bytes, bytearray)) else open(path_or_bytes, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    pos, idat, w = 8, b'', None
    while pos < len(data):
        n, t = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if t == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
            if depth != 8 or ctype not in (0, 2, 4, 6) or inter:
                raise ValueError('unsupported PNG: depth %d type %d interlace %d' % (depth, ctype, inter))
            ch = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
        elif t == b'IDAT':
            idat += body
        elif t == b'IEND':
            break
        pos += 12 + n
    raw, stride, out, prev = zlib.decompress(idat), w * ch, bytearray(), bytearray(w * ch)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0
            b, c = prev[i], prev[i - ch] if i >= ch else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif f != 0:
                raise ValueError('bad PNG filter %d' % f)
        out += line
        prev = line
    return w, h, ch, bytes(out)


def png_encode(w, h, ch, pix, filters=(0,)):  # test images for the self-test; filter per row cycles through `filters`
    import struct
    import zlib
    stride, raw, prev = w * ch, bytearray(), bytearray(w * ch)
    for y in range(h):
        line, f = pix[y * stride:(y + 1) * stride], filters[y % len(filters)]
        enc = bytearray(stride)
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0
            b, c = prev[i], prev[i - ch] if i >= ch else 0
            p = a + b - c
            pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            pred = (0, a, b, (a + b) // 2, a if pa <= pb and pa <= pc else b if pb <= pc else c)[f]
            enc[i] = (line[i] - pred) & 255
        raw += bytes([f]) + enc
        prev = bytearray(line)
    ctype = {1: 0, 3: 2, 4: 6}[ch]

    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, ctype, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(bytes(raw))) + chunk(b'IEND', b''))


def camera_for_dent(c, n, size):  # T 5.3: camera POS r phi theta (degrees, r in target sizes) looking at a dent centre c with normal n
    import math
    d = max(5.0, norm3(c))
    q = add(c, scale(n, d))
    u = scale(q, 1 / norm3(q))
    return norm3(q) / size, math.degrees(math.atan2(u[0], -u[2])), math.degrees(math.asin(-u[1]))


def camera_project(pt, r, phi, theta, size, fov, width=1280, height=720):  # pixel of a target-frame point, camera at r*size*u looking at the origin
    import math
    ph, th = math.radians(phi), math.radians(theta)
    u = (math.sin(ph) * math.cos(th), -math.sin(th), -math.cos(ph) * math.cos(th))  # Camera.cpp SetRelPos convention (T 5.3)
    cam = scale(u, r * size)
    fwd = scale(u, -1.0)
    up0 = (0.0, 1.0, 0.0) if abs(fwd[1]) < 0.99 else (0.0, 0.0, 1.0)
    right = cross(fwd, up0)
    right = scale(right, 1 / norm3(right))
    up = cross(right, fwd)
    v = sub(pt, cam)
    z = dot(v, fwd)
    if z <= 0:
        return None
    f = (height / 2) / math.tan(math.radians(fov) / 2)
    return width / 2 + f * dot(v, right) / z, height / 2 - f * dot(v, up) / z
