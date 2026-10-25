"""Blender rendering of V3 source STLs; electronics and hardware are schematic."""

import json
import math
import sys
from pathlib import Path
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parent
OUT = ROOT/'output/unified-v3'
B = json.loads((ROOT/'board.json').read_text())
C = json.loads((ROOT/'closed-mechanics.json').read_text())
R = json.loads((OUT/'validation.json').read_text())
assert R.get('stl_frame',{}).get('revision')=='RH-2026-10-25','Withdrawn STL revision'
VIEWS = set(sys.argv[sys.argv.index('--')+1:]) if '--' in sys.argv else {'preview','exploded','underside'}
S = R['stack_mm']
PCB = S['pcb_top']
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)


def mat(name,color,metal=0,transmission=0):
    m = bpy.data.materials.new(name); m.use_nodes=True
    p = m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Roughness'].default_value=.45 if not transmission else .12
    p.inputs['Metallic'].default_value=metal
    p.inputs['Transmission Weight'].default_value=transmission
    p.inputs['IOR'].default_value=1.49
    return m


shell=mat('Charcoal PETG',(.035,.05,.057))
paper=mat('Paper ivory',(.85,.82,.7))
acrylic=mat('Clear acrylic',(.82,.94,.96),transmission=.94)
pcbmat=mat('PCB schematic',(.025,.075,.055))
metal=mat('Metal posts and nuts',(.5,.53,.55),metal=.8)
black=mat('Black caps',(.015,.02,.022))
grey=mat('Grey caps',(.57,.59,.58))
ink=mat('Print',(.03,.035,.04))
objects={}


def cube(name,x,y,z,w,d,h,m):
    bpy.ops.mesh.primitive_cube_add(size=1,location=(x+w/2,-y-d/2,z+h/2))
    o=bpy.context.object; o.name=name; o.dimensions=(w,d,h)
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    o.data.materials.append(m); return o


def cyl(name,x,y,z,r,h,m,vertices=48):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=r,depth=h,location=(x,-y,z+h/2))
    o=bpy.context.object; o.name=name; o.data.materials.append(m); return o


def text(label,x,y,z,size=2):
    bpy.ops.object.text_add(location=(x,-y,z)); o=bpy.context.object
    o.data.body=label; o.data.size=size; o.data.align_x='CENTER'; o.data.materials.append(ink)
    return o


def stl(name,z,m):
    bpy.ops.wm.stl_import(filepath=str(OUT/f'{name}.stl'))
    o=bpy.context.object; o.name=name
    # Physical RH STL: no render-only reflection.
    assert max(v.co.y for v in o.data.vertices)<=9.01,(name,'Wrong STL orientation')
    o.location.z=z; o.data.materials.append(m)
    if m==acrylic:
        # Avoid opaque shadow approximations on the paper in a short product render.
        o.visible_shadow=False
    objects[name]=o; return o


stl('tray',0,shell)
stl('lower-acrylic-reference',S['lower_bottom'],acrylic)
stl('upper-acrylic-reference',S['upper_bottom'],acrylic)
objects['upper-acrylic-reference'].location.z+=.02  # Render-only separation at the touching paper interface.
stl('paper-reference',S['lower_top'],paper)
stl('unified-carrier',R['parts']['unified-carrier']['installed_z'],shell)
cube('PCB (schematic)',0,0,PCB-1.6,197,111,1.6,pcbmat)
post_objects=[]
knurlies=[]
for p in B['mounts']:
    post_objects.append(cyl('Lower metal M3 female-female',p['x'],p['y'],2.8,5.5/math.sqrt(3),10,metal,6))
    post_objects.append(cyl('Upper metal M3 male-female',p['x'],p['y'],PCB+S['upper_post_shim'],5.5/math.sqrt(3),S['upper_post_body'],metal,6))
    post_objects.append(cyl('Metal shim schematic',p['x'],p['y'],PCB,3,S['upper_post_shim'],metal))
    knurlies.append(cyl('M3 Knurlie schematic',p['x'],p['y'],S['upper_top'],2.25,4,black,32))
for p in C['jacks']:
    cube('QingPu housing schematic',p['x']-4.5,p['y']-6,PCB,9,10.5,9,black)
    cyl('QingPu barrel schematic',p['x'],p['y'],PCB+9,3,4.5,metal)
    cyl('Jack nut on lower layer',p['x'],p['y'],S['lower_top'],4.1,1.8,metal,32)
for p in B['pots']:
    cube('Pot housing schematic',p['x']-4.5,p['y']-5.3,PCB,9,10.6,7,metal)
    # BOM RV09AF-40-25K: standard Alpha L=25 mm from seating plane to tip.
    # Direct PCB seating is user-confirmed; custom -0085 dimensions are unmeasured.
    cyl('Pot shaft nominal L25, dimensions unmeasured',p['x'],p['y'],PCB+7,3,25-7,black)
    text(p['ref'],p['x'],68,S['lower_top']+.16)
for p in B['buttons']:
    cube('Switch body',p['x']-3,p['y']-3,PCB,6,6,3.5,black)
    if int(p['ref'][1:])<=8:
        cube('1SBLK assumed seated height',p['x']-2.5,p['y']-2.5,PCB+4,5,5,9,black)
        text(p['ref'],p['x'],72,S['lower_top']+.16,1.8)
    else:
        cyl('1RLGR assumed seated height',p['x'],p['y'],PCB+4,2.5,9,grey)
        text(p['ref'],p['x'],109,S['lower_top']+.16,1.8)
for p in B['leds']:
    cube('Unlit LED schematic',p['x']-.8,p['y']-.4,PCB,1.6,.8,.7,grey)
for p in B['toggles']:
    # Dailywell 2MSxT1B1M2: nominal body 8.64, B1 5.59, T1 9.40 mm.
    # Direct PCB seating is user-confirmed; actual dimensions remain unmeasured.
    cyl('Toggle barrel nominal, dimensions unmeasured',p['x'],p['y'],PCB+8.64,2.4,5.59,metal)
    cyl('10-48 switch nut schematic',p['x'],p['y'],S['lower_top'],3.5,1.8,metal,6)
    cyl('Toggle lever nominal, shown upright',p['x'],p['y'],PCB+8.64+5.59,1.3,9.4,metal)
    text(p['ref'],p['x'],86,S['lower_top']+.16)
for p in C['service_positions_provisional']:
    text(p['name'],p['x']+7,p['y']+1,PCB+16+2.42,1.8)
for x in [39.915,62.4,140.4,162.8]:
    cube('Audio jack schematic',x-9.1,.5,PCB,18.2,24,15.7,black)
for y in [20.3,44.3,68.3]:
    cube('DIN jack schematic',179.8,y-10,PCB,17,20,20,black)
cube('Seed module schematic',1.4,64.7,PCB+7.4,51.15,18,1.6,pcbmat)
cyl('Headphone level wheel',191.325,91.6,PCB,7,10,black)

scene=bpy.context.scene; scene.render.engine='CYCLES'; scene.cycles.samples=24
scene.cycles.use_denoising=True; scene.render.resolution_x=1700; scene.render.resolution_y=1100
scene.render.resolution_percentage=100; scene.view_settings.view_transform='AgX'; scene.world.color=(.3,.3,.3)
ground=mat('Backdrop',(.16,.2,.22)); cube('Ground',-1000,-1000,-2,2000,2000,1,ground)
for loc,energy in [((30,-80,300),1500000),((230,160,160),1000000)]:
    bpy.ops.object.light_add(type='AREA',location=loc); o=bpy.context.object
    o.data.energy=energy; o.data.shape='DISK'; o.data.size=180
    o.rotation_euler=(Vector((98,-55,0))-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(280,-310,290)); camera=bpy.context.object
camera.rotation_euler=(Vector((98,-55,15))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO'; camera.data.ortho_scale=290; scene.camera=camera
scene.render.image_settings.file_format='PNG'; scene.render.filepath=str(OUT/'unified-v3-preview.png')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'unified-v3-preview.blend'))
if 'preview' in VIEWS: bpy.ops.render.render(write_still=True)

# Explode actual layer meshes to make the load path and removable cover visible.
for name,delta in [('lower-acrylic-reference',34),('paper-reference',48),('upper-acrylic-reference',62),
                   ('unified-carrier',16)]:
    objects[name].location.z+=delta
for o in knurlies: o.location.z+=70
for o in bpy.context.scene.objects:
    if o.type=='FONT': o.hide_render=True
camera.data.ortho_scale=360
camera.rotation_euler=(Vector((98,-55,45))-camera.location).to_track_quat('-Z','Y').to_euler()
scene.render.filepath=str(OUT/'unified-v3-exploded.png')
if 'exploded' in VIEWS: bpy.ops.render.render(write_still=True)

# Isolate the actual single carrier mesh and show its underside, including guides.
for o in list(bpy.context.scene.objects):
    if o.type in ['MESH','FONT']: o.hide_render = o != objects['unified-carrier']
carrier=objects['unified-carrier']; carrier.location.z=0
camera.location=(240,-270,-210)
camera.rotation_euler=(Vector((98,-55,8))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.ortho_scale=260
for o in bpy.context.scene.objects:
    if o.type=='LIGHT':
        o.location.z=-abs(o.location.z)
        o.rotation_euler=(Vector((98,-55,8))-o.location).to_track_quat('-Z','Y').to_euler()
scene.render.film_transparent=False
scene.render.filepath=str(OUT/'unified-v3-underside.png')
if 'underside' in VIEWS: bpy.ops.render.render(write_still=True)
