"""Render three nominal exterior studies around the unchanged V4 electronics.

Blender 5.2; pass --assembly PATH --output PATH after --.
These skins are visual studies, not manufacturing geometry or new V4 parts.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--assembly', type=Path, required=True)
parser.add_argument('--output', type=Path, default=ROOT / '2026-10-27')
parser.add_argument('--only', choices=['a', 'b', 'c'])
parser.add_argument('--views', nargs='+', choices=['hero', 'rear', 'top'], default=['hero', 'rear', 'top'])
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
args.output.mkdir(parents=True, exist_ok=True)
PARAMETERS = ROOT.parent / 'v4/v4-parameters.json'
P = json.loads(PARAMETERS.read_text(encoding='utf8'))
Q = {k: v['value'] for k, v in P['case'].items()}
BOARD = {k: v['value'] for k, v in P['full_board'].items()}
PCB = 14.6
STUDIES = {
    'a': dict(name='Studio / Schulter', ramp=5.5, profile='linear',
              shell=(.018, .023, .029), paper=(.66, .61, .51), accent=(.17, .45, .48)),
    'b': dict(name='Pebble / weiche Schale', ramp=11, profile='smooth',
              shell=(.17, .27, .19), paper=(.78, .70, .52), accent=(.11, .17, .15)),
    'c': dict(name='Konsole / facettierte Flanken', ramp=9, profile='facet',
              shell=(.035, .052, .073), paper=(.60, .64, .65), accent=(.83, .20, .055)),
}


def material(name, rgb, metal=0, rough=.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    shader = m.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (*rgb, 1)
    shader.inputs['Metallic'].default_value = metal
    shader.inputs['Roughness'].default_value = rough
    return m


def assign(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)


def cube(name, bounds):
    x0, y0, z0, x1, y1, z1 = bounds
    bpy.ops.mesh.primitive_cube_add(size=1, location=((x0+x1)/2, -(y0+y1)/2, PCB+(z0+z1)/2))
    o = bpy.context.object
    o.name = name
    o.dimensions = (x1-x0, y1-y0, z1-z0)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return o


def cylinder(name, x, y, z, radius, depth, axis='z'):
    bpy.ops.mesh.primitive_cylinder_add(vertices=48, radius=radius, depth=depth,
                                       location=(x, -y, PCB+z))
    o = bpy.context.object
    o.name = name
    if axis == 'x':
        o.rotation_euler.y = math.pi/2
    if axis == 'y':
        o.rotation_euler.x = math.pi/2
    return o


def subtract(target, cutters):
    bpy.ops.object.select_all(action='DESELECT')
    for cutter in cutters:
        cutter.select_set(True)
    bpy.context.view_layer.objects.active = cutters[0]
    bpy.ops.object.join()
    cutter = bpy.context.object
    bpy.context.view_layer.objects.active = target
    mod = target.modifiers.new('Nominal access openings', 'BOOLEAN')
    mod.operation = 'DIFFERENCE'
    mod.solver = 'EXACT'
    mod.use_self = True
    mod.object = cutter
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter, do_unlink=True)


def distance_rect(x, y, rect):
    x0, y0, x1, y1 = rect
    return math.hypot(max(x0-x, 0, x-x1), max(y0-y, 0, y-y1))


def height(x, y, style):
    # Original hood footprints are the minimum high regions. Added connecting
    # regions group the separate covers into one rear-and-side shoulder.
    rectangles = [(-4, -8.8, 199, 0), (-4, -8.8, 77, 36),
                  (128, -8.8, 199, 36), (-4, 32, 18, 64),
                  (-4, 61, 55.7, 86.5), (177.4, 7.5, 199, 81.7)]
    d = min(distance_rect(x, y, r) for r in rectangles)
    if style['profile'] == 'facet':
        d = min(max(r[0]-x, 0, x-r[2]) + max(r[1]-y, 0, y-r[3]) for r in rectangles)
    f = max(0, 1-d/style['ramp'])
    if style['profile'] == 'smooth':
        f = f*f*(3-2*f)
    # Keep the original CV bay and control field at the fixed plate level.
    for rect in [(77.2, 4, 127.8, 43.5), (50, 45.3, 174.5, 69.5),
                 (60, 72, 165, 111.5), (21, 89, 59, 111)]:
        if distance_rect(x, y, rect) == 0:
            f = 0
    crown = 24.4
    if style['profile'] == 'smooth':
        crown += 2 * math.sin(math.pi*(x+4)/203)**2
    if style['profile'] == 'facet':
        crown += 3 * max(0, 1-(y+8.8)/95)
    return 9 + (crown-9)*f


def skin(style, mat):
    # Closed roof skin: same physical right-handed frame as the V4 STLs.
    nx, ny = 204, 130
    verts, faces = [], []
    for underside in [False, True]:
        for j in range(ny+1):
            y = -8.8 + 128.6*j/ny
            for i in range(nx+1):
                x = -4 + 203*i/nx
                h = height(x, y, style) - (2.4 if underside else 0)
                if underside and (i <= 2 or i >= nx-2 or j <= 2 or j >= ny-2):
                    h = 6.6
                verts.append((x, -y, PCB+h))
    n = (nx+1)*(ny+1)
    for j in range(ny):
        for i in range(nx):
            a = j*(nx+1)+i
            b, c, d = a+1, a+nx+2, a+nx+1
            faces.extend([(a, d, c, b), (a+n, b+n, c+n, d+n)])
    edges = list(range(nx+1))
    edges += [j*(nx+1)+nx for j in range(1, ny+1)]
    edges += [ny*(nx+1)+i for i in range(nx-1, -1, -1)]
    edges += [j*(nx+1) for j in range(ny-1, 0, -1)]
    for a, b in zip(edges, edges[1:]+edges[:1]):
        faces.append((a, b, b+n, a+n))
    mesh = bpy.data.meshes.new('Nominal continuous shoulder')
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new('Exterior study - NOT FOR PRINT', mesh)
    bpy.context.collection.objects.link(obj)
    assign(obj, mat)
    # Use the actual V4 carrier as the starting inner structure. The added
    # skin is a visual shell; its attachment and final plate contours are open.
    cutters = []
    for x, y in BOARD['keys']:
        cutters.append(cube('Key travel aperture', (x-2.8, y-2.8, 0, x+2.8, y+2.8, 32)))
    for x, y in BOARD['leds']:
        cutters.append(cylinder('LED opening', x, y, 12, 2, 40))
    for x, y in BOARD['pots']:
        cutters.append(cylinder('Pot opening', x, y, 12, 5.5, 40))
    for x, y in BOARD['jacks']:
        cutters.append(cylinder('CV opening', x, y, 12, 3.2, 40))
    for x, y in BOARD['toggles']:
        cutters.append(cube('Toggle aperture', (x-6.5, y-11, 0, x+6.5, y+11, 32)))
    for x, y in BOARD['mounts']:
        cutters.append(cylinder('Open corner mounting well', x, y, 17, 8, 42))
    for y in Q['midi_axes_y']:
        cutters.append(cube('MIDI access', (195, y-11.5, -1.5, 205, y+11.5, 21.5)))
    for x in Q['audio_axis_x']:
        cutters.append(cylinder('Audio access', x, -4, 8.14, 9, 30, axis='y'))
    cutters.append(cube('J7 access', (-8, 33.09, -2.5, 4, 49.09, 8)))
    cutters.append(cube('Seed USB access', (-8, 65.7, 6.8, 4, 81.7, 14)))
    cutters.append(cube('SD access', (-8, 83, -.5, 3, 105, 10)))
    cutters.append(cube('Volume wheel access', (181, 80, -3, 205, 101, 6.6)))
    cutters.append(cube('Headphone access', (166.6, 108, -3, 180.6, 125, 8)))
    # A flush removable lid on the common crown, above the two real buttons.
    cutters.append(cube('Seed service opening', (5, 64, 9, 31, 84, 35)))
    # The source hood's roof ventilation stays open under the raised crown.
    for x in Q['seed_roof_vent_x']:
        cutters.append(cube('Seed vent', (x-1, 66, 12, x+1, 81, 35)))
    subtract(obj, cutters)
    # A subtraction must never introduce surfaces from the cutter volume.
    bounds = [(min(v.co[i] for v in obj.data.vertices), max(v.co[i] for v in obj.data.vertices))
              for i in range(3)]
    assert bounds[0][0] >= -4.001 and bounds[0][1] <= 199.001, bounds
    assert bounds[1][0] >= -119.801 and bounds[1][1] <= 8.801, bounds
    assert bounds[2][0] >= PCB+6.599 and bounds[2][1] <= PCB+27.401, bounds
    if style['profile'] == 'smooth':
        for polygon in obj.data.polygons:
            polygon.use_smooth = abs(polygon.normal.z) > .3
    bevel = obj.modifiers.new('Study edge treatment', 'BEVEL')
    bevel.width = .55 if style['profile'] == 'linear' else 1.1 if style['profile'] == 'smooth' else .16
    bevel.segments = 3 if style['profile'] == 'smooth' else 1
    return obj


def immutable_hardware_snapshot():
    names = ['cap-square-', 'pot-', 'CV ', 'CV-', 'toggle-', 'LED-',
             'MIDI-', 'DIN collar', 'audio-', 'Audio ', 'headphone-',
             'Headphone ', 'wheel-', 'Knurlie ', 'Lower spacer', 'Upper spacer']
    result = {}
    for o in bpy.data.objects:
        if o.type == 'MESH' and any(o.name.startswith(p) for p in names):
            digest = hashlib.sha256()
            for v in o.data.vertices:
                digest.update(str(tuple(v.co)).encode())
            result[o.name] = [list(row) for row in o.matrix_world], digest.hexdigest()
    return result


report = {'date': '2026-10-27', 'status': 'NOMINAL / HARDWARE NICHT GEMESSEN',
          'assembly_sha256': hashlib.sha256(args.assembly.read_bytes()).hexdigest(),
          'parameters_sha256': hashlib.sha256(PARAMETERS.read_bytes()).hexdigest(),
          'frame': '+X right / MIDI, +Y rear / audio, +Z up; Seed left',
          'studies': {}}
for key, style in STUDIES.items():
    if args.only and args.only != key:
        continue
    bpy.ops.wm.open_mainfile(filepath=str(args.assembly))
    snapshot = immutable_hardware_snapshot()
    shell_mat = material('Study enclosure', style['shell'], rough=.48)
    paper_mat = material('Study paper', style['paper'], rough=.8)
    accent_mat = material('Study accent', style['accent'], rough=.4)
    # Keep all original control positions, plate elevations and the tray.
    for name in ['tray', 'carrier', 'rear-panel']:
        assign(bpy.data.objects[name], shell_mat)
    # The original carrier remains in the scene as an internal reference.
    # Hiding it avoids coplanar old/new MIDI roofs producing black ray artefacts.
    bpy.data.objects['carrier'].hide_render = True
    assign(bpy.data.objects['paper'], paper_mat)
    # Omit the upper transparent face in the form render so that paper and
    # controls remain legible; the actual two-layer stack stays at its Z levels.
    bpy.data.objects['upper-acrylic'].hide_render = True
    exterior = skin(style, shell_mat)
    # The old hoods remain internal references, hidden behind the new crown.
    bpy.data.objects['seed-service-cover'].hide_render = True
    for o in bpy.data.objects:
        if o.name.startswith('Service screw head'):
            o.hide_render = True
    lid_z = height(18, 74, style)
    lid = cube('Removable service lid - nominal', (5.25, 64.25, lid_z-2.4, 30.75, 83.75, lid_z))
    assign(lid, shell_mat)
    for x, y in [(8, 67), (28, 81)]:
        screw = cylinder('Lid screw', x, y, lid_z+.1, 1.65, .25)
        assign(screw, bpy.data.materials['Metal hardware'])
    # Cosmetic knobs are explicit nominal additions, never measured hardware.
    for x, y in BOARD['pots']:
        knob = cylinder('Nominal knob diameter 11', x, y, 19, 5.5, 14)
        assign(knob, bpy.data.materials['Molded housings'])
        b = knob.modifiers.new('Knob edge', 'BEVEL'); b.width = .5; b.segments = 3
        tick = cube('Knob pointer', (x-.3, y-4.2, 26.01, x+.3, y-1.5, 26.08))
        assign(tick, accent_mat)
    # A quiet front seam and a restrained accent, both outside the control field.
    strip = cube('Front accent', (16, 119.82, 3.8, 163, 119.95, 4.5))
    assign(strip, accent_mat)
    if key == 'c':
        # Faceted perimeter wings add a pronounced console silhouette while
        # preserving side connector openings and the low volume-wheel niche.
        for x0, x1 in [(-4, 18), (183, 199)]:
            wing = cube('Console front cheek', (x0, 108.5, -2, x1, 119.8, 8.8))
            assign(wing, shell_mat)
            bevel = wing.modifiers.new('Faceted cheek', 'BEVEL')
            bevel.width = 3; bevel.segments = 1
    assert immutable_hardware_snapshot() == snapshot, 'Hardware geometry moved'
    scene = bpy.context.scene
    scene.cycles.samples = 24
    scene.view_settings.exposure = -.8
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 1500
    scene.render.resolution_y = 1100
    scene.render.resolution_percentage = 100
    # Shader bevels on imported metal are unnecessary for a form comparison.
    for m in bpy.data.materials:
        if m.use_nodes:
            for node in list(m.node_tree.nodes):
                if node.bl_idname == 'ShaderNodeBevel':
                    m.node_tree.nodes.remove(node)
    camera = scene.camera
    configs = {
        'hero': ((285, -238, 218), (98, -54, 21), 275),
        'rear': ((-85, 163, 157), (98, -48, 23), 277),
        'top': ((98, -55, 350), (98, -55, 20), 235),
    }
    for view, (position, target, scale) in configs.items():
        if view not in args.views:
            continue
        camera.location = position
        camera.rotation_euler = (Vector(target)-camera.location).to_track_quat('-Z', 'Y').to_euler()
        camera.data.ortho_scale = scale
        scene.render.filepath = str(args.output / f'{key}-{view}.png')
        bpy.ops.render.render(write_still=True)
    scene.render.filepath = ''
    scene_path = args.output / f'{key}-study.blend'
    # Local scene contains manufacturer objects and is deliberately not tracked.
    bpy.ops.wm.save_as_mainfile(filepath=str(scene_path))
    report['studies'][key] = {'name': style['name'], 'hardware_objects_unchanged': len(snapshot),
                              'crown_max_pcb_relative_mm': {'a': 24.4, 'b': 26.4, 'c': 27.4}[key],
                              'acrylic_top_mm': 11.15,
                              'knob_preview_mm': {'diameter': 11, 'height': 14},
                              'views': list(configs)}
(args.output / ('validation-'+(args.only or 'all')+'.json')).write_text(
    json.dumps(report, indent=2, ensure_ascii=False)+'\n', encoding='utf8')
