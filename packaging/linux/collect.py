#!/usr/bin/env python3
# not upstream: makes an installed Orbiter tree portable: bundles Qt and the non-system libraries, sets RUNPATHs, writes qt.conf and deps.list
import argparse, os, re, shutil, subprocess, sys, json

SYSTEM = re.compile(r'^(' + '|'.join([
    r'ld-linux-x86-64', r'lib(c|m|dl|pthread|rt|resolv|util|anl|nsl)', r'lib(stdc\+\+|gcc_s)',
    r'lib(GL|EGL|GLX|GLdispatch|OpenGL|GLESv2|gbm|drm)', r'libvulkan',
    r'lib(X11|X11-xcb|Xau|Xdmcp|Xext|Xrender|Xi|Xfixes|Xcursor|Xrandr|Xinerama|Xcomposite|Xdamage|Xss|Xtst|SM|ICE)',
    r'libxcb(-(dri2|dri3|glx|present|randr|render|shape|shm|sync|xfixes|xinerama|xinput|xkb|composite|damage|dpms|res|screensaver|xtest|xv|xvmc|record))?',
    r'libwayland-(client|cursor|egl|server)', r'libxkbcommon(-x11)?',
    r'lib(fontconfig|freetype|harfbuzz|graphite2|brotlicommon|brotlidec|brotlienc|png16|z|zstd|bz2|lzma|lz4|expat|uuid|ffi)',
    r'lib(pcre2-8|pcre2-16|selinux|mount|blkid|cap|gcrypt|gpg-error|systemd|udev|dbus-1|asound|pipewire-0\.3)',
    r'lib(glib|gthread|gobject|gio|gmodule)-2\.0', r'lib(gssapi_krb5|krb5|k5crypto|krb5support|com_err|keyutils|ssl|crypto)',
]) + r')\.so(\.|$)')
NEVER_BUNDLE = re.compile(r'^lib(ssl|crypto|vulkan|stdc\+\+|gcc_s|c|GL|EGL)\.so')
PLUGINS = {  # Qt plugin types and files the package needs (no sql drivers, print support, egl device integrations, gtk theme)
    'platforms': ['libqxcb.so', 'libqwayland-egl.so', 'libqwayland-generic.so', 'libqoffscreen.so', 'libqminimal.so'],
    'xcbglintegrations': None, 'wayland-decoration-client': None, 'wayland-graphics-integration-client': None,
    'wayland-shell-integration': None, 'platforminputcontexts': None, 'imageformats': None, 'iconengines': None, 'tls': None,
    'networkinformation': None, 'platformthemes': ['libqxdgdesktopportal.so'],
}
QML_START = ['QtQml', 'QtQml/Models', 'QtQml/WorkerScript', 'QtQuick', 'QtQuick/Window', 'QtQuick/Layouts', 'QtQuick/Shapes', 'QtQuick/Effects', 'QtQuick/Particles']  # LauncherQml.cpp BuildAllowDir
APT = {  # system sonames -> Debian/Ubuntu packages (22.04 to 26.04, Debian 12/13); a|b: the t64 renames
    'libvulkan.so.1': 'libvulkan1', 'libX11.so.6': 'libx11-6', 'libX11-xcb.so.1': 'libx11-xcb1', 'libxcb.so.1': 'libxcb1',
    'libxkbcommon.so.0': 'libxkbcommon0', 'libxkbcommon-x11.so.0': 'libxkbcommon-x11-0', 'libwayland-client.so.0': 'libwayland-client0', 'libwayland-cursor.so.0': 'libwayland-cursor0',
    'libwayland-egl.so.1': 'libwayland-egl1', 'libfontconfig.so.1': 'libfontconfig1', 'libfreetype.so.6': 'libfreetype6',
    'libEGL.so.1': 'libegl1', 'libGL.so.1': 'libgl1', 'libGLX.so.0': 'libglx0', 'libOpenGL.so.0': 'libopengl0',
    'libdbus-1.so.3': 'libdbus-1-3', 'libglib-2.0.so.0': 'libglib2.0-0t64|libglib2.0-0', 'libgthread-2.0.so.0': 'libglib2.0-0t64|libglib2.0-0',
    'libgobject-2.0.so.0': 'libglib2.0-0t64|libglib2.0-0', 'libgio-2.0.so.0': 'libglib2.0-0t64|libglib2.0-0',
    'libgssapi_krb5.so.2': 'libgssapi-krb5-2', 'libpng16.so.16': 'libpng16-16t64|libpng16-16', 'libz.so.1': 'zlib1g', 'libzstd.so.1': 'libzstd1',
    'libSM.so.6': 'libsm6', 'libICE.so.6': 'libice6', 'libxcb-randr.so.0': 'libxcb-randr0', 'libxcb-render.so.0': 'libxcb-render0',
    'libxcb-shape.so.0': 'libxcb-shape0', 'libxcb-shm.so.0': 'libxcb-shm0', 'libxcb-sync.so.1': 'libxcb-sync1', 'libxcb-xfixes.so.0': 'libxcb-xfixes0',
    'libxcb-xkb.so.1': 'libxcb-xkb1', 'libxcb-xinput.so.0': 'libxcb-xinput0', 'libxcb-glx.so.0': 'libxcb-glx0', 'libXext.so.6': 'libxext6',
    'libXrender.so.1': 'libxrender1', 'libXi.so.6': 'libxi6', 'libXfixes.so.3': 'libxfixes3', 'libXcursor.so.1': 'libxcursor1',
    'libXrandr.so.2': 'libxrandr2', 'libdrm.so.2': 'libdrm2', 'libgbm.so.1': 'libgbm1', 'libexpat.so.1': 'libexpat1',
    'libssl.so.3': 'libssl3t64|libssl3', 'libcrypto.so.3': 'libssl3t64|libssl3', 'libpipewire-0.3.so.0': 'libpipewire-0.3-0t64|libpipewire-0.3-0',
}
PACMAN = {'libvulkan': 'vulkan-icd-loader', 'libxkbcommon-x11': 'libxkbcommon-x11', 'libX11': 'libx11', 'libxcb': 'libxcb', 'libxkbcommon': 'libxkbcommon', 'libwayland': 'wayland',
          'libfontconfig': 'fontconfig', 'libfreetype': 'freetype2', 'libEGL': 'libglvnd', 'libGL': 'libglvnd', 'libOpenGL': 'libglvnd',
          'libdbus': 'dbus', 'libglib': 'glib2', 'libgthread': 'glib2', 'libgobject': 'glib2', 'libgio': 'glib2', 'libgssapi': 'krb5',
          'libpng': 'libpng', 'libz.': 'zlib', 'libzstd': 'zstd', 'libSM': 'libsm', 'libICE': 'libice', 'libXext': 'libxext',
          'libXrender': 'libxrender', 'libXi.': 'libxi', 'libXfixes': 'libxfixes', 'libXcursor': 'libxcursor', 'libXrandr': 'libxrandr',
          'libdrm': 'libdrm', 'libgbm': 'mesa', 'libexpat': 'expat', 'libssl': 'openssl', 'libcrypto': 'openssl', 'libpipewire': 'libpipewire'}
MAXVER = {'GLIBC': (2, 34), 'GLIBCXX': (3, 4, 29), 'CXXABI': (1, 3, 13)}


def run(*a):
    return subprocess.run(a, capture_output=True, text=True).stdout


def is_elf(p):
    try:
        with open(p, 'rb') as f:
            return f.read(4) == b'\x7fELF'
    except OSError:
        return False


def elfs(root):
    for d, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(d, f)
            if not os.path.islink(p) and is_elf(p):
                yield p


def needed(p):
    return re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', run('readelf', '-d', p))


def ldconfig():
    m = {}
    for l in run('ldconfig', '-p').splitlines():
        r = re.match(r'\s+(\S+) \(libc6,x86-64[^)]*\) => (\S+)', l)
        if r and r.group(1) not in m:
            m[r.group(1)] = r.group(2)
    return m


def qml_modules(qml):
    queue, out = list(QML_START), []
    while queue:
        m = queue.pop(0)
        if m in out:
            continue
        q = os.path.join(qml, m, 'qmldir')
        if not os.path.isfile(q):
            continue
        out.append(m)
        for l in open(q, encoding='utf-8', errors='replace'):
            w = l.split()
            if w and w[0] in ('optional', 'default'):
                w = w[1:]
            if len(w) >= 2 and w[0] in ('import', 'depends'):
                queue.append(w[1].replace('.', '/'))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--qt', required=True)
    ap.add_argument('--deps', required=True)
    a = ap.parse_args()
    R, QT = os.path.abspath(a.root), os.path.abspath(a.qt)
    LIB = os.path.join(R, 'lib')
    QTR = os.path.join(LIB, 'qt6')
    os.makedirs(LIB, exist_ok=True)
    fromqt = set()
    for t, files in PLUGINS.items():  # Qt plugins
        s = os.path.join(QT, 'plugins', t)
        if not os.path.isdir(s):
            continue
        d = os.path.join(QTR, 'plugins', t)
        os.makedirs(d, exist_ok=True)
        for f in sorted(os.listdir(s)):
            if f.endswith('.so') and (files is None or f in files):
                shutil.copy2(os.path.join(s, f), d)
                fromqt.add(os.path.join(d, f))
    mods = qml_modules(os.path.join(QT, 'qml'))  # QML modules the launcher skins may import
    for m in mods:
        s, d = os.path.join(QT, 'qml', m), os.path.join(QTR, 'qml', m)
        os.makedirs(d, exist_ok=True)
        for f in sorted(os.listdir(s)):
            if os.path.isfile(os.path.join(s, f)):
                shutil.copy2(os.path.join(s, f), d)
                if f.endswith('.so'):
                    fromqt.add(os.path.join(d, f))
    lay = os.path.join(a.deps, 'lib64', 'libVkLayer_khronos_shader_object.so')  # Khronos' shader-object layer for drivers without VK_EXT_shader_object
    if os.path.isfile(lay):
        vd = os.path.join(LIB, 'vulkan')
        os.makedirs(vd, exist_ok=True)
        shutil.copy2(lay, vd)
        j = json.load(open(os.path.join(a.deps, 'share', 'vulkan', 'explicit_layer.d', 'VkLayer_khronos_shader_object.json')))
        j['layer']['library_path'] = './libVkLayer_khronos_shader_object.so'
        json.dump(j, open(os.path.join(vd, 'VkLayer_khronos_shader_object.json'), 'w'), indent=4)
    else:
        sys.exit('collect: no shader-object layer in %s' % a.deps)
    own = {}
    for p in elfs(R):
        own.setdefault(os.path.basename(p), p)
    ld = ldconfig()
    sysdeps, todo, seen, bad = set(), list(elfs(R)), set(), []
    while todo:
        p = todo.pop()
        if p in seen:
            continue
        seen.add(p)
        for n in needed(p):
            if n in own:
                continue
            if SYSTEM.match(n):
                sysdeps.add(n)
                continue
            src = os.path.join(QT, 'lib', n)
            if not os.path.exists(src):
                src = ld.get(n) or next((os.path.join(x, n) for x in (os.path.join(a.deps, 'lib64'), os.path.join(a.deps, 'lib')) if os.path.exists(os.path.join(x, n))), None)
            if not src:
                bad.append('%s needs %s: not found' % (os.path.relpath(p, R), n))
                continue
            if NEVER_BUNDLE.match(n):
                bad.append('%s would be bundled (%s)' % (n, os.path.relpath(p, R)))
                continue
            dst = os.path.join(LIB, n)
            shutil.copy2(os.path.realpath(src), dst)
            os.chmod(dst, 0o755)
            if src.startswith(QT):
                fromqt.add(dst)
            own[n] = dst
            todo.append(dst)
    if bad:
        sys.exit('collect:\n  ' + '\n  '.join(sorted(set(bad))))
    for p in elfs(R):  # RUNPATH: the project's own entries, then lib/ relative to the file
        rel, top = os.path.relpath(LIB, os.path.dirname(p)), os.path.relpath(R, os.path.dirname(p))
        mine = ':'.join('$ORIGIN' if x == '.' else '$ORIGIN/' + x for x in ([rel] if p in fromqt or os.path.dirname(p) == LIB else [rel, top]))  # the package root too: lua.so for the SDK's Lua modules
        old = run('patchelf', '--print-rpath', p).strip()
        keep = [] if p in fromqt or os.path.dirname(p) == LIB else [x for x in old.split(':') if x.startswith('$ORIGIN')]
        new = ':'.join(dict.fromkeys(keep + [mine]))
        if new != old:
            r = subprocess.run(['patchelf', '--set-rpath', new, p], capture_output=True, text=True)
            if r.returncode:
                sys.exit('collect: patchelf %s: %s' % (p, r.stderr))
    conf = '[Paths]\nPrefix=%s\nLibraries=%s\nPlugins=plugins\nQmlImports=qml\n'
    for p in elfs(R):  # qt.conf next to every program that links Qt
        if os.access(p, os.X_OK) and not p.endswith('.so') and '.so.' not in os.path.basename(p) and any(n.startswith('libQt6') for n in needed(p)):
            d = os.path.dirname(p)
            with open(os.path.join(d, 'qt.conf'), 'w') as f:
                f.write(conf % (os.path.relpath(QTR, d), os.path.relpath(LIB, QTR)))
    worst = {}
    for p in elfs(R):  # symbol versions against the baseline
        for k, v in re.findall(r'\b(GLIBC|GLIBCXX|CXXABI)_([0-9.]+)\b', run('objdump', '-T', p)):
            t = tuple(int(x) for x in v.split('.'))
            if t > MAXVER[k]:
                bad.append('%s needs %s_%s' % (os.path.relpath(p, R), k, v))
            worst[k] = max(worst.get(k, ()), t)
    if bad:
        sys.exit('collect: newer than the baseline:\n  ' + '\n  '.join(sorted(set(bad))[:40]))
    sysdeps.add('libpipewire-0.3.so.0')  # XRSound loads it with dlopen
    with open(os.path.join(R, 'deps.list'), 'w') as f:  # the OpenOrbiter launcher's list: only what the system provides
        f.write('# What the OpenOrbiter launcher checks and installs, made by packaging/linux/collect.py (portable package)\n')
        f.write('built_on AlmaLinux 9 (portable)\n')
        f.write('glibc %s\n' % '.'.join(map(str, worst.get('GLIBC', (2, 17)))))
        lines = set()
        for n in sorted(sysdeps):
            if re.match(r'^(ld-linux|lib(c|m|dl|pthread|rt|resolv|util|stdc\+\+|gcc_s)\.so)', n):
                continue
            lines.add('rpm %s()(64bit)' % n)
            if n in APT:
                lines.add('apt %s' % APT[n])
            pm = next((v for k, v in PACMAN.items() if n.startswith(k)), None)
            if pm:
                lines.add('pacman %s' % pm)
        f.write(''.join(l + '\n' for l in sorted(lines)))
    print('collect: %d bundled libraries, %d system libraries, %d QML modules, max %s' % (
        len([x for x in os.listdir(LIB) if '.so' in x]), len(sysdeps), len(mods),
        ', '.join('%s_%s' % (k, '.'.join(map(str, v))) for k, v in sorted(worst.items()))))


main()
