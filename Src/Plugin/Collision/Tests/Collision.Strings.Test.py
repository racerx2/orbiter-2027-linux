# not upstream: the Launchpad strings of Collision.so as Orbiter reads them from the file (.oapi_strtab, Design CA E4 6, 7.10)
import argparse
import struct
import sys


def strtab(path):
    data = open(path, 'rb').read()
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        raise ValueError('not a little-endian ELF64 file')
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x3A)
    secs = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize) for i in range(shnum)]
    names = secs[shstrndx]
    for s in secs:
        n = data[names[4] + s[0]:data.index(b'\0', names[4] + s[0])].decode('latin-1')
        if n == '.oapi_strtab':
            return data[s[4]:s[4] + s[5]]
    return None


def entries(blob):  # as LoadModuleString (ResDialog.cpp): the first entry of an id wins, a truncated entry ends the table
    strings = {}
    p = 8
    while p + 8 <= len(blob):
        sid, n = struct.unpack_from('<II', blob, p)
        p += 8
        if p + n > len(blob):
            print('note: truncated entry %d at %d' % (sid, p - 8))
            break
        strings.setdefault(sid, blob[p:p + n])
        p += n
    return strings


def selftest():
    e = lambda sid, b: struct.pack('<II', sid, len(b)) + b
    got = entries(b'OAPISTR1' + e(1000, b'first') + e(1000, b'second') + e(1001, b'Physics'))
    ok = got == {1000: b'first', 1001: b'Physics'}
    got = entries(b'OAPISTR1' + e(1000, b'a') + struct.pack('<II', 1001, 99) + b'short' + e(1002, b'x'))
    ok = ok and got == {1000: b'a'}
    print('selftest: %s' % ('ok' if ok else 'FAIL'))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--so')
    ap.add_argument('--category', default='Physics')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest or not a.so:
        return selftest()
    blob = strtab(a.so)
    errors = []
    strings = {}
    if blob is None or blob[:8] != b'OAPISTR1':
        errors.append('no .oapi_strtab section')
    else:
        strings = entries(blob)
    info, cat = strings.get(1000), strings.get(1001)
    for sid, s in ((1000, info), (1001, cat)):
        if s is None:
            errors.append('string %d missing' % sid)
        elif len(s) >= 1023 or any(b > 127 for b in s):
            errors.append('string %d is not ASCII under 1023 bytes' % sid)
    if info is not None and not info.startswith(b'COLLISIONS:'):
        errors.append('string 1000 does not start with COLLISIONS:')
    if cat is not None and cat != a.category.encode('ascii'):
        errors.append('string 1001 is %r, not %r' % (cat, a.category))
    for e in errors:
        print('FAIL: ' + e)
    print('strings: %s' % ('ok' if not errors else '%d errors' % len(errors)))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
