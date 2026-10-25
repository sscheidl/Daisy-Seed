"""Top-view proof from actual old/new STL files, with no reflection at render time.

The old part is rigidly rotated 180 degrees about Z to put audio at the rear.
Its reversed Seed/MIDI positions then become visible. The corrected STL is
imported directly. This diagnostic scene is not a print model.
"""

import json
import math
import subprocess
from pathlib import Path
import bpy

ROOT=Path(__file__).resolve().parent
REPO=ROOT.parents[1]
OUT=ROOT/'output/orientation-proof'
OUT.mkdir(parents=True,exist_ok=True)
BASELINE='9ca3be6'
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)


def mat(name,color):
    m=bpy.data.materials.new(name); m.diffuse_color=(*color,1); return m


old_mat=mat('Withdrawn file',(.32,.12,.11))
new_mat=mat('Corrected RH file',(.10,.23,.30))
dark=mat('Dark text',(.025,.03,.035))
light=mat('Light text',(.95,.96,.97))


def load(path,z,offset,old):
    bpy.ops.wm.stl_import(filepath=str(path)); o=bpy.context.object
    if old:
        o.rotation_euler.z=math.pi
        o.location.x=197+offset
    else: o.location.x=offset
    o.location.z=z
    o.data.materials.append(old_mat if old else new_mat)
    # Only a rigid rotation was used. No negative scale or mesh reflection.
    assert all(s>0 for s in o.scale)


def label(text,x,y,size=4,material=dark,z=70):
    bpy.ops.object.text_add(location=(x,y,z)); o=bpy.context.object
    o.data.body=text; o.data.align_x='CENTER'; o.data.size=size
    o.data.materials.append(material)


for old,offset in [(True,0),(False,250)]:
    for name,z in [('tray',0),('unified-carrier',20.4)]:
        if old:
            p=OUT/f'WITHDRAWN-DO-NOT-PRINT-{name}.stl'
            p.write_bytes(subprocess.check_output(['git','show',f'{BASELINE}:hardware/desktop-case/unified-v3/{name}.stl'],cwd=REPO))
        else: p=ROOT/'unified-v3'/f'{name}.stl'
        load(p,z,offset,old)
    centre=98.5+offset
    label('ALT - ZURUECKGEZOGEN' if old else 'RH-KORREKTUR 25.10.2026',centre,47,5.2)
    label('Audio nach hinten ausgerichtet',centre,36,3.3)
    label('HINTEN / AUDIO',centre,16,3.3)
    label('VORNE',centre,-131,3.3)
    label('Seed rechts / MIDI links: FALSCH' if old else 'Seed links / MIDI rechts: RICHTIG',centre,-148,3.8)
    label('SEED',offset+(177 if old else 20),-74,3.6,light)
    label('MIDI',offset+(4.5 if old else 192.5),-48,3.2,light)
    label('AUDIO',centre,-1,3.3,light)
    label('CV OUT',offset+(115 if old else 82),-26,3,light)

label('Draufsicht auf die bestueckte Seite - Geometrie direkt aus den Druck-STLs',223.5,-176,4)
label('Links: nur starre Z-Drehung. Rechts: ohne Renderer-Spiegelung importiert.',223.5,-186,3.3)

scene=bpy.context.scene
scene.render.engine='BLENDER_WORKBENCH'
scene.display.shading.light='STUDIO'; scene.display.shading.color_type='MATERIAL'
scene.display.shading.show_shadows=False; scene.display.shading.show_cavity=True
scene.display.shading.cavity_type='BOTH'; scene.display.shading.background_type='WORLD'
scene.world.color=(.85,.86,.87)
scene.render.resolution_x=2400; scene.render.resolution_y=1500; scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
bpy.ops.object.camera_add(location=(223.5,-55,500))
camera=bpy.context.object; camera.rotation_euler=(0,0,0)
camera.data.type='ORTHO'; camera.data.ortho_scale=500; scene.camera=camera
scene.render.filepath=str(OUT/'orientation-proof-RH-2026-10-25.png')
bpy.ops.render.render(write_still=True)
print('STL orientation proof rendered without any reflection.')
