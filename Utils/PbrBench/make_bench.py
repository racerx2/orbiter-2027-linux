#!/usr/bin/env python3
# not upstream: PBR material bench generator (design-pbr 10): mesh, PNG textures, vessel and material configs, scenario
import math
import os
import struct
import sys
import zlib

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', '..'))
TEX = 256
SEG_U, SEG_V = 48, 24
RADIUS = 1.0
SPACING = 2.6


def png(path, w, h, px):  # px: rows top to bottom of (r, g, b, a), 0..255
    raw = b''.join(b'\x00' + bytes(c for p in row for c in p) for row in px)
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(data)


def clamp8(x):
    return max(0, min(255, int(round(x * 255))))


def image(fn):
    return [[tuple(clamp8(c) for c in fn(x / TEX, y / TEX)) for x in range(TEX)] for y in range(TEX)]


def checker(u, v):
    grid = (u * 32 % 1 < 0.04) or (v * 16 % 1 < 0.04)
    on = (int(u * 16) + int(v * 8)) % 2
    c = (0.95, 0.55, 0.15) if on else (0.92, 0.92, 0.9)
    if grid:
        c = (0.15, 0.15, 0.2)
    return c + (1.0,)


def metalrough(u, v):
    r = 0.15 + 0.7 * (0.5 + 0.5 * math.sin(v * math.pi * 6))
    return (0.0, r, 1.0, 1.0)


def height(u, v):  # panel lines, rivets and arrows pointing to +u (asymmetric bevels show the tangent frame)
    h = 0.0
    fu, fv = u * 8 % 1, v * 4 % 1
    if fu < 0.03 or fv < 0.03:
        h -= 1.0
    for cu in (0.12, 0.88):
        for cv in (0.15, 0.85):
            d = math.hypot((fu - cu) * 2, fv - cv)
            if d < 0.05:
                h += 0.8 * math.cos(d / 0.05 * math.pi / 2)
    au, av = fu - 0.3, fv - 0.5
    if 0 < au < 0.4 and abs(av) < 0.3 * (1 - au / 0.4):
        h += 0.6 * au / 0.4
    return h


def normalmap(u, v):  # legacy frame: x along +u, y along +v (image down)
    e = 1.0 / TEX
    du = (height(u + e, v) - height(u - e, v)) / (2 * e) * 0.004
    dv = (height(u, v + e) - height(u, v - e)) / (2 * e) * 0.004
    n = (-du, -dv, 1.0)
    l = math.sqrt(sum(c * c for c in n))
    return tuple(0.5 + 0.5 * c / l for c in n) + (1.0,)


def occlusion(u, v):
    a = 0.25 + 0.75 * min(1.0, abs(math.sin(u * math.pi * 12)) * 3) * min(1.0, abs(math.sin(v * math.pi * 6)) * 3)
    return (a, a, a, 1.0)


def emissive(u, v):
    on = (v * 12 % 1) < 0.25 or (u * 24 % 1) < 0.08
    return (0.1, 0.9, 1.0, 1.0) if on else (0.0, 0.0, 0.0, 1.0)


def mask(u, v):
    fu, fv = u * 12 % 1 - 0.5, v * 6 % 1 - 0.5
    hole = math.hypot(fu, fv) < 0.32
    return (0.2, 0.75, 0.3, 0.0 if hole else 1.0)


def fabric(u, v):
    w = 0.5 + 0.5 * math.sin(u * math.pi * 96) * math.sin(v * math.pi * 48)
    c = 0.04 + 0.06 * w
    return (c * 0.6, c * 0.7, c * 1.6, 1.0)


def aniso(u, v):  # direction along +u (tangent), full strength
    return (1.0, 0.5, 1.0, 1.0)


def iridthick(u, v):
    t = 0.5 + 0.5 * math.sin(v * math.pi * 3 + math.sin(u * math.pi * 4))
    return (0.0, t, 0.0, 1.0)


TEXTURES = [('checker', checker), ('metalrough', metalrough), ('normal', normalmap), ('occlusion', occlusion),
            ('emissive', emissive), ('mask', mask), ('fabric', fabric), ('aniso', aniso), ('iridthick', iridthick)]
TEXIDX = {n: i + 1 for i, (n, _) in enumerate(TEXTURES)}  # .msh texture numbers start at 1


def fix_winding(vtx, idx):  # clockwise front faces: cross(b-a, c-a) along the outward vertex normal
    out = []
    for t in range(0, len(idx), 3):
        a, b, c = idx[t:t + 3]
        pa, pb, pc = vtx[a][:3], vtx[b][:3], vtx[c][:3]
        e1 = [pb[k] - pa[k] for k in range(3)]
        e2 = [pc[k] - pa[k] for k in range(3)]
        cr = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        n = [vtx[a][3 + k] + vtx[b][3 + k] + vtx[c][3 + k] for k in range(3)]
        if sum(cr[k] * n[k] for k in range(3)) < 0:
            b, c = c, b
        out += [a, b, c]
    return vtx, out


def sphere(cy, cz):
    vtx, idx = [], []
    for j in range(SEG_V + 1):
        th = math.pi * j / SEG_V
        for i in range(SEG_U + 1):
            ph = 2 * math.pi * i / SEG_U
            n = (math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph))
            vtx.append((n[0] * RADIUS, cy + n[1] * RADIUS, cz + n[2] * RADIUS) + n + (i / SEG_U, j / SEG_V))
    row = SEG_U + 1
    for j in range(SEG_V):
        for i in range(SEG_U):
            a, b, c, d = j * row + i, j * row + i + 1, (j + 1) * row + i, (j + 1) * row + i + 1
            if j > 0:
                idx += [a, b, c]
            if j < SEG_V - 1:
                idx += [b, d, c]
    return fix_winding(vtx, idx)


def plate(cy, cz):
    s = RADIUS
    vtx = [(0, cy + s, cz - s, 1, 0, 0, 0, 0), (0, cy + s, cz + s, 1, 0, 0, 1, 0),
           (0, cy - s, cz - s, 1, 0, 0, 0, 1), (0, cy - s, cz + s, 1, 0, 0, 1, 1)]
    return fix_winding(vtx, [0, 1, 2, 1, 3, 2])


# (label, legacy texture, PBR config lines; None = the legacy reference)
SLOTS = [
    ('base colour', 'checker', ['PBR_METALLIC 0', 'PBR_ROUGHNESS 0.5', 'PBR_TEX BASECOLOR PbrBench\\checker.png']),
    ('alpha mask', 'mask', ['PBR_METALLIC 0', 'PBR_ROUGHNESS 0.5', 'PBR_ALPHA MASK 0.5', 'PBR_TEX BASECOLOR PbrBench\\mask.png']),
    ('alpha blend', 'checker', ['PBR_BASECOLOR 0.3 0.6 1 0.35', 'PBR_METALLIC 0', 'PBR_ROUGHNESS 0.2', 'PBR_ALPHA BLEND']),
    ('metal smooth', None, ['PBR_BASECOLOR 1 0.766 0.336 1', 'PBR_METALLIC 1', 'PBR_ROUGHNESS 0.15']),
    ('metal rough', None, ['PBR_BASECOLOR 0.91 0.92 0.92 1', 'PBR_METALLIC 1', 'PBR_ROUGHNESS 1', 'PBR_TEX METALROUGH PbrBench\\metalrough.png']),
    ('normal map', None, ['PBR_BASECOLOR 0.8 0.8 0.82 1', 'PBR_METALLIC 0', 'PBR_ROUGHNESS 0.4', 'PBR_TEX NORMAL PbrBench\\normal.png']),
    ('emission', None, ['PBR_BASECOLOR 0.05 0.05 0.05 1', 'PBR_METALLIC 0', 'PBR_ROUGHNESS 0.5', 'PBR_EMISSIVE 1 1 1 4', 'PBR_TEX EMISSIVE PbrBench\\emissive.png']),
    ('occlusion', None, ['PBR_BASECOLOR 0.9 0.9 0.9 1', 'PBR_METALLIC 0', 'PBR_ROUGHNESS 0.6', 'PBR_OCCLUSION 1', 'PBR_TEX OCCLUSION PbrBench\\occlusion.png']),
    ('ior specular', None, ['PBR_BASECOLOR 0.25 0.02 0.02 1', 'PBR_METALLIC 0', 'PBR_ROUGHNESS 0.15', 'PBR_IOR 1.9', 'PBR_SPECULAR 1', 'PBR_SPECULARCOLOR 0.5 1 0.5']),
    ('transmission volume', None, ['PBR_BASECOLOR 0.6 1 0.7 1', 'PBR_METALLIC 0', 'PBR_ROUGHNESS 0.05', 'PBR_IOR 1.5', 'PBR_TRANSMISSION 1', 'PBR_VOLUME 1 1.5 0.4 0.9 0.5']),
    ('clearcoat', None, ['PBR_BASECOLOR 0.7 0.02 0.02 1', 'PBR_METALLIC 0', 'PBR_ROUGHNESS 0.6', 'PBR_CLEARCOAT 1 0.03']),
    ('sheen', 'fabric', ['PBR_METALLIC 0', 'PBR_ROUGHNESS 0.9', 'PBR_SHEEN 0.3 0.5 1 0.4', 'PBR_TEX BASECOLOR PbrBench\\fabric.png']),
    ('anisotropy', None, ['PBR_BASECOLOR 0.9 0.9 0.92 1', 'PBR_METALLIC 1', 'PBR_ROUGHNESS 0.35', 'PBR_ANISOTROPY 0.9 0', 'PBR_TEX ANISOTROPY PbrBench\\aniso.png']),
    ('iridescence', None, ['PBR_BASECOLOR 0.05 0.05 0.05 1', 'PBR_METALLIC 0', 'PBR_ROUGHNESS 0.1', 'PBR_IRIDESCENCE 1 1.3 100 600', 'PBR_TEX IRIDESCENCETHICK PbrBench\\iridthick.png']),
    ('legacy reference', 'checker', None),
    ('double-sided plate', 'mask', ['PBR_METALLIC 0', 'PBR_ROUGHNESS 0.5', 'PBR_ALPHA MASK 0.5', 'PBR_DOUBLESIDED 1', 'PBR_TEX BASECOLOR PbrBench\\mask.png']),
]


def write_mesh(path):
    groups = []
    for k, (label, tex, _) in enumerate(SLOTS):
        cy = (1.5 - k // 4) * SPACING
        cz = (k % 4 - 1.5) * SPACING
        vtx, idx = plate(cy, cz) if label.startswith('double-sided') else sphere(cy, cz)
        groups.append((label, k + 1, TEXIDX[tex] if tex else 0, vtx, idx))
    out = ['MSHX1', 'GROUPS %d' % len(groups)]
    for label, mat, tex, vtx, idx in groups:
        out += ['LABEL %s' % label.replace(' ', '_'), 'MATERIAL %d' % mat, 'TEXTURE %d' % tex, 'GEOM %d %d' % (len(vtx), len(idx) // 3)]
        out += ['%.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f' % v for v in vtx]
        out += ['%d %d %d' % tuple(idx[t:t + 3]) for t in range(0, len(idx), 3)]
    out += ['MATERIALS %d' % len(SLOTS)] + ['m%02d' % k for k in range(len(SLOTS))]
    for k, (label, tex, _) in enumerate(SLOTS):
        d = '1 1 1 1' if tex else '0.6 0.6 0.62 1'
        out += ['MATERIAL m%02d' % k, d, '0.6 0.6 0.62 1', '0.2 0.2 0.2 1 20', '0 0 0 1']
    out += ['TEXTURES %d' % len(TEXTURES)] + ['PbrBench\\%s.png' % n for n, _ in TEXTURES]
    with open(path, 'w', newline='\r\n') as f:
        f.write('\n'.join(out) + '\n')


def write_gc(path):
    out = ['; not upstream: PBR material bench materials (design-pbr 9), made by Utils/PbrBench/make_bench.py', 'CONFIG_VERSION 3',
           '; =============================================', 'MESH PbrBench\\PbrBench']
    for k, (label, _, pbr) in enumerate(SLOTS):
        if pbr is None:
            continue
        out += ['; --------------------------------------------- %s' % label, 'MATERIAL %d' % k, 'PBR'] + pbr
    with open(path, 'w') as f:
        f.write('\n'.join(out) + '\n')


def write_vessel(path):
    with open(path, 'w') as f:
        f.write('; not upstream: PBR material bench (design-pbr 10), made by Utils/PbrBench/make_bench.py\n'
                'ClassName = PbrBench\nMeshname = PbrBench\\PbrBench\nMass = 1000\nSize = 6.0\n'
                'Inertia = 5.0 5.0 5.0\nCrossSections = 40.0 40.0 40.0\nEnableFocus = TRUE\nEnableXPDR = FALSE\n')


def write_scenario(path):
    r = 6778137.0
    v = math.sqrt(6.67259e-11 * 5.973698968e+24 / r)
    order = ', '.join('%d %s' % (k + 1, s[0]) for k, s in enumerate(SLOTS))
    with open(path, 'w') as f:
        f.write('BEGIN_DESC\nPBR material bench (custom branch test data): one sphere per glTF material feature, rows of four from the top: %s.\nEND_DESC\n\n'
                'BEGIN_ENVIRONMENT\n  System Sol\n  Date MJD 51982.5\nEND_ENVIRONMENT\n\n'
                'BEGIN_FOCUS\n  Ship Bench\nEND_FOCUS\n\n'
                'BEGIN_CAMERA\n  TARGET Bench\n  MODE Extern\n  POS 2.60 90.00 0.00\n  TRACKMODE TargetRelative\n  FOV 40.00\nEND_CAMERA\n\n'
                'BEGIN_SHIPS\nBench:PbrBench\n  STATUS Orbiting Earth\n  RPOS %.3f 0 0\n  RVEL 0 0 %.6f\n  AROT 0 0 0\nEND\nEND_SHIPS\n' % (order, r, v))


def main():
    dirs = {k: os.path.join(ROOT, *p) for k, p in (('mesh', ('Meshes', 'PbrBench')), ('tex', ('Textures', 'PbrBench')),
                                                    ('ves', ('Config', 'Vessels')), ('gc', ('Config', 'GC')), ('scn', ('Scenarios', 'Graphics')))}
    for d in dirs.values():
        os.makedirs(d, exist_ok=True)
    for n, fn in TEXTURES:
        png(os.path.join(dirs['tex'], n + '.png'), TEX, TEX, image(fn))
    write_mesh(os.path.join(dirs['mesh'], 'PbrBench.msh'))
    write_gc(os.path.join(dirs['gc'], 'PbrBench.cfg'))
    write_vessel(os.path.join(dirs['ves'], 'PbrBench.cfg'))
    write_scenario(os.path.join(dirs['scn'], 'PBR material bench.scn'))
    print('bench written under', ROOT)


if __name__ == '__main__':
    main()
