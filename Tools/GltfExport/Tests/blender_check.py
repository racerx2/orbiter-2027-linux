# GltfExport check: a .glb imported into Blender against gltfx_ref's expected positions
# blender -b --python blender_check.py -- <file.glb> <ref.json> [<render folder> <animation name> <state> ...]
import bpy, json, sys, os, struct

argv = sys.argv[sys.argv.index('--') + 1:]
glb, refp = argv[0], argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb)
ref = json.load(open(refp))
scene = bpy.context.scene
fps = scene.render.fps * scene.render.fps_base  # as the importer: frame = time * fps

byid = {}
for o in bpy.data.objects:
    p = o.get('orbiter')
    if p is not None and 'group' in p:
        byid[(int(p['mesh']), int(p['group']))] = o
animated = [o for o in bpy.data.objects if o.animation_data and len(o.animation_data.nla_tracks)]
tracks = set(t.name for o in animated for t in o.animation_data.nla_tracks)
raw = open(glb, 'rb').read()
gj = json.loads(raw[20:20 + struct.unpack_from('<I', raw, 12)[0]])
anims = gj['asset']['extras']['orbiter']['animations']


def rest_frame(track):  # the frame of the animation's defstate, where its channels give the rest pose
    a = anims[int(track.split()[0])]
    lo = min([0.0] + [min(c['state0'], c['state1']) for c in a['components']])
    return (a['defstate'] - lo) * fps


def fcurves(strip):
    act = strip.action
    if hasattr(act, 'layers') and len(act.layers) and len(act.layers[0].strips):
        bag = act.layers[0].strips[0].channelbag(strip.action_slot)
        return bag.fcurves if bag else []
    return act.fcurves


def activate(name):
    for o in animated:
        ad = o.animation_data
        ad.action = None
        for t in ad.nla_tracks:
            t.mute = True
        for t in ad.nla_tracks:  # without an action the last pose would stay: each part to its own animation's rest
            if len(t.strips):
                f = rest_frame(t.name)
                for fc in fcurves(t.strips[0]):
                    getattr(o, fc.data_path)[fc.array_index] = fc.evaluate(f)
        for t in ad.nla_tracks:
            if t.name == name and len(t.strips):
                st = t.strips[0]
                ad.action = st.action
                if hasattr(ad, 'action_slot') and hasattr(st, 'action_slot') and st.action_slot:
                    ad.action_slot = st.action_slot
                break


worst = 0.0
bad = []
per = {}
for c in ref['checks']:
    activate(c['name'])
    scene.frame_set(0)
    scene.frame_set(int(round(c['time'] * fps)), subframe=(c['time'] * fps) % 1.0)
    dg = bpy.context.evaluated_depsgraph_get()
    for n in c['nodes']:
        o = byid.get((n['mesh'], n['group']))
        if o is None:
            bad.append('no object for %s' % n['name'])
            continue
        oe = o.evaluated_get(dg)
        M = oe.matrix_world
        vs = [M @ v.co for v in oe.data.vertices]
        for e in n['verts']:
            d = min(((v.x - e[0]) ** 2 + (v.y - e[1]) ** 2 + (v.z - e[2]) ** 2) ** 0.5 for v in vs)
            if d > worst:
                worst = d
            key = (c['name'], c['state'])
            per[key] = max(per.get(key, 0.0), d)
            if d > 1e-3:
                bad.append('%s at state %.3f: %s off by %.4f m' % (c['name'], c['state'], n['name'], d))

print('GLTFX_CHECK actions=%d checks=%d worst=%.6f bad=%d' % (len(tracks), len(ref['checks']), worst, len(bad)))
for k, v in sorted(per.items()):
    print('GLTFX_ANIM %s state %.3f worst %.5f' % (k[0], k[1], v))
for b in bad[:20]:
    print('GLTFX_BAD', b)

if len(argv) > 2:  # pictures: <folder> then pairs of animation name and state
    out = argv[2]
    os.makedirs(out, exist_ok=True)
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'TEXTURE'
    scene.render.resolution_x, scene.render.resolution_y = 1280, 720
    hide = set()
    for o in bpy.data.objects:  # the cockpit tree stays out of the pictures
        p = o
        while p is not None:
            if p.name == 'Cockpit':
                hide.add(o)
                break
            p = p.parent
    for o in hide:
        o.hide_render = True
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    scene.collection.objects.link(cam)
    scene.camera = cam
    cam.location = (-16.0, 14.0, 5.0)  # front left, above: Orbiter's nose is Blender's +Y
    cam.data.lens = 32
    tgt = bpy.data.objects.new('tgt', None)
    scene.collection.objects.link(tgt)
    tgt.location = (0, 0, 0)
    con = cam.constraints.new('TRACK_TO')
    con.target = tgt
    con.track_axis = 'TRACK_NEGATIVE_Z'
    con.up_axis = 'UP_Y'
    pairs = argv[3:]
    for i in range(0, len(pairs) - 1, 2):
        name, state = pairs[i], float(pairs[i + 1])
        activate(name)
        scene.frame_set(int(round(state * fps)))
        scene.render.filepath = os.path.join(out, 'shot%d.png' % (i // 2))
        bpy.ops.render.render(write_still=True)
        print('GLTFX_SHOT', scene.render.filepath, name, state)
