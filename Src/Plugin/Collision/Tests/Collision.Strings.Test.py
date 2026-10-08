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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--so', required=True)
    ap.add_argument('--category', default='Physics')
    a = ap.parse_args()
    blob = strtab(a.so)
    errors = []
    strings = {}
    if blob is None or blob[:8] != b'OAPISTR1':
        errors.append('no .oapi_strtab section')
    else:
        p = 8
        while p + 8 <= len(blob):
            sid, n = struct.unpack_from('<II', blob, p)
            strings[sid] = blob[p + 8:p + 8 + n]
            p += 8 + n
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
