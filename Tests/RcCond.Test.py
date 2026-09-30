# not upstream: rc2cpp's #if evaluator against C semantics (intmax_t/uintmax_t); RcCond.Test.py <dir of rc2cpp.py>
import os, sys, tempfile
sys.dont_write_bytecode = True  # no __pycache__ in the source tree
sys.path.insert(0, sys.argv[1])
import rc2cpp
pp = rc2cpp.Preproc([])
pp.defs.update({'A': '(B+1)', 'B': '2', 'R': '(R+1)', 'TWICE': 'B+B'})
true = ['-1 > 0u', '~0u > 0', '0xFFFFFFFFFFFFFFFF == -1', "'A' == 65", "'\\x41' == 65", "'\\101' == 65", "'\\n' == 10",
        "'\\xff' == -1", '5/2 == 2', '-7/2 == -3', '-7%2 == -1', '(-1) >> 1 == -1', '0 && 1/0 || 1', '1 || 1/0', '1 ? 2 : 1/0',
        '0 ? 1/0 : 3', '1 ? -1 : 0u', 'A == 3', 'TWICE == 4', 'defined(A) && defined B', '!defined(C)', '0x7FFFFFFFFFFFFFFF > 0',
        '18446744073709551615 == -1', '1 << 63 < 0', '1u << 63 > 0', '(0u - 1) / 2 == 0x7FFFFFFFFFFFFFFF', '2 + 3 * 4 == 14',
        '(1 ? 2 : 3) == 2', '10 - 2 - 3 == 5', '1 == 1 == 1', '-1 < 0', '~0 == -1']
false = ['0 && 1/0', "'B' == 65", '1 ? 0 : 1', 'C', '(0u - 1) < 0', '-1 > 0']
errors = ['1/0', '1 << 64', '1 << -1', '1 << 99999999999999999999', 'R', '(1', '1 ?', "'ab'", 'defined']
bad = 0
for e in true + false:
    try:
        r = pp.cond(e, 'test')
    except rc2cpp.RcError as x:
        print('ERROR', e, x); bad += 1; continue
    if r != (e in true): print('WRONG', e, r); bad += 1
for e in errors:
    try:
        pp.cond(e, 'test'); print('NO ERROR', e); bad += 1
    except rc2cpp.RcError:
        pass
    except Exception as x:
        print('TRACEBACK', e, type(x).__name__, x); bad += 1
for text, want in [('#if 1\n#else\n#else\n#endif\n', 'after #else'), ('#if 0\n#else\n#elif 1\n#endif\n', 'after #else'),
                   ('#if 0\n#if 1/0\n#endif\n#endif\n', None), ('#if 1 /* note */\n#endif\n', None)]:
    f = tempfile.NamedTemporaryFile('w', suffix='.rc', delete=False); f.write(text); f.close()
    try:
        rc2cpp.Preproc([]).run(f.name, []); got = None
    except rc2cpp.RcError as x:
        got = str(x)
    os.unlink(f.name)
    if (want is None) != (got is None) or (want and want not in got): print('DIRECTIVES', repr(text), got); bad += 1
print('checks', len(true) + len(false) + len(errors) + 4, 'bad', bad)
sys.exit(1 if bad else 0)
