"""Render the generated STLs in Blender; electronics are schematic context only.

blender --background --python render_preview.py -- --output-dir output
"""

import argparse
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
args = argparse.ArgumentParser()
args.add_argument('--output-dir', type=Path, default=ROOT/'output')
opt = args.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
data = json.loads((ROOT/'board.json').read_text())
report = json.loads((opt.output_dir/'validation.json').read_text())
assert report.get('stl_frame',{}).get('revision')=='RH-2026-10-25','Withdrawn STL revision'
seat = report['design_mm']['frame_underside']
pcbtop = report['design_mm']['pcb_top']
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)


def material(name, color, roughness=.5, metal=0):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (*color, 1)
    shader.inputs['Roughness'].default_value = roughness
    shader.inputs['Metallic'].default_value = metal
    return mat


shell = material('PETG charcoal', (.055, .07, .077), .6)
paper = material('Paper ivory', (.83, .82, .72), .9)
pcbmat = material('PCB simplified', (.025, .06, .051), .6)
black = material('Control black', (.022, .025, .027), .5)
grey = material('Light grey caps', (.58, .6, .59), .5)
silver = material('Connector metal', (.5, .53, .56), .27, .8)
green = material('Status LEDs (unlit)', (.15, .28, .12), .5)
ink = material('Paper print', (.035, .04, .045), .8)


def cube(name, x, y, z, w, d, h, mat, bevel=0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=(x+w/2, -y-d/2, z+h/2))
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = (w, d, h)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    if bevel:
        mod = obj.modifiers.new('Edge radius', 'BEVEL')
        mod.width, mod.segments = bevel, 3
        obj.modifiers.new('Normals', 'WEIGHTED_NORMAL')
    return obj


def cyl(name, x, y, z, r, h, mat):
    bpy.ops.mesh.primitive_cylinder_add(vertices=48, radius=r, depth=h,
                                      location=(x, -y, z+h/2))
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(mat)
    return obj


def label(text, x, y, z, size=2.2):
    bpy.ops.object.text_add(location=(x, -y, z))
    obj = bpy.context.object
    obj.name = text
    obj.data.body = text
    obj.data.size = size
    obj.data.align_x = 'CENTER'
    obj.data.materials.append(ink)


def stl(name, z, mat):
    bpy.ops.wm.stl_import(filepath=str(opt.output_dir/f'{name}.stl'))
    obj = bpy.context.object
    # STL is already in the physical RH frame; render exactly what is printed.
    assert max(v.co.y for v in obj.data.vertices)<=9.01,(name,'Wrong STL orientation')
    obj.location.z = z
    obj.data.materials.append(mat)
    return obj


tray = stl('tray', 0, shell)
frame = stl('overlay-frame', seat, shell)
# The top 0.15 mm of the same carrier illustrates the interchangeable paper.
overlay = stl('overlay-frame', seat+2.4, paper)
overlay.name = 'Paper overlay (same contour)'
overlay.scale.z = .15/2.4
cube('PCB - simplified outline', 0, 0, 8.8, 197, 111, 1.6, pcbmat, 2)
for p in data['mounts']:
    cyl('PCB M3 screw', p['x'], p['y'], pcbtop, 2.8, 1.7, silver)
for x, y in [(-4.5, -4.5), (201.5, -4.5), (-4.5, 115.5), (201.5, 115.5)]:
    cyl('Cover screw', x, y, seat+2.55, 2.8, 1.5, silver)
for p in data['pots']:
    cube('Pot housing schematic', p['x']-4.5, p['y']-5.3, pcbtop, 9, 10.6, 7, silver, .5)
    cyl('Pot shaft schematic', p['x'], p['y'], pcbtop+7, 3, 5, black)
    label(p['ref'], p['x'], 68, seat+2.57)
for p in data['buttons']:
    cube('Switch body', p['x']-3, p['y']-3, pcbtop, 6, 6, 3.5, black, .3)
    if int(p['ref'][1:]) <= 8:
        cube('1SBLK envelope schematic', p['x']-2.5, p['y']-2.5, pcbtop+4, 5, 5, 9, black, .35)
        label(p['ref'], p['x'], 72, seat+2.57, 1.8)
    else:
        cyl('1RLGR envelope schematic', p['x'], p['y'], pcbtop+4, 2.5, 9, grey)
        label(p['ref'], p['x'], 109, seat+2.57, 1.8)
for p in data['leds']:
    cube('LED', p['x']-.8, p['y']-.4, pcbtop, 1.6, .8, .7, green)
for p in data['toggles']:
    cube('Toggle housing', p['x']-2.5, p['y']-5, pcbtop, 5, 10, 7, black)
    cyl('Toggle lever schematic', p['x'], p['y'], pcbtop+7, 1.4, 11, silver)
    label(p['ref'], p['x'], 84, seat+2.57, 2)
for x in [39.915, 62.4, 140.4, 162.8]:
    cube('Audio jack schematic', x-9.1, .5, pcbtop, 18.2, 24, 15, black, .7)
    cyl_obj = cyl('Audio ferrule schematic', x, 0, pcbtop+8, 5.7, 4, silver)
    cyl_obj.rotation_euler.x = math.pi/2
for y in [20.3, 44.3, 68.3]:
    cube('DIN jack schematic', 179.8, y-10, pcbtop, 17, 20, 20, black, .8)
for x in [82, 112.3, 122.4]:
    for y in [15.8, 31.4]:
        cube('CV or gate jack schematic', x-4.5, y-5, pcbtop, 9, 10, 10, black, .4)
        cyl('CV jack rim', x, y, pcbtop+10, 2.8, 1, silver)
cube('Seed module schematic', 0, 64.5, pcbtop+3, 53, 18, 1.6, pcbmat, .5)
cube('Processor schematic', 15, 68, pcbtop+4.6, 11, 11, 1.5, black, .3)
cube('J7 USB schematic', -.3, 36.6, pcbtop, 7.5, 9, 3.5, silver, .4)
cube('Seed USB schematic', -.3, 68, pcbtop+4.6, 7.5, 9, 3.5, silver, .4)
cube('SD socket schematic', 0, 84, pcbtop, 17, 18, 2, silver, .5)
cube('Headphone jack schematic', 170.6, 94, pcbtop, 6, 17, 5, black, .3)
cyl('Volume wheel schematic', 191.3, 91.6, pcbtop, 7, 5, black)
for x, y in [(6, 17), (6, 29), (22, 31), (34, 31), (7, 55)]:
    cyl('Capacitor schematic', x, y, pcbtop, 4, 10, silver)

# Separate second image: use the same source meshes, with no electronics.
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = 24
scene.cycles.use_denoising = True
scene.render.resolution_x, scene.render.resolution_y = 1700, 1100
scene.render.resolution_percentage = 100
scene.world.color = (.25, .25, .25)
scene.view_settings.view_transform = 'AgX'
ground = material('Backdrop', (.17, .2, .22), .9)
cube('Ground', -1000, -1000, -2, 2000, 2000, 1, ground)
for loc, energy, size in [((30, -80, 300), 1500000, 200), ((230, 160, 160), 1000000, 180)]:
    bpy.ops.object.light_add(type='AREA', location=loc)
    light = bpy.context.object
    light.data.energy, light.data.shape, light.data.size = energy, 'DISK', size
    light.rotation_euler = (Vector((98, -55, 0))-light.location).to_track_quat('-Z', 'Y').to_euler()
bpy.ops.object.camera_add(location=(280, -310, 280))
cam = bpy.context.object
cam.rotation_euler = (Vector((98.5, -55.5, 8))-cam.location).to_track_quat('-Z', 'Y').to_euler()
cam.data.type = 'ORTHO'
cam.data.ortho_scale = 285
scene.camera = cam
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = str(opt.output_dir/'case-preview.png')
bpy.ops.wm.save_as_mainfile(filepath=str(opt.output_dir/'case-preview.blend'))
bpy.ops.render.render(write_still=True)
