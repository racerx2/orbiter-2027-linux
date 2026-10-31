# the export names of a PE file (a 32-bit DLL)
import struct

def exports (path):
    b = open (path, 'rb').read ()
    pe = struct.unpack_from ('<I', b, 0x3c)[0]
    nsec = struct.unpack_from ('<H', b, pe + 6)[0]
    opt = pe + 24
    secs = opt + struct.unpack_from ('<H', b, pe + 20)[0]
    rva, size = struct.unpack_from ('<II', b, opt + 96) # PE32: the export directory
    def off (r):
        for i in range (nsec):
            s = secs + 40 * i
            vsize, va, rsize, raw = struct.unpack_from ('<IIII', b, s + 8)
            if va <= r < va + max (vsize, rsize): return r - va + raw
        raise ValueError ('rva %x in no section' % r)
    if not rva: return []
    d = off (rva)
    n, names = struct.unpack_from ('<I', b, d + 24)[0], struct.unpack_from ('<I', b, d + 32)[0]
    out = []
    for i in range (n):
        p = off (struct.unpack_from ('<I', b, off (names) + 4 * i)[0])
        out.append (b[p:b.index (b'\0', p)].decode ())
    return out
