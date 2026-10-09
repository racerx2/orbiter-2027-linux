# not upstream: dynamic symbols of Collision.so (Design CA E4 3.4, 7.10): exports, no STB_GNU_UNIQUE, SONAME, NEEDED, allowed imports (sanitizer runtimes included)
import argparse
import os
import re
import shutil
import subprocess
import sys

ENTRY = {'InitModule', 'ExitModule', 'ModuleDate', 'ModuleDetach', 'oapiModuleResources', 'opcLoadState', 'opcSaveState'}
WEAK = {'GetModuleVersion'}
FORBIDDEN = {'rand', 'srand', 'setlocale', 'fesetenv', 'fesetround', 'feholdexcept', 'feupdateenv', 'fesetexceptflag', 'feenableexcept', 'uselocale',
             '_ZNSt6locale6globalERKS_'}  # the last: std::locale::global
RUNTIME = ('GLIBC_', 'GLIBCXX_', 'CXXABI_', 'GCC_')
CRT_WEAK = {'_ITM_deregisterTMCloneTable', '_ITM_registerTMCloneTable', '__gmon_start__'}
STD = re.compile(r'^_Z(?:T[ISV]|GV)?Z?N?K?(?:St|9__gnu_cxx)')  # std:: or __gnu_cxx:: entities, their typeinfo, vtables, guards, local statics
NEEDED_OK = {'libstdc++.so.6', 'libm.so.6', 'libgcc_s.so.1', 'libc.so.6'}  # E4 3.4; no Qt
SANITIZER = re.compile(r'^lib(?:asan|ubsan|lsan|tsan|hwasan)\.so\.\d+$')  # a sanitizer build binds libc and operator new to these unversioned


def nm(*args):
    out = subprocess.run(['nm', '-D'] + list(args), capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2:
            rows.append((parts[-2], parts[-1]))
    return rows


def colla_exports(header):
    text = open(header, encoding='ascii').read()
    m = re.search(r'COLLA_EXPORTS\[\]\s*=\s*\{([^}]*)\}', text)
    return set(re.findall(r'"(\w+)"', m.group(1))) if m else set()


def sdk_names(sdk):
    words = set()
    for root, _, files in os.walk(sdk):
        for f in files:
            if f.endswith('.h'):
                words.update(re.findall(r'\w+', open(os.path.join(root, f), encoding='latin-1').read()))
    return words | {'InitLib', 'Date2Int', 'GImGui'}  # module glue of the SDK library (Orbitersdk.cpp), ImGui context (imconfig.h)


def find_lib(name, cxx):
    for c in (cxx, 'c++'):  # the compiler that linked the module knows its runtime
        if c and shutil.which(c):
            p = subprocess.run([c, '-print-file-name=' + name], capture_output=True, text=True).stdout.strip()
            if os.path.isabs(p) and os.path.isfile(p):
                return p
    out = subprocess.run(['ldconfig', '-p'], capture_output=True, text=True).stdout if shutil.which('ldconfig') else ''
    m = re.search(r'^\s*%s \([^)]*\) => (\S+)$' % re.escape(name), out, re.M)
    return m.group(1) if m else ''


def demangled_words(name):
    out = subprocess.run(['c++filt', name], capture_output=True, text=True).stdout.strip()
    return re.findall(r'[A-Za-z_]\w*', out.split('(')[0])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--so', required=True)
    ap.add_argument('--api', required=True)
    ap.add_argument('--exe', default='')
    ap.add_argument('--sdk', default='')
    ap.add_argument('--require-colla', action='store_true')  # from E3 step 5 on
    ap.add_argument('--cxx', default='')  # the compiler that linked the module, to find a sanitizer runtime
    a = ap.parse_args()
    if not shutil.which('nm') or not shutil.which('readelf'):
        print('skip: binutils missing')
        return 77
    errors = []

    dyn = subprocess.run(['readelf', '-d', a.so], capture_output=True, text=True, check=True, env=dict(os.environ, LC_ALL='C')).stdout
    if not re.search(r'\(SONAME\)[^\[\n]*\[Collision\.so\]', dyn):
        errors.append('SONAME is not Collision.so')
    sanitizer = set()
    for lib in re.findall(r'\(NEEDED\)[^\[\n]*\[([^\]]+)\]', dyn):
        if lib in NEEDED_OK:
            continue
        if not SANITIZER.match(lib):
            errors.append('NEEDED %s is not one of %s' % (lib, ', '.join(sorted(NEEDED_OK))))
            continue
        path = find_lib(lib, a.cxx)
        if not path:
            errors.append('sanitizer runtime %s not found' % lib)
            continue
        sanitizer |= {name for _, name in nm('--defined-only', path)}

    colla = colla_exports(a.api)
    if len(colla) != 5:
        errors.append('COLLA_EXPORTS not found in %s' % a.api)
    exports = set()
    for kind, name in nm('--defined-only', a.so):
        if kind == 'u':
            errors.append('STB_GNU_UNIQUE symbol %s (a re-tick would skip InitModule)' % name)
        elif name in ENTRY or name in colla:
            exports.add(name)
            if kind != 'T':
                errors.append('export %s has type %s' % (name, kind))
        elif name in WEAK and kind == 'W':
            pass
        elif kind in ('W', 'V') and STD.match(name):
            pass
        else:
            errors.append('unexpected export %s %s' % (kind, name))
    for name in sorted(ENTRY - exports):
        errors.append('missing export %s' % name)
    if a.require_colla:
        for name in sorted(colla - exports):
            errors.append('missing export %s' % name)

    exe = set()
    if a.exe and os.path.isfile(a.exe):
        exe = {name for _, name in nm('--defined-only', a.exe)}
    sdk = sdk_names(a.sdk) if not exe and a.sdk else set()
    if not exe and not sdk:
        errors.append('neither --exe nor --sdk to check the imports against')
    for kind, name in nm('--undefined-only', a.so):
        base, _, ver = name.partition('@')
        if base in FORBIDDEN:
            errors.append('forbidden import %s' % name)
        elif ver:
            if not ver.lstrip('@').startswith(RUNTIME):
                errors.append('import %s from an unexpected library' % name)
        elif kind == 'w' and base in CRT_WEAK:
            pass
        elif base in sanitizer:
            pass
        elif exe:
            if base not in exe:
                errors.append('import %s is not exported by %s' % (base, a.exe))
        elif not all(w in sdk for w in demangled_words(base) if w not in ('for', 'typeinfo', 'vtable', 'const', 'oapi', 'abi', 'cxx11')):
            errors.append('import %s is not declared in the SDK headers' % base)

    for e in errors:
        print('FAIL: ' + e)
    print('%s: %d exports checked, %s' % (os.path.basename(a.so), len(exports), 'ok' if not errors else '%d errors' % len(errors)))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
