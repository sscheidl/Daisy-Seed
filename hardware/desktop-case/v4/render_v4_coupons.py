"""Blender contact sheet of the actual delivered STLs; no construction solids.

Run: blender --background --python hardware/desktop-case/v4/render_v4_coupons.py
Layout/lighting values are presentation settings, never mechanical dimensions.
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parent
manifest=json.loads((ROOT/"coupons/manifest.json").read_text(encoding="utf8"))
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)


def material(name,color):
    a=bpy.data.materials.new(name); a.diffuse_color=(*color,1); a.use_nodes=True
    bsdf=a.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value=(*color,1)
    bsdf.inputs["Roughness"].default_value=.56
    return a


shell=material("Black PETG-CF, schematic finish",(.045,.054,.06))
ink=material("Caption ink",(.025,.035,.05))
ground=material("Neutral floor",(.7,.73,.76))


def text_label(label,x,y,size):
    curve=bpy.data.curves.new(label,"FONT"); curve.body=label; curve.size=size
    obj=bpy.data.objects.new(label,curve); bpy.context.collection.objects.link(obj)
    obj.location=(x,y,.05); obj.data.materials.append(ink)


for i,(name,meta) in enumerate(manifest["parts"].items()):
    bpy.ops.wm.stl_import(filepath=str(ROOT/"coupons"/meta["file"]))
    obj=bpy.context.object; obj.name=name; obj.data.materials.clear(); obj.data.materials.append(shell)
    # Translation only: retain the handedness, faces and openings of the STL.
    xyz=[obj.matrix_world@Vector(p) for p in obj.bound_box]
    cx=(min(p.x for p in xyz)+max(p.x for p in xyz))/2
    cy=(min(p.y for p in xyz)+max(p.y for p in xyz))/2
    x=(i%7)*100; y=-(i//7)*65
    obj.location+=Vector((x-cx,y-cy,-min(p.z for p in xyz)+.1))
    text_label(name,x-43,y-28,4.4)

text_label("V4 / C1-C7   |   38 Einzel-STLs   |   NOMINAL - Hardware NICHT GEMESSEN",-45,48,6)
text_label("Teile zur Uebersicht verschoben. Keine montierte Baugruppe. Keine Spiegelung.",-45,37,4.3)
bpy.ops.mesh.primitive_plane_add(size=2000,location=(300,-150,-.05))
bpy.context.object.data.materials.append(ground)

bpy.ops.object.camera_add(location=(540,-700,1000))
camera=bpy.context.object; target=Vector((300,-150,0))
camera.rotation_euler=(target-camera.location).to_track_quat("-Z","Y").to_euler()
camera.data.type="ORTHO"; camera.data.ortho_scale=860; camera.data.clip_end=10000
scene=bpy.context.scene; scene.camera=camera
for pos,power,size in [((140,-200,650),5000000,450),((700,200,450),3000000,350)]:
    bpy.ops.object.light_add(type="AREA",location=pos)
    light=bpy.context.object; light.data.energy=power; light.data.shape="DISK"; light.data.size=size
    light.rotation_euler=(Vector((300,-150,0))-light.location).to_track_quat("-Z","Y").to_euler()
scene.world.color=(.35,.35,.35)
scene.render.engine="CYCLES"; scene.cycles.samples=32
scene.cycles.use_denoising=True
try:
    prefs=bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type="OPTIX"; prefs.get_devices()
    for device in prefs.devices: device.use=device.type=="OPTIX"
    scene.cycles.device="GPU"
except Exception:
    scene.cycles.device="CPU"
scene.render.resolution_x=2700; scene.render.resolution_y=2000; scene.render.resolution_percentage=100
scene.view_settings.view_transform="AgX"
scene.render.image_settings.file_format="PNG"
scene.render.filepath=str(ROOT/"coupons-overview.png")
bpy.ops.render.render(write_still=True)
local=ROOT.parents[2]/"build/v4-checks/coupons-preview.blend"
local.parent.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(local))
