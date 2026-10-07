# not upstream: optional check that the addon compiles against the Orbiter 2024 SDK headers (Design CA E4 5.2, 7.10); clang in mingw mode, not MSVC
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

NAMES = ('ImGuiDialog', 'oapiOpenDialog', 'oapiAddNotification')  # absent in 2024, behind COLL_HAVE_IMGUI
WRAPPERS = ('CollOpenDialog', 'imgui.h')  # CollPlatform.h wrappers and the header of those names
CONSEQUENCES = ("only virtual member functions can be marked 'override'",)


def clang_cmd(clang, a, extra, nominmax=True):
    return [clang, '--target=x86_64-w64-mingw32', '-fms-extensions', '-std=c++20', '-fsyntax-only', '-ferror-limit=0',
            '-DCOLL_ADDON_VERSION="0.0.0"', '-I' + a.src, '-I' + a.headers] + (['-DNOMINMAX'] if nominmax else []) + extra


def errors(out):
    found = []
    for m in re.finditer(r'^(.+?):(\d+):\d+: (?:fatal )?error: (.*)$', out, re.M):
        path, line, msg = m.group(1), int(m.group(2)), m.group(3)
        try:
            text = open(path, encoding='latin-1').read().splitlines()[line - 1]
        except (OSError, IndexError):
            text = ''
        found.append((path, line, msg, text))
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--headers', required=True)  # tag-2024 Orbitersdk/include; on a case-sensitive disk lua/ must resolve (a link to Lua/)
    ap.add_argument('--src', required=True)
    ap.add_argument('--imgui', default='')  # a folder with imgui.h for the negative case
    ap.add_argument('--clang', default='clang++')
    a = ap.parse_args()
    if not shutil.which(a.clang):
        print('skip: %s missing' % a.clang)
        return 77
    probe = subprocess.run(clang_cmd(a.clang, a, ['-x', 'c++', '-']), input='#include <windows.h>\n', capture_output=True, text=True)
    if probe.returncode:
        print('skip: no mingw-w64 headers for clang')
        return 77
    if not os.path.isfile(os.path.join(a.headers, 'OrbiterAPI.h')) or not os.path.isfile(os.path.join(a.headers, 'lua', 'lua.h')):
        print('FAIL: %s is not an Orbiter 2024 Orbitersdk/include with lua/lua.h' % a.headers)
        return 1
    fails = []

    macro = subprocess.run(clang_cmd(a.clang, a, ['-x', 'c++', '-']), input='#include "CollPlatform.h"\nstatic_assert (COLL_HAVE_IMGUI == 0);\n',
                           capture_output=True, text=True)
    if macro.returncode:
        fails.append('COLL_HAVE_IMGUI is not 0 with these headers:\n' + macro.stderr)

    sources = sorted(glob.glob(os.path.join(a.src, '*.cpp')))
    hit = set()
    ok = 0
    for src in sources:
        r = subprocess.run(clang_cmd(a.clang, a, [src]), capture_output=True, text=True)
        if r.returncode:
            fails.append('%s does not compile:\n%s' % (os.path.basename(src), r.stderr))
        else:
            ok += 1
        extra = ['-DCOLL_HAVE_IMGUI=1'] + (['-I' + a.imgui] if a.imgui else [])
        n = subprocess.run(clang_cmd(a.clang, a, extra + [src]), capture_output=True, text=True)
        for path, line, msg, text in errors(n.stderr):
            names = [k for k in NAMES + WRAPPERS if k in msg or k in text]
            hit.update(k for k in names if k in NAMES)
            if not names and msg not in CONSEQUENCES:
                fails.append('%s:%d: forced COLL_HAVE_IMGUI fails outside the ImGui and notification names: %s' % (os.path.basename(path), line, msg))
    if not hit:
        fails.append('forcing COLL_HAVE_IMGUI=1 broke nothing: the macro guards no 2024 difference')
    api = os.path.join(a.src, 'Tests', 'CollisionAPI.Check.cpp')  # the public header as a Windows vessel module includes it, with or without NOMINMAX
    api_ok = 0
    for nominmax in (True, False):
        r = subprocess.run(clang_cmd(a.clang, a, [api], nominmax), capture_output=True, text=True)
        api_ok += not r.returncode
        if r.returncode:
            fails.append('CollisionAPI.h does not compile%s:\n%s' % ('' if nominmax else ' without NOMINMAX', r.stderr))
    windres = shutil.which('x86_64-w64-mingw32-windres')
    for rc in sorted(glob.glob(os.path.join(a.src, '*.rc'))) if windres else []:
        r = subprocess.run([windres, '-I' + a.src, '-I' + a.headers, rc, '-O', 'coff', '-o', os.devnull], capture_output=True, text=True)
        if r.returncode:
            fails.append('%s does not compile with windres:\n%s' % (os.path.basename(rc), r.stderr))

    for f in fails:
        print('FAIL: ' + f)
    print('%d of %d sources compile against %s, CollisionAPI.h %d of 2 (resources: %s); forced macro fails at: %s; not used yet: %s' % (
        ok, len(sources), a.headers, api_ok, 'windres' if windres else 'not checked', ', '.join(sorted(hit)) or '-', ', '.join(k for k in NAMES if k not in hit) or '-'))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
