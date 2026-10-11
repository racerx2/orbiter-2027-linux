# not upstream: writes GltfTest/nestedglass.glb for Gltf.Visual.NestedGlass (claude/design-pbr-nested-glass.md 7)
# a transmission box (canopy) holding a green transmission HUD in front of an emissive backdrop, a red BLEND decal on its front,
# the same box and HUD with alpha BLEND on the left, and a reference quad outside
import json, os, struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'GltfTest', 'nestedglass.glb')
GLASS = {'KHR_materials_transmission': {'transmissionFactor': 1.0}, 'KHR_materials_ior': {'ior': 1.0}}
NOREFL = {'KHR_materials_ior': {'ior': 1.0}}


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def quad(c, u, v):  # glTF frame; counterclockwise seen from u x v, the face normal
    n = cross(u, v)
    l = sum(x * x for x in n) ** 0.5
    n = tuple(x / l for x in n)
    p = [tuple(c[i] + su * u[i] + sv * v[i] for i in range(3)) for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    return p, [n] * 4


def box(lo, hi):  # glTF frame, outward faces
    c = tuple((a + b) / 2 for a, b in zip(lo, hi))
    h = tuple((b - a) / 2 for a, b in zip(lo, hi))
    X, Y, Z = (h[0], 0, 0), (0, h[1], 0), (0, 0, h[2])
    neg = lambda a: tuple(-x for x in a)
    add = lambda a, b: tuple(x + y for x, y in zip(a, b))
    faces = [(add(c, X), Y, Z), (add(c, neg(X)), Z, Y), (add(c, Y), Z, X), (add(c, neg(Y)), X, Z), (add(c, Z), X, Y), (add(c, neg(Z)), Y, X)]
    pos, nrm = [], []
    for fc, u, v in faces:
        p, n = quad(fc, u, v)
        pos += p
        nrm += n
    return pos, nrm


def front(x0, x1, y0, y1, z):  # Orbiter quad at z facing Orbiter -z: glTF z = -z, normal glTF +z
    return quad(((x0 + x1) / 2, (y0 + y1) / 2, -z), ((x1 - x0) / 2, 0, 0), (0, (y1 - y0) / 2, 0))


# canopy: Orbiter z -0.6 .. 1.4 is glTF z -1.4 .. 0.6
parts = [
    ('canopy', box((-0.6, -0.45, -1.4), (0.6, 0.45, 0.6)), 0),
    ('hud', front(-0.2, 0.2, -0.15, 0.15, 0.0), 1),
    ('backdrop', front(-0.45, 0.45, -0.40, 0.40, 0.3), 2),
    ('reference', front(-0.3, 0.3, 0.75, 1.0, 0.0), 2),
    ('decal', front(0.3, 0.5, -0.35, -0.2, -0.602), 5),
    ('canopy2', box((-2.2, -0.45, -1.4), (-1.0, 0.45, 0.6)), 3),
    ('hud2', front(-1.8, -1.4, -0.15, 0.15, 0.0), 4),
    ('backdrop2', front(-2.05, -1.15, -0.40, 0.40, 0.3), 2),
]
materials = [
    {'name': 'canopy', 'pbrMetallicRoughness': {'baseColorFactor': [0.3, 0.3, 0.3, 1], 'metallicFactor': 0, 'roughnessFactor': 0}, 'extensions': GLASS},
    {'name': 'hud', 'pbrMetallicRoughness': {'baseColorFactor': [0.002, 1, 0, 1], 'metallicFactor': 0, 'roughnessFactor': 0}, 'extensions': GLASS},
    {'name': 'emissive', 'pbrMetallicRoughness': {'baseColorFactor': [0, 0, 0, 1], 'metallicFactor': 0, 'roughnessFactor': 1}, 'emissiveFactor': [1, 1, 1], 'extensions': NOREFL},
    {'name': 'canopy2', 'alphaMode': 'BLEND', 'pbrMetallicRoughness': {'baseColorFactor': [0.3, 0.3, 0.3, 0.8], 'metallicFactor': 0, 'roughnessFactor': 0}, 'extensions': GLASS},
    {'name': 'hud2', 'alphaMode': 'BLEND', 'pbrMetallicRoughness': {'baseColorFactor': [0.002, 1, 0, 0.9], 'metallicFactor': 0, 'roughnessFactor': 0}, 'extensions': GLASS},
    {'name': 'decal', 'alphaMode': 'BLEND', 'pbrMetallicRoughness': {'baseColorFactor': [0, 0, 0, 0.8], 'metallicFactor': 0, 'roughnessFactor': 1}, 'emissiveFactor': [1, 0, 0], 'extensions': NOREFL},
]

bin_, views, accs, meshes, nodes = b'', [], [], [], []
for name, (pos, nrm), mat in parts:
    idx = [q * 4 + k for q in range(len(pos) // 4) for k in (0, 1, 2, 0, 2, 3)]
    for data, typ, ctype, fmt in ((pos, 'VEC3', 5126, 'f'), (nrm, 'VEC3', 5126, 'f'), (idx, 'SCALAR', 5123, 'H')):
        flat = [x for v in data for x in v] if typ == 'VEC3' else data
        raw = struct.pack('<%d%s' % (len(flat), fmt), *flat)
        while len(raw) % 4: raw += b'\0'
        views.append({'buffer': 0, 'byteOffset': len(bin_), 'byteLength': len(raw)})
        a = {'bufferView': len(views) - 1, 'componentType': ctype, 'count': len(data), 'type': typ}
        if data is pos:
            a['min'] = [round(min(v[i] for v in pos), 6) for i in range(3)]
            a['max'] = [round(max(v[i] for v in pos), 6) for i in range(3)]
        accs.append(a)
        bin_ += raw
    n = len(accs)
    meshes.append({'name': name, 'primitives': [{'attributes': {'POSITION': n - 3, 'NORMAL': n - 2}, 'indices': n - 1, 'material': mat}]})
    nodes.append({'name': name, 'mesh': len(meshes) - 1})

doc = {'asset': {'version': '2.0', 'generator': 'make_nestedglass.py'}, 'extensionsUsed': ['KHR_materials_transmission', 'KHR_materials_ior'],
       'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}], 'nodes': nodes, 'meshes': meshes, 'materials': materials,
       'buffers': [{'byteLength': len(bin_)}], 'bufferViews': views, 'accessors': accs}
js = json.dumps(doc, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
body = struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(bin_), 0x004E4942) + bin_
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 12 + len(body)) + body)
print('wrote %s (%d bytes)' % (OUT, 12 + len(body)))
