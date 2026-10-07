# not upstream: scenario-test runner of the collision addon (Design CA E4 7, design-C-T 2-3): fixed sandboxes, runs, guards, checks
import argparse
import fcntl
import importlib.util
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True  # nothing is written into the source folder
sys.path.insert(0, HERE)
import gen_scn  # noqa: E402
import scnlib  # noqa: E402

SKIP = 77
COMPILERS = {'ninja', 'make', 'gmake', 'cc1plus', 'cc1', 'ld', 'ld.bfd', 'ld.gold', 'collect2', 'glslangValidator'}
NOMIRROR = {'Tests', 'CMakeFiles', 'Testing', '_deps', 'CMakeCache.txt', 'cmake_install.cmake', 'CTestTestfile.cmake',
            'compile_commands.json', 'Orbiter.cfg', 'Orbiter.log', 'Sketchpad.log', 'keymap.cfg', 'home', 'run', 'TestOut'}
SPECIAL = {'Config', 'Scenarios', 'Script', 'Flights', 'Images', 'Modules', 'Textures', 'Meshes'}
LVP_ICDS = ['/usr/share/vulkan/icd.d/lvp_icd.json', '/usr/share/vulkan/icd.d/lvp_icd.x86_64.json']
XDISPLAY = ':58'
TEXBODIES = ('Earth', 'Moon', 'Mars')  # bodies whose textures upstream ships separately


class TestError(Exception):  # exit 1: the test could not run as specified, or a guard refused
    pass


class Skipped(Exception):  # exit 77
    pass


LAST = ['']


def log(msg):
    LAST[0] = msg
    print(msg, flush=True)


# ---- skip checks and the lock

def ancestors():
    out, pid = set(), os.getpid()
    while pid > 1:
        out.add(pid)
        try:
            with open('/proc/%d/stat' % pid) as f:
                pid = int(f.read().rsplit(')', 1)[1].split()[1])
        except (OSError, ValueError, IndexError):
            break
    return out


def compilers_running():
    mine, uid = ancestors(), os.getuid()
    found = []
    for d in os.listdir('/proc'):
        if not d.isdigit() or int(d) in mine:
            continue
        try:
            if os.stat('/proc/' + d).st_uid != uid:
                continue
            with open('/proc/%s/comm' % d) as f:
                comm = f.read().strip()
        except OSError:
            continue
        if comm in COMPILERS:
            found.append(comm)
    return found


def skip_checks():  # step 1 of E4 7.5: a skipped run deletes nothing
    c = compilers_running()
    if c:
        raise Skipped('build running (%s), test not run' % ' '.join(sorted(set(c))))


def take_lock(work):
    rt = os.environ.get('XDG_RUNTIME_DIR')
    path = os.path.join(rt, 'orbiter-scn.lock') if rt and os.path.isdir(rt) else os.path.join(work, 'runner.lock')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    f = open(path, 'a')
    t0 = time.time()
    while True:
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return f
        except BlockingIOError:
            if time.time() - t0 > 600:
                raise Skipped('another scenario test holds %s for 10 min' % path)
            time.sleep(1)


# ---- fixed-folder sandbox (created once, overwritten in place)

def ensure_dir(p):
    if os.path.islink(p):
        raise TestError('sandbox layout changed: %s is a symlink, a folder is needed (move the run folder away by hand)' % p)
    os.makedirs(p, exist_ok=True)


def ensure_link(p, target):
    if os.path.islink(p):
        if os.readlink(p) == target:
            return
        tmp = p + '.relink'
        if os.path.islink(tmp):
            os.replace(tmp, p + '.stale')  # a link left by an interrupted re-point is moved aside, not deleted
        os.symlink(target, tmp)
        os.replace(tmp, p)  # re-pointed by rename, as T decision v2.2
        return
    if os.path.exists(p):
        raise TestError('sandbox layout changed: %s is not a symlink (move the run folder away by hand)' % p)
    os.symlink(target, p)


def write_if_changed(p, data):
    if isinstance(data, str):
        data = data.encode('utf-8')
    if os.path.islink(p):
        raise TestError('sandbox layout changed: %s is a symlink, a file is needed' % p)
    try:
        with open(p, 'rb') as f:
            if f.read() == data:
                return
    except OSError:
        pass
    with open(p, 'wb') as f:
        f.write(data)


def copy_file(src, dst):
    with open(src, 'rb') as f:
        write_if_changed(dst, f.read())


def copy_tree(src, dst, skip=()):
    ensure_dir(dst)
    for e in sorted(os.listdir(src)):
        s, d = os.path.join(src, e), os.path.join(dst, e)
        if e in skip:
            continue
        if os.path.isdir(s):
            copy_tree(s, d)
        elif os.path.isfile(s):
            copy_file(s, d)


def link_entries(src, dst, skip=()):
    ensure_dir(dst)
    for e in sorted(os.listdir(src)):
        if e not in skip:
            ensure_link(os.path.join(dst, e), os.path.join(src, e))


class Sandbox:
    def __init__(self, a, test, run):
        self.a = a
        self.work = os.path.realpath(a.work)
        self.dir = os.path.join(self.work, test, run)
        self.root = os.path.realpath(a.root)

    def path(self, *p):
        return os.path.join(self.dir, *p)

    def mirror(self, client):
        r, d = self.root, self.dir
        os.makedirs(d, exist_ok=True)
        for e in sorted(os.listdir(r)):
            if e in NOMIRROR or e in SPECIAL or e.endswith('.ninja') or e.startswith('.ninja'):
                continue
            ensure_link(os.path.join(d, e), os.path.join(r, e))
        cfgsrc = os.path.join(r, 'Config')
        copy_tree(cfgsrc, self.path('Config')) if os.path.isdir(cfgsrc) else ensure_dir(self.path('Config'))
        vdir = os.path.join(self.a.data, 'cfg', 'Vessels')
        if os.path.isdir(vdir):
            copy_tree(vdir, self.path('Config', 'Vessels'))
        scn = os.path.join(r, 'Scenarios')
        copy_tree(scn, self.path('Scenarios'), skip=('Playback', 'Quicksave')) if os.path.isdir(scn) else ensure_dir(self.path('Scenarios'))
        for e in ('Quicksave', 'Tests', os.path.join('Tests', 'Coll'), os.path.join('Tests', 'Coll', 'Saved'), os.path.join('Tests', 'Coll', 'Inherit')):
            ensure_dir(self.path('Scenarios', e))
        for p in (self.path('Flights'), self.path('Scenarios', 'Playback')):  # the clear's two folders: a planted link is left to the guards
            if not os.path.lexists(p):
                os.makedirs(p)
        scr = os.path.join(r, 'Script')
        copy_tree(scr, self.path('Script')) if os.path.isdir(scr) else ensure_dir(self.path('Script'))
        copy_tree(os.path.join(self.a.data, 'lua'), self.path('Script', 'Tests', 'Coll'))
        for e in ('Images', 'TestOut', 'home', os.path.join('home', '.config'), os.path.join('home', '.cache'),
                  os.path.join('home', '.local'), os.path.join('home', '.local', 'share'), os.path.join('home', '.local', 'state'), 'run'):
            ensure_dir(self.path(e))
        os.chmod(self.path('run'), 0o700)
        self.mirror_modules(client)
        self.mirror_textures()
        self.mirror_meshes()
        km = os.path.join(r, 'keymap.cfg')
        if os.path.isfile(km):
            copy_file(km, self.path('keymap.cfg'))

    def mirror_modules(self, client):
        src, dst = os.path.join(self.root, 'Modules'), self.path('Modules')
        ensure_dir(dst)
        if os.path.isdir(src):
            link_entries(src, dst, skip=('Plugin', 'VulkanClient'))
        plug = os.path.join(src, 'Plugin')
        if os.path.isdir(plug):
            link_entries(plug, os.path.join(dst, 'Plugin'), skip=('Collision.so',))
        else:
            ensure_dir(os.path.join(dst, 'Plugin'))
        if self.a.addon_so:
            ensure_link(os.path.join(dst, 'Plugin', 'Collision.so'), os.path.realpath(self.a.addon_so))
        vk = os.path.join(src, 'VulkanClient')
        if os.path.isdir(vk):
            link_entries(vk, os.path.join(dst, 'VulkanClient'), skip=('VulkanClient.cfg', 'D3D9ClientLog.html'))
            tmpl = os.path.join(self.a.data, 'cfg', 'VulkanClient.cfg')
            if os.path.isfile(tmpl):
                copy_file(tmpl, os.path.join(dst, 'VulkanClient', 'VulkanClient.cfg'))
        tm = self.a.modules
        for sub in ('', 'Plugin'):  # test modules: vessel modules in Modules/, test plugins in Modules/Plugin/
            d = os.path.join(tm, sub) if tm else ''
            if d and os.path.isdir(d):
                for e in sorted(os.listdir(d)):
                    if e.endswith('.so'):
                        ensure_link(os.path.join(dst, sub, e), os.path.join(os.path.realpath(d), e))

    def mirror_textures(self):
        src, dst = os.path.join(self.root, 'Textures'), self.path('Textures')
        if not os.path.isdir(src):
            ensure_dir(dst)
            return
        ensure_dir(dst)
        for e in sorted(os.listdir(src)):
            s = os.path.join(src, e)
            if os.path.isdir(s) and os.path.isdir(os.path.join(s, 'Archive')):
                link_entries(s, os.path.join(dst, e), skip=('Archive',))  # planet archives never reach a test (T 2.3)
            else:
                ensure_link(os.path.join(dst, e), s)
        for b in TEXBODIES:  # without a texture folder or .tex the client waits in a modal box for the focus planet (VPlanet.cpp:541-548)
            if not os.path.lexists(os.path.join(src, b)) and not os.path.exists(os.path.join(src, b + '.tex')):
                ensure_dir(os.path.join(dst, b))

    def mirror_meshes(self):
        src, dst = os.path.join(self.root, 'Meshes'), self.path('Meshes')
        cols = []
        if self.a.addon_src:
            md = os.path.join(self.a.addon_src, 'Meshes')
            for base, _, files in os.walk(md):
                for f in files:
                    if f.endswith('.col'):
                        cols.append(os.path.relpath(os.path.join(base, f), md))
        if not cols:
            ensure_link(dst, src)
            return
        dirs = {''}
        for c in cols:
            p = os.path.dirname(c)
            while p:
                dirs.add(p)
                p = os.path.dirname(p)
        for rel in sorted(dirs, key=len):
            s, d = os.path.join(src, rel), os.path.join(dst, rel)
            skip = {e for e in (os.listdir(s) if os.path.isdir(s) else []) if os.path.join(rel, e) in dirs or os.path.join(rel, e) in cols}
            link_entries(s, d, skip=skip) if os.path.isdir(s) else ensure_dir(d)
        for c in cols:
            ensure_link(os.path.join(dst, c), os.path.join(os.path.realpath(self.a.addon_src), 'Meshes', c))


# ---- the clear of E4 7.5 (the only deletion the runner makes)

def clear_targets(work, test, run):  # (path, the only real path it may have)
    base = os.path.join(os.path.realpath(work), test, run)
    return [(os.path.join(work, test, run, 'Flights'), os.path.join(base, 'Flights')),
            (os.path.join(work, test, run, 'Scenarios', 'Playback'), os.path.join(base, 'Scenarios', 'Playback'))]


def guard_and_clear(work, test, run):
    targets = clear_targets(work, test, run)
    for p, want in targets:  # step 3: every guard before any delete; a refusal is a test error, never a skip
        if os.path.islink(p):
            raise TestError('runner: clear refused %s symlink' % p)
        if not os.path.isdir(p):
            raise TestError('runner: clear refused %s not a folder' % p)
        if os.path.realpath(p) != want:
            raise TestError('runner: clear refused %s resolves to %s' % (p, os.path.realpath(p)))
    n = 0
    for p, _ in targets:  # step 4: files and symlinks unlinked (never followed), real folders removed
        for e in sorted(os.listdir(p)):
            c = os.path.join(p, e)
            if os.path.islink(c) or not os.path.isdir(c):
                os.unlink(c)
            else:
                shutil.rmtree(c)
            n += 1
    return n


def selftest_skipscan(base):  # T0.9: a process named like a compiler makes the real scan report a build
    fake = os.path.join(base, 'ninja')
    sleep = shutil.which('sleep')
    if not sleep:
        return 0
    copy_file(sleep, fake)
    os.chmod(fake, 0o755)
    p = subprocess.Popen([fake, '30'], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(20):
            found = compilers_running()
            if 'ninja' in found:
                break
            time.sleep(0.1)
        try:
            skip_checks()
            log('selftest skipscan: a running "ninja" was not reported')
            return 1
        except Skipped:
            return 0
    finally:
        p.kill()
        p.wait()


def selftest_guards(work):  # T0.1: planted unsafe Flights folders give the step-3 error and delete nothing; a skipped run deletes nothing
    base = os.path.join(os.path.realpath(work), 'Scn.RunnerGuard')
    ensure_dir(os.path.join(base, 'outside'))
    keep = os.path.join(base, 'outside', 'keep.txt')
    write_if_changed(keep, 'planted by Scn.RunnerGuard; must survive\n')
    ensure_dir(os.path.join(base, 'elsewhere', 'Flights'))
    ensure_dir(os.path.join(base, 'elsewhere', 'Scenarios', 'Playback'))
    keep2 = os.path.join(base, 'elsewhere', 'Flights', 'keep.txt')
    write_if_changed(keep2, 'planted by Scn.RunnerGuard; must survive\n')
    cases = []
    r = os.path.join(base, 'symlinked')  # Flights is a symlink to a folder outside R
    ensure_dir(os.path.join(r, 'Scenarios', 'Playback'))
    ensure_link(os.path.join(r, 'Flights'), os.path.join(base, 'outside'))
    cases.append(('symlinked', 'symlink', keep))
    ensure_link(os.path.join(base, 'resolved'), os.path.join(base, 'elsewhere'))  # R itself resolves outside the run folder
    cases.append(('resolved', 'resolves to', keep2))
    errors = 0
    for run, why, k in cases:
        try:
            guard_and_clear(work, 'Scn.RunnerGuard', run)
            log('selftest %s: clear was not refused' % run)
            errors += 1
        except TestError as e:
            if why not in str(e):
                log('selftest %s: wrong refusal: %s' % (run, e))
                errors += 1
        if not os.path.isfile(k):
            log('selftest %s: %s was deleted' % (run, k))
            errors += 1
    ensure_dir(os.path.join(base, 'root'))  # a whole run through main: the refusal is exit 1, never 77
    rc = main(['--name', 'Scn.RunnerGuard', '--work', work, '--root', os.path.join(base, 'root'), '--exe', '/bin/false',
               '--scn', 'gen:pair', '--run', 'symlinked|headless|off||||'])
    if rc != 1 or not os.path.isfile(keep) or 'clear refused' not in LAST[0]:
        log('selftest symlinked through main: exit %s, planted file %s' % (rc, 'kept' if os.path.isfile(keep) else 'deleted'))
        errors += 1
    r = os.path.join(base, 'ok')  # a safe folder is cleared: files, real folders, symlinks (not followed)
    for d in ('Flights', os.path.join('Flights', 'rec'), os.path.join('Scenarios', 'Playback')):
        ensure_dir(os.path.join(r, d))
    write_if_changed(os.path.join(r, 'Flights', 'rec', 'system.dat'), 'x\n')
    write_if_changed(os.path.join(r, 'Scenarios', 'Playback', 'rec.scn'), 'x\n')
    ensure_link(os.path.join(r, 'Flights', 'link'), os.path.join(base, 'outside'))
    write_if_changed(os.path.join(r, 'kept.txt'), 'R files outside the two folders stay\n')
    n = guard_and_clear(work, 'Scn.RunnerGuard', 'ok')
    left = os.listdir(os.path.join(r, 'Flights')) + os.listdir(os.path.join(r, 'Scenarios', 'Playback'))
    if n != 3 or left or not os.path.isfile(keep) or not os.path.isfile(os.path.join(r, 'kept.txt')):
        log('selftest ok: cleared %d, left %s' % (n, left))
        errors += 1
    r = os.path.join(base, 'skipped')  # the skip check comes before any clear
    ensure_dir(os.path.join(r, 'Scenarios', 'Playback'))
    ensure_dir(os.path.join(r, 'Flights'))
    k3 = os.path.join(r, 'Flights', 'keep.txt')
    write_if_changed(k3, 'planted; a skipped run deletes nothing\n')
    global compilers_running
    real = compilers_running
    compilers_running = lambda: ['ninja']
    try:
        rc = main(['--name', 'Scn.RunnerGuard', '--work', work, '--root', base, '--exe', '/bin/false', '--run', 'skipped|headless|off||||'])
    finally:
        compilers_running = real
    if rc != SKIP or not os.path.isfile(k3):
        log('selftest skipped: exit %s, planted file %s' % (rc, 'kept' if os.path.isfile(k3) else 'deleted'))
        errors += 1
    errors += selftest_skipscan(base)
    log('runner guard selftest: %s' % ('FAIL' if errors else 'PASS'))
    return 1 if errors else 0


# ---- run specs and per-run files

class RunSpec:
    def __init__(self, text):
        f = (text.split('|') + [''] * 7)[:7]
        self.id, self.mode, self.addon, self.order, args, self.inherit, self.actions = [x.strip() for x in f]
        self.mode = self.mode or 'headless'
        self.addon = self.addon or 'off'
        self.order = self.order or 'client-first'
        if not re.match(r'^[A-Za-z0-9_.-]+$', self.id or ''):
            raise TestError('bad run id in %r' % text)
        if self.mode not in ('headless', 'client', 'paced'):
            raise TestError('bad mode %r' % self.mode)
        if self.addon not in ('on', 'off'):
            raise TestError('bad addon field %r' % self.addon)
        if self.order not in ('client-first', 'addon-first'):
            raise TestError('bad order %r' % self.order)
        self.args, self.cfg, self.acfg, self.exit, self.endon, self.stdin, self.dump = [], [], [], 0, None, None, True
        self.scn, self.limit, self.expect = None, True, None
        for t in shlex.split(args):
            if t.startswith('@cfg:'):
                self.cfg.append(item(t[5:]))
            elif t.startswith('@acfg:'):
                self.acfg.append(item(t[6:]))
            elif t.startswith('@exit='):
                self.exit = int(t[6:])
            elif t.startswith('@endon='):
                self.endon = t[7:]
            elif t.startswith('@stdin='):
                self.stdin = t[7:]
            elif t == '@nodump':
                self.dump = False
            elif t == '@nolimit':  # no --maxframes: the run ends by its own args (--maxsimtime) or the timeout
                self.limit = False
            elif t.startswith('@expect='):  # the end the run must have: TIMEOUT
                self.expect = t[8:]
            elif t.startswith('@scn='):
                self.scn = t[5:]
            else:
                self.args.append(t)

    def client(self):
        return self.mode == 'client'


def item(t):  # Key=Value -> "Key = Value"
    k, _, v = t.partition('=')
    return '%s = %s' % (k.strip(), v.strip())


def cfg_key(line):
    return line.split('=', 1)[0].strip().lower()


def merge_cfg(text, items):
    lines = text.splitlines()
    keys = {cfg_key(i): i for i in items}
    out, seen = [], set()
    for l in lines:
        k = cfg_key(l) if '=' in l and not l.lstrip().startswith(';') else None
        if k in keys:
            out.append(keys[k])
            seen.add(k)
        else:
            out.append(l)
    out += [keys[k] for k in keys if k not in seen]
    return '\n'.join(out) + '\n'


PINS = (('CollisionModel', '1'), ('CollisionResponse', 'TRUE'), ('CollisionCheck', 'TRUE'), ('CollisionLog', '2'))


def addon_cfg(a, acfg):
    tmpl = os.path.join(a.addon_src, 'Config', 'Collision.cfg') if a.addon_src else None
    text = open(tmpl).read() if tmpl and os.path.isfile(tmpl) else '; not upstream: collision addon test cfg\n'
    given = {cfg_key(i) for i in acfg}
    if 'collisioncheck' in given:
        raise TestError('CollisionCheck is pinned TRUE in every test (E4 7.2)')
    items = ['%s = %s' % (k, v) for k, v in PINS if k.lower() not in given] + list(acfg)
    return merge_cfg(text, items), {cfg_key(i): i.split('=', 1)[1].strip() for i in items}


def orbiter_cfg(a, spec, cfgitems):
    name = 'Orbiter.client.cfg' if spec.client() else 'Orbiter.headless.cfg'
    text = merge_cfg(open(os.path.join(a.data, 'cfg', name)).read(), cfgitems)
    mods = []
    if spec.client():
        mods = ['VulkanClient']
        if spec.addon == 'on':
            mods = ['Collision', 'VulkanClient'] if spec.order == 'addon-first' else ['VulkanClient', 'Collision']
    elif spec.addon == 'on':
        mods = ['Collision']
    mods += [m for m in a.module if m not in mods]
    mods.append('CollTestHarness')  # last: its post-step (dumps, Lua actions) follows the addon's (E4 7.2, 7.4)
    if mods:
        text += 'ACTIVE_MODULES\n' + ''.join(m + '\n' for m in mods) + 'END_MODULES\n'
    return text


def stem_of(name):
    return re.sub(r'[^A-Za-z0-9]+', '_', name).strip('_')


def lua_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def split_actions(spec, data):
    lines = []
    if spec.actions.startswith('@') and spec.actions.endswith('.txt'):  # @<file> in act/; "@<ship>:<line>" targets a ship block
        with open(os.path.join(data, 'act', spec.actions[1:])) as f:
            lines = [l.strip() for l in f if l.strip() and not l.lstrip().startswith(';')]
    elif spec.actions:
        lines = [l.strip() for l in spec.actions.split(',') if l.strip()]
    lua = [l for l in lines if l.split()[0] in ('LUACALL', 'LUASTATUS', 'LUASAVE', 'LUASHOT')]  # the harness's; TEST* lines go to the scenario
    return [l for l in lines if l not in lua], lua


def run_lua(a, spec, runid, stem, orig_script):
    lua_act = split_actions(spec, a.data)[1]
    acts = []
    for l in lua_act:
        w = l.split()
        if len(w) < 3:
            raise TestError('bad Lua action %r' % l)
        args = []
        for x in w[4:] if w[0] == 'LUACALL' else []:
            args.append(x if re.match(r'^-?[0-9.eE+-]+$', x) else lua_str(x))
        acts.append('{k=%d, kind=%s, v=%s, m=%s, args={%s}}' % (int(w[1]), lua_str(w[0]), lua_str(w[2]),
                    lua_str(w[3] if len(w) > 3 and w[0] == 'LUACALL' else ''), ', '.join(args)))
    return ('-- not upstream: generated by runner.py for %s run %s, read by harness.lua through CollTestHarness\n' % (a.name, spec.id)
            + 'return {run=%s, scn=%s, h=%s, frames=%d, every=%d, dump=%s, test=%s, script=%s, actions={%s}}\n' % (
                lua_str(runid), lua_str(stem), lua_str(str(a.step)), a.frames, a.every, 'true' if spec.dump else 'false',
                lua_str(a.lua or ''), lua_str(orig_script or ''), ', '.join(acts)))


def write_scenario(a, spec, sbx, stem, runid):  # step 6: the run's scenario is always Scenarios/Tests/Coll/<stem>.scn
    src = (spec.scn or a.scn).replace('{stem}', stem)
    if src.startswith('gen:'):
        text = gen_scn.generate(src[4:], a)
        if isinstance(text, tuple):  # synthetic flight records, written after the clear (E4 7.5 step 6), only into Flights/
            text, files = text
            for rel, data in sorted(files.items()):
                if rel.split('/')[0] != 'Flights' or '..' in rel.split('/'):
                    raise TestError('generated file outside Flights/: %s' % rel)
                ensure_dir(os.path.dirname(sbx.path(rel)))
                write_if_changed(sbx.path(rel), data)
    elif src.endswith('.scn'):  # a scenario file of this folder (or an absolute path)
        p = src if os.path.isabs(src) else os.path.join(a.data, src)
        if not os.path.isfile(p):
            raise TestError('scenario not found: %s' % p)
        text = open(p, encoding='latin-1').read()
    else:
        p = sbx.path('Scenarios', src + '.scn')
        if not os.path.isfile(p):
            raise TestError('scenario not found: %s' % p)
        text = open(p, encoding='latin-1').read()
    text, orig = gen_scn.set_script(text, None)  # CollTestHarness runs the scenario's own script, frame-exact
    acts = split_actions(spec, a.data)[0]
    if acts:
        text = gen_scn.insert_actions(text, acts)
    target = os.path.join('Tests', 'Coll', stem)
    write_if_changed(sbx.path('Scenarios', target + '.scn'), text.encode('latin-1'))
    write_if_changed(sbx.path('Script', 'Tests', 'Coll', 'run.lua'), run_lua(a, spec, runid, stem, orig))
    return target


def inherit(a, sbx, spec):
    if not spec.inherit:
        return
    src = Sandbox(a, a.name, spec.inherit)
    if not os.path.isdir(src.dir):
        raise TestError('run %s inherits %s, which has not run' % (spec.id, spec.inherit))
    copy_tree(src.path('Scenarios', 'Playback'), sbx.path('Scenarios', 'Playback'))
    copy_tree(src.path('Flights'), sbx.path('Flights'))
    copy_tree(src.path('Scenarios', 'Tests', 'Coll', 'Saved'), sbx.path('Scenarios', 'Tests', 'Coll', 'Inherit'))
    cs = src.path('Scenarios', '(Current state).scn')
    if os.path.isfile(cs):
        copy_file(cs, sbx.path('Scenarios', 'Tests', 'Coll', 'Inherit', 'CurrentState.scn'))


# ---- leak guard

def leak_snapshot(root, addon_so):
    snap = {}

    def add(p):
        try:
            st = os.stat(p)
            snap[p] = (st.st_size, st.st_mtime_ns)
        except OSError:
            snap[p] = None

    for n in ('Orbiter.cfg', 'Orbiter.log', 'keymap.cfg', 'Sketchpad.log', os.path.join('Config', 'Collision.cfg'),
              os.path.join('Modules', 'Plugin', 'Collision.so')):
        add(os.path.join(root, n))
    for d, pat in (('Scenarios', None), ('Config', None), ('Flights', None), ('Images', None), ('Modules', r'\.(cfg|log|html)$'), ('Meshes', r'\.col$')):
        for base, _, files in os.walk(os.path.join(root, d)):
            for f in files:
                if pat is None or re.search(pat, f):
                    add(os.path.join(base, f))
    return snap


def leak_check(before, root, addon_so):
    after = leak_snapshot(root, addon_so)
    bad = sorted(p for p in set(before) | set(after) if before.get(p) != after.get(p))
    if bad:
        raise TestError('sandbox leak: ' + ', '.join(bad[:10]))


# ---- Xvfb fixture (client runs)

def xvfb_pidfile(work):
    return os.path.join(work, 'xvfb58.pid')


def xvfb_start(work):
    os.makedirs(work, exist_ok=True)
    if os.path.exists('/tmp/.X11-unix/X58'):
        log('runner: X server on %s already running, used as is' % XDISPLAY)
        return 0
    if not shutil.which('Xvfb'):
        log('runner: Xvfb missing')
        return SKIP
    p = subprocess.Popen(['nice', '-n', '10', 'Xvfb', XDISPLAY, '-screen', '0', '1920x1080x24', '-nolisten', 'tcp', '-noreset'],
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    with open(xvfb_pidfile(work), 'w') as f:
        f.write('%d\n' % p.pid)
    for _ in range(50):
        if os.path.exists('/tmp/.X11-unix/X58'):
            return 0
        time.sleep(0.1)
    log('runner: Xvfb did not start')
    return 1


def xvfb_stop(work):
    try:
        pid = int(open(xvfb_pidfile(work)).read())
    except (OSError, ValueError):
        return 0
    try:
        os.kill(pid, signal.SIGTERM)
    except OSError:
        pass
    write_if_changed(xvfb_pidfile(work), '')
    return 0


def lvp_icd():
    for p in LVP_ICDS:
        if os.path.isfile(p):
            return p
    return None


# ---- one run

def run_env(sbx, spec):
    env = {k: v for k, v in os.environ.items() if not k.startswith(('XDG_', 'WAYLAND_', 'LUA_')) and k not in ('DISPLAY', 'DBUS_SESSION_BUS_ADDRESS', 'QT_QPA_PLATFORM')}
    env.update(HOME=sbx.path('home'), XDG_CONFIG_HOME=sbx.path('home', '.config'), XDG_CACHE_HOME=sbx.path('home', '.cache'),
               XDG_DATA_HOME=sbx.path('home', '.local', 'share'), XDG_STATE_HOME=sbx.path('home', '.local', 'state'),
               XDG_RUNTIME_DIR=sbx.path('run'), LANG='C.UTF-8', QT_QPA_PLATFORM='offscreen')
    if spec.client():
        icd = lvp_icd()
        env.update(DISPLAY=XDISPLAY, QT_QPA_PLATFORM='xcb', VK_DRIVER_FILES=icd, VK_ICD_FILENAMES=icd, LP_NUM_THREADS='4',
                   VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation', VK_LAYER_ENABLES='', VK_KHRONOS_VALIDATION_LOG_FILENAME=sbx.path('vvl.txt'))
    return env


def launch(a, sbx, spec, scn):
    cmd = [os.path.realpath(a.exe), '--scenariox=' + scn]
    step = a.step
    if spec.mode != 'paced':
        cmd.append('--fixedstep=%s' % step)
    if spec.limit:
        cmd.append('--maxframes=%d' % (a.frames + 5))
    if spec.endon:
        cmd[1] = '--scenario=' + scn
    cmd += a.arg + spec.args
    for p in ('Orbiter.log', 'stdout.txt', 'stderr.txt'):
        write_if_changed(sbx.path(p), b'')
    write_if_changed(sbx.path('cmd.txt'), ' '.join(shlex.quote(c) for c in cmd) + '\n')
    out, err = open(sbx.path('stdout.txt'), 'wb'), open(sbx.path('stderr.txt'), 'wb')
    stdin = subprocess.PIPE if spec.stdin else subprocess.DEVNULL
    t0 = time.time()
    p = subprocess.Popen(cmd, cwd=sbx.dir, env=run_env(sbx, spec), stdin=stdin, stdout=out, stderr=err, start_new_session=True)
    feed = scnlib.read_lines(os.path.join(a.data, 'stdin', spec.stdin)) if spec.stdin else []
    status, logpath = None, sbx.path('Orbiter.log')
    while True:
        try:
            rc = p.wait(timeout=0.5)
            break
        except subprocess.TimeoutExpired:
            pass
        text = scnlib.read_text(logpath)
        if '>>> TERMINATING <<<' in text:
            status = 'TERMINATING'
        elif spec.endon and spec.endon in text:
            status = 'ENDON'
        elif time.time() - t0 > a.timeout:
            status = 'TIMEOUT'
        while feed and feed[0][0] in text:
            when, line = feed.pop(0)
            time.sleep(0.5)
            p.stdin.write((line + '\n').encode())
            p.stdin.flush()
            if not feed:
                p.stdin.close()
        if status:
            try:
                os.killpg(p.pid, signal.SIGKILL)
            except OSError:
                pass
            rc = p.wait()
            break
    out.close()
    err.close()
    try:
        os.killpg(p.pid, signal.SIGKILL)  # nothing of the run may stay behind
    except OSError:
        pass
    return rc, status, time.time() - t0


def golden_header(a):
    up = 'unknown'
    if a.upstream and os.path.isfile(a.upstream):
        for l in open(a.upstream):
            w = l.split()
            if len(w) >= 3 and 'orbitersim' in w[0]:
                up = w[2][:12]
                break
    cpu = 'unknown'
    try:
        for l in open('/proc/cpuinfo'):
            if l.startswith('model name'):
                cpu = l.split(':', 1)[1].strip().replace(' ', '_')
                break
    except OSError:
        pass
    return 'G upstream=%s compiler=%s build=%s cpu=%s addon=none' % (up, a.compiler, a.buildtype, cpu)


def do_run(a, spec, ctx, runid):
    sbx = Sandbox(a, a.name, spec.id)
    if spec.client():
        if not lvp_icd():
            raise Skipped('lavapipe ICD missing')
        if not os.path.exists('/tmp/.X11-unix/X58'):
            raise Skipped('no X server on %s (fixture Scn.Xvfb.Start)' % XDISPLAY)
    t0 = time.time()
    sbx.mirror(spec.client())  # step 2
    write_if_changed(sbx.path('run.id'), runid + '\n')
    n = guard_and_clear(a.work, a.name, spec.id)  # steps 3-4
    inherit(a, sbx, spec)  # step 5
    scn = write_scenario(a, spec, sbx, stem_of(a.name), runid)  # step 6
    acfg_text, pins = addon_cfg(a, a.acfg + spec.acfg)  # step 7
    write_if_changed(sbx.path('Config', 'Collision.cfg'), acfg_text)
    write_if_changed(sbx.path('Orbiter.cfg'), orbiter_cfg(a, spec, a.cfg + spec.cfg))
    rc, status, secs = launch(a, sbx, spec, scn)
    r = scnlib.RunResult(spec, sbx.dir, rc, status, secs, pins, runid, cleared=n)
    r.start = t0
    log('run %s: exit %s%s in %.1f s, %s' % (spec.id, rc, ' (%s)' % status if status else '', secs, sbx.dir))
    ctx.runs[spec.id] = r
    if spec.id not in ctx.order:
        ctx.order.append(spec.id)
    ctx.history.append(r)
    scnlib.run_checks(ctx, r, a)


def load_check(a):
    if not a.check:
        return None
    p = os.path.join(a.data, 'checks', a.check + '.py')
    if not os.path.isfile(p):
        raise TestError('check not found: %s' % p)
    sys.path.insert(0, os.path.dirname(p))  # checks may share helpers
    spec = importlib.util.spec_from_file_location('check_' + a.check, p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def run_test(a):
    for _ in range(20):
        if os.nice(0) >= 10:
            break
        os.nice(1)
    skip_checks()
    lock = take_lock(a.work)
    try:
        skip_checks()
        specs = [RunSpec(r) for r in (a.run or ['main|headless|off||||'])]
        check = load_check(a)
        before = leak_snapshot(os.path.realpath(a.root), a.addon_so)
        ctx = scnlib.Context(a, golden_header(a))
        runid = '%d-%d' % (int(time.time()), os.getpid())
        for spec in specs:
            do_run(a, spec, ctx, runid)
        leak_check(before, os.path.realpath(a.root), a.addon_so)
        if check:
            check.check(ctx)
    finally:
        lock.close()


def parse(argv):
    p = argparse.ArgumentParser(description='collision addon scenario-test runner')
    p.add_argument('--name')
    p.add_argument('--exe')
    p.add_argument('--root')
    p.add_argument('--data', default=HERE)
    p.add_argument('--work')
    p.add_argument('--modules', default='')
    p.add_argument('--addon-so', default='')
    p.add_argument('--addon-src', default='')
    p.add_argument('--upstream', default='')
    p.add_argument('--scn', default='')
    p.add_argument('--step', type=float, default=0.02)
    p.add_argument('--frames', type=int, default=60)
    p.add_argument('--every', type=int, default=1)
    p.add_argument('--timeout', type=float, default=120)
    p.add_argument('--check', default='')
    p.add_argument('--lua', default='')
    p.add_argument('--run', action='append', default=[])
    p.add_argument('--cfg', action='append', default=[])
    p.add_argument('--acfg', action='append', default=[])
    p.add_argument('--arg', action='append', default=[])
    p.add_argument('--module', action='append', default=[])
    p.add_argument('--compiler', default='unknown')
    p.add_argument('--buildtype', default='unknown')
    p.add_argument('--xvfb-start', action='store_true')
    p.add_argument('--xvfb-stop', action='store_true')
    p.add_argument('--selftest-guards', action='store_true')
    a = p.parse_args(argv)
    a.cfg = [item(c) if '=' in c and ' = ' not in c else c for c in a.cfg]
    a.acfg = [item(c) if '=' in c and ' = ' not in c else c for c in a.acfg]
    return a


def main(argv=None):
    a = parse(sys.argv[1:] if argv is None else argv)
    try:
        if a.xvfb_start:
            return xvfb_start(a.work)
        if a.xvfb_stop:
            return xvfb_stop(a.work)
        if a.selftest_guards:
            return selftest_guards(a.work)
        run_test(a)
    except Skipped as e:
        log('SKIP: %s' % e)
        return SKIP
    except TestError as e:
        log('ERROR: %s' % e)
        return 1
    except scnlib.Skip as e:
        log('SKIP: %s' % e)
        return SKIP
    except scnlib.CheckFail as e:
        log('FAIL: %s' % e)
        return 1
    except Exception:  # a broken check or runner: a test error with its traceback, never a pass
        import traceback
        traceback.print_exc()
        log('ERROR: runner or check raised')
        return 1
    log('PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
