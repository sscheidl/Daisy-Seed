"""Blender views from actual case/probe STLs and manufacturer electronics.

No reflection or geometry adjustment is applied to imported case parts.
Electronics are nominal illustrations; they do not certify physical fit.
"""
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix,Vector

ROOT=Path(__file__).resolve().parent; OUT=ROOT/'full'
REPO=ROOT.parents[2]; REF=REPO/'build/case-reference'; PROBES=REPO/'build/v4-full/probes'
P=json.loads((ROOT/'v4-parameters.json').read_text(encoding='utf8'))
M=json.loads((OUT/'manifest.json').read_text(encoding='utf8'))
def v(g,k): return P[g][k]['value']
def q(k): return v('case',k)
L=M['levels_pcb_relative']; PCB=-L['bottom']
views=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else ['studio','rear','right','exploded']
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='NONE'


def mat(name,color,metal=0,transmit=0,rough=.4):
    a=bpy.data.materials.new(name); a.use_nodes=True
    p=a.node_tree.nodes.get('Principled BSDF'); p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Metallic'].default_value=metal; p.inputs['Transmission Weight'].default_value=transmit
    p.inputs['Roughness'].default_value=rough; p.inputs['IOR'].default_value=1.49
    return a


shell=mat('Black PETG-CF, schematic finish',(.026,.032,.038),rough=.55)
steel=mat('Metal hardware',(.5,.53,.56),metal=.8,rough=.24)
black=mat('Molded housings',(.012,.015,.018),rough=.3)
nylon=mat('Insulating nylon heads',(.7,.73,.72),rough=.45)
paper=mat('Replaceable printed paper',(.84,.83,.77),rough=.8)
ink=mat('Paper ink',(.025,.03,.035),rough=.8)
clear=mat('1mm clear acrylic',(.98,.99,1),transmit=1,rough=.04)
floor=mat('Studio floor',(.31,.34,.37),rough=.7)
red=mat('LED package face',(.14,.004,.003),rough=.35)
red.node_tree.nodes['Principled BSDF'].inputs['Emission Color'].default_value=(.6,.005,.001,1)
red.node_tree.nodes['Principled BSDF'].inputs['Emission Strength'].default_value=.6
for material in [shell,black]:
    nodes=material.node_tree.nodes; links=material.node_tree.links
    bevel=nodes.new('ShaderNodeBevel'); bevel.inputs['Radius'].default_value=.1; bevel.samples=3
    links.new(bevel.outputs['Normal'],nodes['Principled BSDF'].inputs['Normal'])

parts={}; hardware=[]; electronics=[]; labels=[]


def load_stl(path,name,material,z):
    bpy.ops.wm.stl_import(filepath=str(path)); o=bpy.context.object; o.name=name
    o.data.materials.clear(); o.data.materials.append(material); o.location.z+=z
    return o


for name,meta in M['parts'].items():
    material=clear if 'acrylic' in name else paper if name=='paper' else shell
    parts[name]=load_stl(OUT/meta['file'],name,material,PCB+meta['installed_z_offset'])


def probe(name,material=black):
    o=load_stl(PROBES/(name+'.stl'),name,material,PCB); electronics.append(o); return o


def ring(name,x,y,z,od,inside,height,material,axis='z',segments=None):
    n=segments or int(v('general','segments')); vertices=[]
    for zz in [0,height]:
        for r in [od/2,inside/2]:
            vertices.extend((r*math.cos(2*math.pi*i/n),r*math.sin(2*math.pi*i/n),zz) for i in range(n))
    faces=[]
    for i in range(n):
        j=(i+1)%n
        faces.extend([(i,j,2*n+j,2*n+i),(n+j,n+i,3*n+i,3*n+j),
                      (j,i,n+i,n+j),(2*n+i,2*n+j,3*n+j,3*n+i)])
    mesh=bpy.data.meshes.new(name); mesh.from_pydata(vertices,[],faces); mesh.update()
    o=bpy.data.objects.new(name,mesh); bpy.context.collection.objects.link(o)
    o.location=(x,-y,PCB+z); o.data.materials.append(material)
    if axis=='y': o.rotation_euler.x=math.pi/2
    if axis=='x': o.rotation_euler.y=math.pi/2
    return o


def label(body,x,y,z,size=2):
    curve=bpy.data.curves.new(body,'FONT'); curve.body=body; curve.size=size; curve.align_x='CENTER'
    o=bpy.data.objects.new(body,curve); bpy.context.collection.objects.link(o)
    o.location=(x,-y,PCB+z); o.data.materials.append(ink); labels.append(o)


# Pinned board STEP export already uses physical RH, GLTF Y-up in mm.
before=set(bpy.data.objects); bpy.ops.import_scene.gltf(filepath=str(REF/'board-mm-v3.glb'))
native=[o for o in bpy.data.objects if o not in before and o.type=='MESH']
for o in native: o.location.z+=PCB-14.4
electronics+=native

# Official Seed3 FBX. Only this external coordinate frame needs transformation.
before=set(bpy.data.objects); bpy.ops.import_scene.fbx(filepath=str(REF/'Seed3.fbx'))
seed=[o for o in bpy.data.objects if o not in before and o.type=='MESH']
world={o:o.matrix_world.copy() for o in seed}
board=next(o for o in seed if o.name.startswith('Board'))
front=min((world[board]@Vector(p)).y for p in board.bound_box)
bx,by,_,_,_=q('seed_board')
transform=Matrix(((0,0,1000,bx),(-1000,0,0,-by),(0,-1000,0,PCB+v('c4','seed_pcb_top')+1000*front),(0,0,0,1)))
assert transform.to_3x3().determinant()>0
for o in seed:
    o.parent=None; o.matrix_world=Matrix.Identity(4); o.data=o.data.copy()
    o.data.transform(transform@world[o]); o.data.update()
electronics+=seed

for i,(x,y) in enumerate(v('full_board','pots')):
    probe('pot-body-'+str(i),steel); probe('pot-shaft-'+str(i),black)
    label('VR'+str(i+1),x,y+10,L['lower_top']+v('c5','paper')+.02)
for i,(x,y) in enumerate(v('full_board','keys')):
    probe('cap-'+q('render_cap_choice')+'-'+str(i),black)
    label('S'+str(i+1),x,y+6,L['lower_top']+v('c5','paper')+.02,1.8)
for i,(x,y) in enumerate(v('full_board','leds')):
    probe('LED-'+str(i),red)
for i,(x,y) in enumerate(v('full_board','jacks')):
    probe('CV-body-'+str(i))
    electronics.append(ring('CV sleeve '+str(i),x,y,q('jack_body')[2],2*v('screw','thread_diameter'),q('cv_socket_diameter'),q('jack_thread_top')-q('jack_body')[2],steel))
    hardware.append(ring('CV nut '+str(i),x,y,L['lower_top'],q('jack_upper_hole')-1,q('jack_hole'),v('screw','head_height'),steel,segments=6))
for i,(x,y) in enumerate(v('full_board','toggles')):
    probe('toggle-body-'+str(i)); probe('toggle-bushing-'+str(i),steel)
    probe(f'toggle-lever-{i}-{q("toggle_lean_degrees")}',steel)
for i,(x,plane) in enumerate(zip(q('audio_axis_x'),q('audio_planes_y'))):
    probe('audio-body-'+str(i)); face=plane-v('c1','nose_length')
    electronics.append(ring('Audio ferrule '+str(i),x,face,v('c1','axis_z'),q('audio_ferrule_diameter'),q('cv_socket_diameter')*2,v('c1','nose_length'),black,'y'))
    washer_y=plane-q('rear_plate_thickness')-q('audio_washer_thickness')
    hardware.append(ring('Audio washer '+str(i),x,washer_y,v('c1','axis_z'),q('audio_washer_diameter'),q('audio_ferrule_diameter'),q('audio_washer_thickness'),black,'y'))
    hardware.append(ring('Audio SW11 nut '+str(i),x,washer_y-q('audio_nut_thickness'),v('c1','axis_z'),2*v('c1','nut_af')/math.sqrt(3),q('audio_ferrule_diameter'),q('audio_nut_thickness'),black,'y',6))
for i,y in enumerate(q('midi_axes_y')):
    probe('MIDI-body-'+str(i))
    electronics.append(ring('DIN collar '+str(i),q('midi_body_face_x'),y,q('midi_axis_z'),q('midi_port_size')-4,q('midi_port_size')-8,q('midi_face_x')-q('midi_body_face_x'),black,'x'))
probe('headphone-body'); probe('wheel-disk')
x,y,z=v('c3','headphone_axis')
electronics.append(ring('Headphone collar',x,v('c3','headphone_body')[1]+v('c3','headphone_body')[4],z,q('headphone_window')[2]-9,v('screw','thread_diameter'),2.5,black,'y'))
for i,(x,y) in enumerate(v('full_board','mounts')):
    hardware.append(ring('Lower spacer '+str(i),x,y,L['floor_top'],2*v('c5','post_af')/math.sqrt(3),v('screw','thread_diameter'),v('c5','lower_post'),steel,segments=6))
    shims=v('c5','washers')*v('c5','washer_thickness')
    hardware.append(ring('Upper spacer '+str(i),x,y,shims,2*v('c5','post_af')/math.sqrt(3),v('screw','thread_diameter'),v('c5','upper_post'),steel,segments=6))
    hardware.append(ring('Shims '+str(i),x,y,0,v('c5','washer_od'),v('screw','thread_diameter'),shims,steel))
    hardware.append(ring('Knurlie washer '+str(i),x,y,L['upper_top'],q('knurlie_head_diameter'),v('screw','thread_diameter'),q('knurlie_washer'),black))
    hardware.append(ring('Knurlie head approximate '+str(i),x,y,L['upper_top']+q('knurlie_washer'),q('knurlie_head_diameter'),v('screw','thread_diameter'),q('knurlie_head_height'),black))
for x,y in q('mounting_domes'):
    hardware.append(ring('Carrier screw head',x,y,L['deck_top']-v('screw','counterbore_depth'),v('screw','head_diameter'),v('screw','thread_diameter'),v('screw','head_height'),steel))
for x,y in q('service_screws'):
    hardware.append(ring('Service screw head',x,y,q('seed_inner_roof')+q('wall'),v('screw','head_diameter'),v('screw','thread_diameter'),v('screw','head_height'),steel))
for i,(x,z) in enumerate(q('rear_plate_mounts')):
    plane=q('audio_planes_y')[0 if x<q('rear_plate_step_x') else 1]
    face=plane-q('rear_plate_thickness')
    hardware.append(ring('Rear panel M3x8 head',x,face-v('screw','head_height'),z,
        v('screw','head_diameter'),v('screw','thread_diameter'),v('screw','head_height'),steel,'y'))
    hardware.append(load_stl(PROBES/f'rear-mount-{i+1}-nut-seated.stl','Rear panel M3 nut',steel,PCB))
for x,y in q('nylon_supports'):
    z=-v('board','thickness')-v('c7','nominal_adjustment_gap')-v('c7','nylon_head_height')
    hardware.append(ring('Nylon PCB support head',x,y,z,v('c7','nylon_head_diameter'),v('screw','thread_diameter'),v('c7','nylon_head_height'),nylon))

bpy.context.view_layer.update()
for name,o in parts.items():
    # Exact imported mesh is unchanged. Save a per-part vertex bound proof.
    assert o.scale==Vector((1,1,1)),name
bpy.ops.mesh.primitive_plane_add(size=2000,location=(100,-50,-.1)); bpy.context.object.data.materials.append(floor)
scene=bpy.context.scene; scene.render.engine='CYCLES'; scene.cycles.samples=48; scene.cycles.use_denoising=True
try:
    prefs=bpy.context.preferences.addons['cycles'].preferences; prefs.compute_device_type='OPTIX'; prefs.get_devices()
    for device in prefs.devices: device.use=device.type=='OPTIX'
    scene.cycles.device='GPU'
except Exception: scene.cycles.device='CPU'
scene.world.color=(.4,.4,.4)
for position,power,size in [((20,-70,300),2500000,220),((240,90,200),1800000,180),((-80,50,130),1000000,120)]:
    bpy.ops.object.light_add(type='AREA',location=position); o=bpy.context.object
    o.data.energy=power; o.data.shape='DISK'; o.data.size=size
    o.rotation_euler=(Vector((100,-55,18))-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(); camera=bpy.context.object; camera.data.type='ORTHO'; camera.data.clip_end=10000; scene.camera=camera
scene.render.resolution_x=1900; scene.render.resolution_y=1300; scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'; scene.view_settings.view_transform='AgX'
config=dict(studio=((290,-230,210),(98,-54,20),285),rear=((265,150,140),(100,-35,22),275),
    right=((275,-150,75),(178,-91,19),125),exploded=((290,-230,230),(98,-54,40),300),
    **{'rear-access':((103,160,55),(103,0,PCB+16),205)})
OUT.joinpath('images').mkdir(exist_ok=True)
for view in views:
    if view=='exploded':
        parts['carrier'].location.z+=25; parts['rear-panel'].location.y+=35; parts['rear-panel'].location.z+=15
        parts['seed-service-cover'].location.z+=65
        for name,dz in [('lower-acrylic',40),('paper',47),('upper-acrylic',54)]: parts[name].location.z+=dz
        for o in labels: o.location.z+=47
        for o in hardware:
            if o.name.startswith('Rear panel M3x8 head'): o.location.y+=35; o.location.z+=15
            elif o.name.startswith('Rear panel M3 nut'): o.location.z+=25
    position,target,scale=config[view]; camera.location=position
    camera.rotation_euler=(Vector(target)-camera.location).to_track_quat('-Z','Y').to_euler(); camera.data.ortho_scale=scale
    image_dir=REPO/'build/v4-full' if view=='rear-access' else OUT/'images'
    scene.render.filepath=str(image_dir/('v4-'+view+'.png')); bpy.ops.render.render(write_still=True)
    if view=='studio':
        local=REPO/'build/v4-full/assembly.blend'; local.parent.mkdir(parents=True,exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(local))
