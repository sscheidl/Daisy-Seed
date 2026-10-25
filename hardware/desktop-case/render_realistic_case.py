"""Render and export the V3 assembly in Blender; dimensions are millimetres.

Uses actual case STLs, a KiCad board export, and the official Seed3 FBX.
Missing controls are procedural approximations, not manufacturer-certified CAD.
"""

import hashlib
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'output/realistic-v3'
PARTS=ROOT/'unified-v3'
REF=ROOT.parents[1]/'build/case-reference'
B=json.loads((ROOT/'board.json').read_text())
C=json.loads((ROOT/'closed-mechanics.json').read_text())
A=json.loads((ROOT/'right-controls.json').read_text())
R=json.loads((PARTS/'validation.json').read_text()); S=R['stack_mm']; PCB=S['pcb_top']
assert R.get('stl_frame',{}).get('revision')=='RH-2026-10-25','Withdrawn STL export revision'
VIEWS=set(sys.argv[sys.argv.index('--')+1:]) if '--' in sys.argv else {'studio','rear','exploded','access'}
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='NONE'
bpy.context.scene.unit_settings.scale_length=1
objects={}; hardware=[]; labels=[]; electronics=[]


def material(name,color,rough=.4,metallic=0,transmission=0):
    m=bpy.data.materials.new(name); m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Roughness'].default_value=rough
    p.inputs['Metallic'].default_value=metallic
    p.inputs['Transmission Weight'].default_value=transmission
    p.inputs['IOR'].default_value=1.49
    return m


shell=material('Printed charcoal PETG',(.025,.032,.036),.58)
steel=material('Satin nickel and steel',(.46,.49,.52),.24,.92)
chrome=material('Chrome actuator',(.65,.68,.7),.12,1)
brass=material('Brass contacts',(.56,.37,.12),.3,.8)
black=material('Black moulded plastic',(.012,.014,.015),.33)
grey=material('Light grey 1RLGR caps',(.48,.5,.49),.4)
blue=material('Pot base blue plastic',(.015,.12,.24),.44)
ivory=material('Paper warm white',(.86,.85,.79),.75)
ink=material('Printed labels',(.035,.041,.04),.85)
clear=material('Polished 1mm acrylic',(.97,.985,1),.035,0,1)
washer=material('Knurlie polymer washer',(.035,.038,.04),.45)
knurlie_mat=material('Black nickel Knurlie approximation',(.055,.06,.063),.28,.85)
# Shader-only edge softening and fine surface relief do not alter print geometry.
nodes=shell.node_tree.nodes; links=shell.node_tree.links; bsdf=nodes.get('Principled BSDF')
bevel=nodes.new('ShaderNodeBevel'); bevel.inputs['Radius'].default_value=.10; bevel.samples=4
noise=nodes.new('ShaderNodeTexNoise'); noise.inputs['Scale'].default_value=180
bump=nodes.new('ShaderNodeBump'); bump.inputs['Strength'].default_value=.12; bump.inputs['Distance'].default_value=.025
links.new(bevel.outputs['Normal'],bump.inputs['Normal']); links.new(noise.outputs['Fac'],bump.inputs['Height'])
links.new(bump.outputs['Normal'],bsdf.inputs['Normal'])


def cube(name,x,y,z,w,d,h,mat,soft=0):
    bpy.ops.mesh.primitive_cube_add(size=1,location=(x+w/2,-y-d/2,z+h/2))
    o=bpy.context.object; o.name=name; o.dimensions=(w,d,h)
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    o.data.materials.append(mat)
    if soft:
        mod=o.modifiers.new('Moulded edge','BEVEL'); mod.width=soft; mod.segments=3
        bpy.context.view_layer.objects.active=o; bpy.ops.object.modifier_apply(modifier=mod.name)
    return o


def cyl(name,x,y,z,r,h,mat,vertices=64):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=r,depth=h,location=(x,-y,z+h/2))
    o=bpy.context.object; o.name=name; o.data.materials.append(mat)
    for f in o.data.polygons: f.use_smooth=len(f.vertices)==4
    return o


def ring(name,x,y,z,outer,inner,h,mat,n=96,flutes=0,hexagon=False):
    verts=[]
    for radius,dz in [(outer,0),(outer,h),(inner,h),(inner,0)]:
        for i in range(n):
            a=i*2*math.pi/n; r=radius
            if radius==outer and flutes: r-=.14*(.5+.5*math.cos(flutes*a))
            if radius==outer and hexagon:
                r=outer*math.cos(math.pi/6)/math.cos((a+math.pi/6)%(math.pi/3)-math.pi/6)
            verts.append((r*math.cos(a),r*math.sin(a),z+dz))
    faces=[]
    for j in range(4):
        for i in range(n):
            a=j*n+i; bb=j*n+(i+1)%n; cc=((j+1)%4)*n+(i+1)%n; d=((j+1)%4)*n+i
            faces.append((a,bb,cc,d))
    mesh=bpy.data.meshes.new(name); mesh.from_pydata(verts,[],faces); mesh.update()
    o=bpy.data.objects.new(name,mesh); bpy.context.collection.objects.link(o)
    o.location=(x,-y,0); o.data.materials.append(mat)
    return o


def horizontal(o,axis,x,y,z):
    # Input primitive has its base at local Z=0.
    o.location=(x,-y,z)
    o.rotation_euler=(math.pi/2,0,0) if axis=='rear' else (0,math.pi/2,0)
    return o


def label(value,x,y,z,size=2.1):
    bpy.ops.object.text_add(location=(x,-y,z))
    o=bpy.context.object; o.name='Paper label '+value; o.data.body=value
    o.data.size=size; o.data.align_x='CENTER'; o.data.materials.append(ink); labels.append(o)
    return o


def part(name,z,mat):
    bpy.ops.wm.stl_import(filepath=str(PARTS/f'{name}.stl'))
    o=bpy.context.object; o.name=name
    # Import the exact physical RH print mesh: never repair handedness here.
    assert max(v.co.y for v in o.data.vertices)<=9.01,(name,'Non-physical STL orientation')
    o.location.z=z; o.data.materials.append(mat)
    if mat==clear: o.visible_shadow=False
    objects[name]=o; return o


part('tray',0,shell)
part('unified-carrier',R['parts']['unified-carrier']['installed_z'],shell)
part('lower-acrylic-reference',S['lower_bottom'],clear)
part('paper-reference',S['lower_top'],ivory)
part('upper-acrylic-reference',S['upper_bottom'],clear)
objects['upper-acrylic-reference'].location.z+=.015  # Render-only touching-interface separation.

before=set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=str(OUT/'board-mm.glb'))
electronics += [o for o in bpy.data.objects if o not in before and o.type=='MESH']
board_vertices=[o.matrix_world@Vector(p) for o in electronics for p in o.bound_box]
board_bounds=[[min(v[i] for v in board_vertices) for i in range(3)],
              [max(v[i] for v in board_vertices) for i in range(3)]]
assert abs(board_bounds[1][0]-197)<.1 and abs(board_bounds[0][1]+111)<.1,board_bounds
assert board_bounds[0][2]>10 and board_bounds[1][2]<24,board_bounds

# Bake the actual Seed3 FBX into the case frame. The source's -Y face is up;
# +Y pin tails point down. This right-handed transform avoids mirroring the PCB.
before=set(bpy.data.objects)
bpy.ops.import_scene.fbx(filepath=str(REF/'seed3-models/ES_Daisy_Seed3_Rev5.fbx'))
seed=[o for o in bpy.data.objects if o not in before and o.type=='MESH']
seed_world={o:o.matrix_world.copy() for o in seed}
board_source=next(o for o in seed if o.name.startswith('Board'))
source_bounds=[board_source.matrix_world@Vector(p) for p in board_source.bound_box]
print('SEED SOURCE PCB BOUNDS',[[min(p[i] for p in source_bounds) for i in range(3)],
    [max(p[i] for p in source_bounds) for i in range(3)]],flush=True)
front=min(p.y for p in source_bounds)
transform=Matrix(((0,0,1000,1.4),(-1000,0,0,-64.7),(0,-1000,0,PCB+9+1000*front),(0,0,0,1)))
seed_services=[]
for o in seed:
    world=seed_world[o]
    names=[o.name]+[p.name for p in [o.parent] if p]
    o.parent=None; o.matrix_world=Matrix.Identity(4)
    o.data=o.data.copy()  # FBX instances share data; bake each instance exactly once.
    o.data.transform(transform@world); o.data.update()
    o.name='Seed3 manufacturer '+o.name
    electronics.append(o)
    if any('PTS815' in x for x in names):
        # Data are baked in world mm; bound_box may still be stale until depsgraph update.
        bounds=[v.co.copy() for v in o.data.vertices]
        seed_services.append(dict(name=o.name,min=[min(p[i] for p in bounds) for i in range(3)],max=[max(p[i] for p in bounds) for i in range(3)]))
bpy.context.view_layer.update()
seed_vertices=[o.matrix_world@v.co for o in seed for v in o.data.vertices]
seed_bounds=[[min(v[i] for v in seed_vertices) for i in range(3)],
             [max(v[i] for v in seed_vertices) for i in range(3)]]
assert 50<seed_bounds[1][0]-seed_bounds[0][0]<54,seed_bounds
assert 17<seed_bounds[1][1]-seed_bounds[0][1]<20,seed_bounds
assert seed_bounds[1][2]<PCB+16 and seed_bounds[0][2]>PCB-2,seed_bounds

for p in B['mounts']:
    x,y=p['x'],p['y']
    hardware.append(ring('Lower metal M3 spacer 10mm',x,y,2.8,5.5/math.sqrt(3),1.5,10,steel,hexagon=True))
    hardware.append(ring('Upper metal M3 spacer 8mm',x,y,PCB+.2,5.5/math.sqrt(3),1.5,8,steel,hexagon=True))
    hardware.append(ring('Metal shim .2mm',x,y,PCB,3.1,1.7,.2,steel))
    hardware.append(cyl('Upper post male M3 stud',x,y,PCB+.2-6,1.5,6,steel))
    hardware.append(cyl('Floor screw shank',x,y,1.4,1.5,6,steel))
    hardware.append(cyl('Floor recessed screw head',x,y,.2,2.8,1.2,steel))
    hardware.append(cyl('Knurlie M3 nominal usable shank',x,y,S['upper_top']-6,1.5,6,knurlie_mat))
    hardware.append(ring('Knurlie washer approximate',x,y,S['upper_top'],3.9,1.6,.45,washer))
    head=ring('Knurlie head approximate, not an exact Befaco model',x,y,S['upper_top']+.45,3.8,.8,3,knurlie_mat,flutes=40)
    hardware.append(head)
    # Recessed Phillips/slot impression; visual approximation only.
    for w,d in [(5.5,.85),(.85,5.5)]:
        cutter=cube('Temporary slot',x-w/2,y-d/2,S['upper_top']+2.7,w,d,1.2,black)
        mod=head.modifiers.new('Drive recess','BOOLEAN'); mod.operation='DIFFERENCE'; mod.object=cutter
        bpy.context.view_layer.objects.active=head; bpy.ops.object.modifier_apply(modifier=mod.name)
        bpy.data.objects.remove(cutter,do_unlink=True)

for p in C['jacks']:
    x,y=p['x'],p['y']
    electronics.append(cube(p['ref']+' housing nominal',x-4.5,y-6,PCB,9,10.5,9,black,.15))
    electronics.append(ring(p['ref']+' open barrel',x,y,PCB+9,3,1.8,4.5,steel))
    electronics.append(cyl(p['ref']+' dark contact well',x,y,PCB+5,1.7,2,black))
    electronics.append(ring(p['ref']+' hex panel nut',x,y,S['lower_top'],4.25,3.05,1.8,steel,hexagon=True))

for i,p in enumerate(B['pots']):
    x,y=p['x'],p['y']
    electronics.append(cube(p['ref']+' blue base',x-4.9,y-5.3,PCB,9.8,10.6,1.6,blue,.15))
    electronics.append(cube(p['ref']+' stamped case',x-4.7,y-5.1,PCB+1.6,9.4,10.2,5.2,steel,.1))
    electronics.append(cyl(p['ref']+' shaft lower',x,y,PCB+6.8,3,18-6.8,black))
    electronics.append(ring(p['ref']+' 40 teeth shaft, L25 nominal',x,y,PCB+18,3,.15,7,black,n=160,flutes=40))
    electronics.append(cyl(p['ref']+' shaft top',x,y,PCB+24.96,2.82,.04,black))
    electronics.append(cube(p['ref']+' position mark',x-.25,y+.7,PCB+25.01,.5,1.5,.015,ivory))
    label(p['ref'],x,68,S['lower_top']+.155,1.9)

for p in B['buttons']:
    x,y=p['x'],p['y']; square=int(p['ref'][1:])<=8
    electronics.append(cube(p['ref']+' TL1105 switch body',x-3,y-3,PCB,6,6,3.2,black,.1))
    electronics.append(cube(p['ref']+' metal cover',x-2.9,y-2.9,PCB+3.2,5.8,5.8,.3,steel,.08))
    electronics.append(cyl(p['ref']+' actuator nominal',x,y,PCB+3.5,1.4,4,black))
    if square:
        cap=cube(p['ref']+' 1SBLK seated height assumed 13mm',x-2.5,y-2.5,PCB+4,5,5,9,black,.25)
    else:
        cap=cyl(p['ref']+' 1RLGR seated height assumed 13mm',x,y,PCB+4,2.5,9,grey)
        mod=cap.modifiers.new('Cap edge','BEVEL'); mod.width=.18; mod.segments=3
        bpy.context.view_layer.objects.active=cap; bpy.ops.object.modifier_apply(modifier=mod.name)
    electronics.append(cap)
    label(p['ref'],x,72 if square else 109,S['lower_top']+.155,1.7)

for i,p in enumerate(B['toggles']):
    x,y=p['x'],p['y']
    electronics.append(cube(p['ref']+' Dailywell body nominal',x-2.54,y-4.065,PCB,5.08,8.13,8.64,black,.15))
    electronics.append(ring(p['ref']+' B1 threaded bushing',x,y,PCB+8.64,2.475,1.4,5.59,steel))
    electronics.append(ring(p['ref']+' lower panel nut',x,y,S['lower_top'],3.7,2.5,1.6,steel,hexagon=True))
    # Tilt the non-centred switch; end height changes with switch position.
    lever=cyl(p['ref']+' T1 chrome lever',x,y,PCB+14.23,1.27,9.4,chrome)
    if i==0: lever.rotation_euler.x=math.radians(10)
    electronics.append(lever)
    label(p['ref'],x,89.5,S['lower_top']+.155,1.7)

for i,x in enumerate([39.915,62.4,140.4,162.8]):
    electronics.append(cube(f'J{i+1} Neutrik envelope approximation',x-9.1,.5,PCB,18.2,24,15.7,black,.4))
    neck=ring(f'J{i+1} rear jack sleeve approximation',0,0,0,5.7,3.3,7,black)
    electronics.append(horizontal(neck,'rear',x,.5,PCB+8.9))
    rim=ring(f'J{i+1} rear jack rim approximation',0,0,0,6.35,3.3,.75,steel)
    electronics.append(horizontal(rim,'rear',x,-6.5,PCB+8.9))

for j,y in enumerate([20.3,44.3,68.3]):
    electronics.append(cube(f'DIN {j} housing approximation',179.8,y-10,PCB,17,20,20,black,.5))
    face=ring(f'DIN {j} circular collar',0,0,0,9.5,7.5,5.2,black)
    electronics.append(horizontal(face,'side',196.8,y,PCB+10))
    disk=cyl(f'DIN {j} socket back',0,0,0,7.45,1,black)
    # The base of a Blender cylinder is at zero after removing its centre offset.
    disk.data.transform(Matrix.Translation((0,0,.5)))
    electronics.append(horizontal(disk,'side',198.1,y,PCB+10))
    for a in [0,45,90,135,180]:
        theta=math.radians(a)
        socket=ring(f'DIN {j} pin socket',0,0,0,.85,.55,.35,steel,n=32)
        electronics.append(horizontal(socket,'side',199.25,y+4*math.cos(theta),PCB+10+4*math.sin(theta)))

x,y,w,d=A['headphone_body_source_xywh']
electronics.append(cube('J5 SJ-3523-SMT body nominal',x,y,PCB,w,d,A['headphone_body_height'],black,.15))
collar=ring('J5 open socket collar nominal',0,0,0,2.5,1.8,2.5,black)
collar.location=(173.6,-108.2,PCB+A['headphone_axis_above_pcb'])
collar.rotation_euler.x=math.pi/2
electronics.append(collar)
electronics.append(cyl('RV1 low profile body nominal',191.325,91.6,PCB,5.2,1.5,black))
electronics.append(ring('RV1 RK10J12R0A0B disk nominal 14x1mm',191.325,91.6,PCB+1.5,7,.5,1,black,n=128,flutes=64))
label('DAISY',8,32,S['lower_top']+.155,3.2)
label('DESKTOP',8,37,S['lower_top']+.155,1.7)
for p in C['service_positions_provisional']:
    label(p['name'],p['x']+8,p['y']+1,PCB+18.42,1.6)

scene=bpy.context.scene
scene.unit_settings.system='METRIC'; scene.unit_settings.scale_length=.001
scene.render.engine='CYCLES'; scene.cycles.samples=96; scene.cycles.use_denoising=True
scene.cycles.adaptive_threshold=.015; scene.cycles.max_bounces=12; scene.cycles.transmission_bounces=10
devices=[]
try:
    pref=bpy.context.preferences.addons['cycles'].preferences
    pref.compute_device_type='OPTIX'; pref.get_devices()
    for d in pref.devices:
        d.use=d.type=='OPTIX'
        if d.use: devices.append(d.name)
    if devices: scene.cycles.device='GPU'
except Exception as exc: print('GPU fallback',str(exc))
print('RENDER DEVICES',devices or ['CPU'])
scene.render.resolution_x=2400; scene.render.resolution_y=1600; scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'; scene.view_settings.view_transform='AgX'
scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.45,.45,.45,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.35
ground_mat=material('Studio backdrop',(.11,.13,.14),.68)
ground=cube('Studio ground',-10000,-10000,-1.05,20000,20000,1,ground_mat)
for loc,energy,size in [((0,-60,350),1900000,240),((250,160,200),1400000,180),((-170,-120,140),650000,130)]:
    bpy.ops.object.light_add(type='AREA',location=loc); lamp=bpy.context.object
    lamp.data.energy=energy; lamp.data.shape='DISK'; lamp.data.size=size
    lamp.rotation_euler=(Vector((98,-55,10))-lamp.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(390,-500,370)); camera=bpy.context.object
scene.camera=camera; camera.data.type='PERSP'; camera.data.lens=70
camera.data.clip_end=100000


def aim(target):
    camera.rotation_euler=(Vector(target)-camera.location).to_track_quat('-Z','Y').to_euler()


def render(name):
    scene.render.filepath=str(OUT/f'{name}.png'); bpy.ops.render.render(write_still=True)


aim((98,-55,18))
for o in bpy.context.scene.objects: o.select_set(False)
assembly=[o for o in bpy.context.scene.objects if o.type in ['MESH','FONT'] and o!=ground]
report=dict(date='2026-10-25',stl_frame=R['stl_frame'],hardware='NICHT GEMESSEN',model_purpose='Assembly visualization; only original case STLs are print parts',
    access_revision=A['revision'],wheel_top_above_pcb=A['wheel_top_above_pcb'],
    units='mm in STL and blend, metres in GLB',devices=devices,
    native_kicad_model_footprints=186,seed_module_pcb_top_above_desktop_assumed=9,
    imported_board_bounds_mm=board_bounds,
    seed3_bounds_mm=seed_bounds,
    seed_transform_right_handed=transform.to_3x3().determinant()>0,
    seed_service_mesh_bounds=seed_services,
    approximations=['Control caps seated height 13mm assumed','Pots standard L25, custom -0085 drawing unverified',
        'Procedural toggles, jack housings, jack/nut threads omitted',
        'Knurlie head/washer outline approximate; no exact Befaco 3D file available',
        'Seed XY/header seating provisional; FBX source is actual Seed3 Rev5',
        'Paper labels are a neutral example; surface relief and shader bevel are visual only'])
for name,o in objects.items():
    report.setdefault('case_parts',{})[name]=dict(location_mm=list(o.location),vertices=len(o.data.vertices))
# Save a complete editable native scene before exploding the layer positions.
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            space=area.spaces.active
            space.clip_end=5000
            space.shading.type='SOLID'; space.shading.color_type='MATERIAL'
            space.region_3d.view_location=(98,-55,20)
            space.region_3d.view_distance=320
            space.region_3d.view_rotation=camera.rotation_euler.to_quaternion()
bpy.ops.file.pack_all()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'desktop-case-realistic.blend'))

# Export the closed case/layer meshes in assembly coordinates, without electronics.
for o in bpy.context.scene.objects: o.select_set(False)
for o in objects.values(): o.select_set(True)
bpy.ops.wm.stl_export(filepath=str(OUT/'case-assembly-view.stl'),export_selected_objects=True,apply_modifiers=False)

# glTF uses metres. Apply an explicit root conversion, keeping the native scene in mm.
matrices={o:o.matrix_world.copy() for o in assembly}
root=bpy.data.objects.new('Assembly millimetres to metres',None); bpy.context.collection.objects.link(root)
for o in assembly:
    o.parent=root; o.matrix_world=matrices[o]; o.select_set(True)
root.select_set(True); root.scale=(.001,.001,.001)
scene.unit_settings.scale_length=1
bpy.ops.export_scene.gltf(filepath=str(OUT/'desktop-case-assembly.glb'),export_format='GLB',use_selection=True,
    export_cameras=False,export_lights=False,export_apply=True)
for o in assembly:
    o.parent=None; o.matrix_world=matrices[o]
bpy.data.objects.remove(root,do_unlink=True)
scene.unit_settings.scale_length=.001
if 'studio' in VIEWS: render('case-realistic-studio')
if 'rear' in VIEWS:
    camera.location=(440,390,310); aim((98,-51,18)); render('case-realistic-rear')
if 'access' in VIEWS:
    # Explicit design gauges, shown only in this image, never in print/GLB exports.
    gauges=[]
    body=ring('Illustrative fully seated plug body OD10mm',0,0,0,5,.1,18,black)
    gauges.append(horizontal(body,'rear',173.6,110.7,PCB+2.5))
    cable=ring('Illustrative headphone cable',0,0,0,1.8,.1,15,black)
    gauges.append(horizontal(cable,'rear',173.6,128.7,PCB+2.5))
    finger_mat=material('Translucent OD14 fingertip design gauge',(.16,.48,.62),.18,0,.65)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32,ring_count=16,radius=7,location=(205.2,-93.8,19.9))
    fingertip=bpy.context.object; fingertip.name='OD14 fingertip gauge, not a real hand model'
    fingertip.data.materials.append(finger_mat); gauges.append(fingertip)
    camera.location=(355,-265,155); camera.data.lens=105; aim((185,-103,17))
    render('case-realistic-right-access')
    for o in gauges: bpy.data.objects.remove(o,do_unlink=True)
if 'exploded' in VIEWS:
    for name,delta in [('unified-carrier',28),('lower-acrylic-reference',64),('paper-reference',81),('upper-acrylic-reference',98)]:
        objects[name].location.z+=delta
    for o in hardware:
        if 'Knurlie' in o.name: o.location.z+=103
    for o in labels: o.hide_render=True
    camera.location=(400,-520,430); camera.data.lens=62; aim((98,-55,50))
    render('case-realistic-exploded')
report['source_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in
    [Path(__file__),ROOT/'prepare_render_board.py',ROOT/'compact_render_board.py',
     ROOT/'check_stl_orientation.py',ROOT/'validate_realistic_case.py',ROOT/'check_right_access.py',
     ROOT/'right-controls.json',PARTS/'validation.json']}
report['artifact_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(OUT.iterdir())
    if p.name.startswith(('desktop-case-','case-realistic-','case-assembly-')) and p.suffix in ['.glb','.stl','.blend','.png']}
(OUT/'render-manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n')
print('REALISTIC CASE COMPLETE',json.dumps({'output':str(OUT),'mesh_objects':len(assembly),'views':sorted(VIEWS)}))
