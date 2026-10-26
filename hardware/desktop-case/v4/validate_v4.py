"""Independent validation of actual full-case STL/SVG files, not CAD helpers.

All probe meshes are written and reimported before intersection. Nominal model
clearance does not establish physical fit, strength or exact HYCW USB support.
"""
from datetime import datetime, timezone
import hashlib
import itertools
import json
import math
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

import manifold3d as m
import numpy as np
import trimesh

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'full'
sys.path.insert(0,str(ROOT.parent))
from check_stl_orientation import winding
from validate_v4_coupons import source_box, source_cylinder, to_solid


class Validation:
    def __init__(self):
        self.p=json.loads((ROOT/'v4-parameters.json').read_text(encoding='utf8'))
        self.manifest=json.loads((OUT/'manifest.json').read_text(encoding='utf8'))
        self.meshes={}; self.solids={}; self.parts={}; self.probes={}; self.checks=[]; self.orientation={}
        self.probe_dir=ROOT.parents[2]/'build/v4-full/probes'
        self.probe_dir.mkdir(parents=True,exist_ok=True)
        self.tol=self.v('general','collision_tolerance')
        for name,meta in self.manifest['parts'].items():
            path=OUT/meta['file']; mesh=trimesh.load(path,force='mesh')
            components=len(to_solid(mesh).decompose())
            integrity=bool(mesh.is_watertight and mesh.is_winding_consistent and mesh.volume>0 and components==1 and abs(mesh.bounds[0,2])<1e-5)
            self.parts[name]=dict(file=meta['file'],sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                closed=bool(mesh.is_watertight),outward=bool(mesh.is_winding_consistent and mesh.volume>0),
                components=components,print_size_mm=mesh.extents.round(5).tolist(),passed=integrity)
            mesh.apply_translation((0,0,meta['installed_z_offset']))
            self.meshes[name]=mesh; self.solids[name]=to_solid(mesh)

    def v(self,g,k): return self.p[g][k]['value']
    def q(self,k): return self.v('case',k)

    def probe(self,name,mesh):
        path=self.probe_dir/(name+'.stl'); mesh.export(path)
        loaded=trimesh.load(path,force='mesh')
        assert loaded.is_watertight and loaded.is_winding_consistent and loaded.volume>0,name
        solid=to_solid(loaded)
        self.probes[name]=dict(file=path.relative_to(ROOT.parents[2]).as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        return solid

    def box(self,name,*args): return self.probe(name,source_box(*args))
    def cylinder(self,name,*args,**kwargs): return self.probe(name,source_cylinder(*args,**kwargs))

    def test(self,group,label,probe,names=None,expected_clear=True):
        if names is None: names=list(self.parts)
        for name in names:
            vol=max(0.,float((self.solids[name]^probe).volume()))
            self.checks.append(dict(group=group,probe=label,part=name,intersection_mm3=round(vol,7),
                required_clear=expected_clear,passed=vol<self.tol if expected_clear else True))

    def blocked(self,group,label,probe,name):
        # Touch protection: the rejection probe must hit material, not pass.
        vol=max(0.,float((self.solids[name]^probe).volume()))
        self.checks.append(dict(group=group,probe=label,part=name,intersection_mm3=round(vol,7),
            required_clear=False,required_blocked=True,passed=vol>=self.tol))

    def landmarks(self):
        p=self.p; l=self.manifest['levels_pcb_relative']; z=(l['lower_bottom']+l['lower_top'])/2
        markers={}
        for name,meta in self.manifest['parts'].items():
            a=meta['marker_source']; physical=lambda x:[x[0],-x[1],x[2]]
            markers[name]=[('ID opening/material',physical(a['void']),physical(a['solid']))]
        # Literal PCB landmarks independent of the constructor's coordinate
        # mapping, plus adjacent material: an outside void cannot pass alone.
        x0,y0,w,d=self.q('outer')
        tray_z=2
        markers['tray'] += [('right wheel',(198.5,-91.6,tray_z),(198.5,-108,tray_z)),
            ('left USB',(-3,-41.09,1.68),(-3,-55,1.68)),
            ('rear audio',(39.915,7,2),(90,7,2))]
        markers['tray'] += [('Seed intake vent',(-3,-58,2.9),(-3,-60,2.9))]
        markers['carrier'] += [('S1',(69,-85,8.5),(73,-85,8.5)),
            ('D1 behind S1',(69,-78,8.5),(72,-78,8.5)),
            ('closed wheel roof',(198.5,-91.6,4),(198.5,-91.6,8.5)),
            ('Seed roof vent',(33,-74,18),(35,-74,18))]
        panel_y=-(3.5-self.q('rear_plate_thickness')/2)
        markers['rear-panel'] += [('J4 axis',(39.915,panel_y,8.14),(53,panel_y,8.14))]
        for name in ['lower-acrylic','upper-acrylic','paper']:
            meta=self.manifest['parts'][name]; zz=meta['installed_z_offset']+self.meshes[name].extents[2]/2
            markers[name]+=[('S1 / web',(69,-85,zz),(73,-85,zz))]
        for name,pairs in markers.items():
            mesh=self.meshes[name]; tests=[]
            for label,empty,filled in pairs:
                a,b=winding(mesh,empty),winding(mesh,filled)
                tests.append(dict(landmark=label,void=empty,material=filled,void_winding=round(a,7),material_winding=round(b,7),passed=bool(abs(a)<1e-4 and abs(b-1)<1e-4)))
            mirror=mesh.copy(); mirror.apply_transform(np.array([[1,0,0,0],[0,-1,0,float(mesh.bounds[:,1].sum())],[0,0,1,0],[0,0,0,1]]))
            rejected=not all(abs(winding(mirror,empty))<1e-4 and abs(winding(mirror,filled)-1)<1e-4 for _,empty,filled in pairs)
            self.orientation[name]=dict(checks=tests,reflected_negative_control_rejected=bool(rejected),passed=all(t['passed'] for t in tests) and bool(rejected))

    def svg_checks(self):
        result={}
        for name,thickness in [('lower-acrylic',self.v('c5','lower_acrylic')),('upper-acrylic',self.v('c5','upper_acrylic')),('paper',self.v('c5','paper'))]:
            path=OUT/'sheets'/(name+'.svg'); root=ET.fromstring(path.read_text(encoding='utf8')); polygons=[]
            for item in root.findall('{http://www.w3.org/2000/svg}path'):
                numbers=[float(x) for x in re.findall(r'[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?',item.attrib['d'])]
                polygons.append(np.asarray(numbers).reshape(-1,2))
            area=abs(sum(float(np.sum(a[:,0]*np.roll(a[:,1],-1)-a[:,1]*np.roll(a[:,0],-1)))/2 for a in polygons))
            actual_thickness=float(self.meshes[name].extents[2])
            mesh_area=float(self.meshes[name].volume)/actual_thickness
            def inside(point):
                x,y=point; crossings=0
                for poly in polygons:
                    for a,b in zip(poly,np.roll(poly,-1,axis=0)):
                        if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]: crossings+=1
                return crossings%2==1
            holes=[]
            for i,(x,y) in enumerate(self.v('full_board','keys')):
                holes.append(dict(landmark=f'S{i+1}',passed=not inside((x,y)) and inside((x+4,y))))
            for i,(x,y) in enumerate(self.v('full_board','leds')):
                holes.append(dict(landmark=f'D{i+1}',passed=inside((x,y))==(name!='paper')))
            for group in ['pots','jacks','toggles','mounts']:
                for i,xy in enumerate(self.v('full_board',group)): holes.append(dict(landmark=group+str(i+1),passed=not inside(xy)))
            result[name]=dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),svg_area_mm2=area,stl_area_mm2=mesh_area,
                delta_mm2=abs(area-mesh_area),thickness_delta_mm=abs(actual_thickness-thickness),
                landmarks=holes,passed=abs(area-mesh_area)<.01 and abs(actual_thickness-thickness)<self.v('general','mesh_simplify_tolerance') and all(x['passed'] for x in holes))
        return result

    def run(self):
        v=self.v; q=self.q; l=self.manifest['levels_pcb_relative']
        for a,b in itertools.combinations(self.solids,2): self.test('assembly',a+' / '+b,self.solids[b],[a])
        pcb=to_solid(source_box(0,0,-v('board','thickness'),v('board','width'),v('board','depth'),v('board','thickness')))
        for x,y in v('full_board','mounts'):
            pcb-=to_solid(source_cylinder(x,y,-2*v('board','thickness'),v('board','mount_hole'),3*v('board','thickness')))
        raw=pcb.to_mesh(); pcb=self.probe('PCB',trimesh.Trimesh(vertices=np.asarray(raw.vert_properties)[:,:3],faces=np.asarray(raw.tri_verts),process=True))
        self.test('hardware','PCB',pcb)
        for item in self.p['modeled_component_boxes']['value']:
            self.test('modeled_components',item['ref'],self.box('model-'+item['ref'],*item['xyzwhd']))
        for i,(x,y) in enumerate(v('full_board','pots')):
            w,d,h=q('pot_body'); body=self.box('pot-body-'+str(i),x-w/2,y-d/2,0,w,d,h)
            self.test('controls','pot body '+str(i+1),body)
            shaft=self.cylinder('pot-shaft-'+str(i),x,y,h,q('pot_shaft_diameter'),q('pot_shaft_top')-h)
            self.test('controls','pot shaft '+str(i+1),shaft)
        for i,(x,y) in enumerate(v('full_board','keys')):
            for shape,cap,height in [('square',v('c2','square_cap'),v('c2','cap_height_square')),
                                      ('round',v('c2','round_cap'),v('c2','cap_height_round'))]:
                z=v('c2','cap_top')-height
                mesh=source_box(x-cap/2,y-cap/2,z,cap,cap,height) if shape=='square' else source_cylinder(x,y,z,cap,height)
                capsolid=self.probe(f'cap-{shape}-{i}',mesh)
                for state,dz in [('rest',0),('pressed',-v('c2','stroke'))]:
                    self.test('keys',f'S{i+1} {shape} {state}',capsolid.translate((0,0,dz)))
        for i,(x,y) in enumerate(v('full_board','leds')):
            w,d,h=v('board','led_size'); self.test('led','D'+str(i+1),self.box('LED-'+str(i),x-w/2,y-d/2,0,w,d,h))
            path=self.cylinder('LED-path-'+str(i),x,y,0,q('paper_led_hole'),l['upper_top'])
            self.test('led_paths','D'+str(i+1)+' vertical',path,['carrier','paper'])
        for i,(x,y) in enumerate(v('full_board','jacks')):
            w,d,h=q('jack_body'); self.test('jacks','body '+str(i),self.box('CV-body-'+str(i),x-w/2,y+q('jack_body_offset_y'),0,w,d,h))
            self.test('jacks','thread '+str(i),self.cylinder('CV-thread-'+str(i),x,y,h,v('screw','thread_diameter')*2,q('jack_thread_top')-h))
        for i,(x,y) in enumerate(v('full_board','toggles')):
            w,d,h=q('toggle_body'); self.test('toggles','body '+str(i),self.box('toggle-body-'+str(i),x-w/2,y-d/2,0,w,d,h))
            self.test('toggles','bushing '+str(i),self.cylinder('toggle-bushing-'+str(i),x,y,h,self.v('c2','square_cap'),q('toggle_bushing_top')-h))
            for angle in [-q('toggle_lean_degrees'),q('toggle_lean_degrees')]:
                mesh=source_cylinder(x,y,q('toggle_bushing_top'),q('toggle_lever_diameter'),q('toggle_lever_length'))
                matrix=trimesh.transformations.rotation_matrix(math.radians(angle),[1,0,0],point=[x,-y,q('toggle_bushing_top')])
                mesh.apply_transform(matrix)
                self.test('toggles','lever '+str(i)+' '+str(angle),self.probe(f'toggle-lever-{i}-{angle}',mesh))
        for i,(x,plane) in enumerate(zip(q('audio_axis_x'),q('audio_planes_y'))):
            w,d,h=q('audio_body')
            self.test('audio','jack body '+str(i),self.box('audio-body-'+str(i),x-w/2,plane,0,w,d,h))
            face=plane-v('c1','nose_length')
            self.test('access','audio plug '+str(i),self.cylinder('audio-plug-'+str(i),x,face-q('audio_plug_length'),v('c1','axis_z'),q('audio_plug_diameter'),q('audio_plug_length'),'y'))
            end=plane-q('rear_plate_thickness')
            self.test('tools','OD16 socket '+str(i),self.cylinder('socket-'+str(i),x,end-v('c1','socket_depth'),v('c1','axis_z'),v('c1','socket_diameter'),v('c1','socket_depth'),'y'))
        for i,y in enumerate(q('midi_axes_y')):
            x,w,d,h=q('midi_body'); self.test('midi','body '+str(i),self.box('MIDI-body-'+str(i),x,y-d/2,0,w,d,h))
            self.test('access','MIDI plug '+str(i),self.cylinder('MIDI-plug-'+str(i),q('midi_face_x'),y,q('midi_axis_z'),v('c3','midi_diameter'),v('c3','midi_length'),'x'))
        for key in ['j7','seed']:
            x,y,z=v('c4',key+'_center'); w,d,h=v('c4',key+'_shell')
            if key=='seed': z+=v('c4','seed_pcb_top')
            self.test('usb',key+' shell reference',self.box(key+'-shell',x-w/2,y-d/2,z-h/2,w,d,h))
            self.test('access',key+' full 12x6 plug',self.box(key+'-plug',x-w/2-q('usb_plug_length'),y-v('c4','plug_width')/2,z-v('c4','plug_height')/2,q('usb_plug_length'),v('c4','plug_width'),v('c4','plug_height')))
        # Official Seed3 FBX mesh bounds, independently transformed from its
        # source frame; the 9-mm module seating height remains an assumption.
        for i,item in enumerate(self.p['seed_model_boxes']['value']):
            self.test('seed_model',item['ref'],self.box('seed-'+str(i),*item['xyzwhd']))
        for i,(x,y) in enumerate(q('seed_buttons')):
            probe=self.cylinder('seed-service-'+str(i),x,y,q('seed_button_top'),q('service_finger_diameter'),q('seed_inner_roof')+q('wall')-q('seed_button_top'))
            self.test('access','Seed button '+str(i)+' cover removed',probe,[n for n in self.parts if n!='seed-service-cover'])
        wx,wy=v('c3','wheel_center'); radius=v('c3','wheel_radius'); rf=v('c3','finger_radius')
        self.test('wheel','flat disk',self.cylinder('wheel-disk',wx,wy,v('c3','wheel_z_bottom'),2*radius,v('c3','wheel_thickness')))
        for angle in v('c3','finger_angles'):
            rad=math.radians(angle); mesh=trimesh.creation.icosphere(subdivisions=4,radius=rf)
            mesh.apply_translation((wx+(radius+rf)*math.cos(rad),-(wy+(radius+rf)*math.sin(rad)),v('c3','wheel_z_bottom')+v('c3','wheel_thickness')/2))
            self.test('access','wheel finger '+str(angle),self.probe('wheel-finger-'+str(angle),mesh))
        self.test('headphone','body H5',self.box('headphone-body',*v('c3','headphone_body')))
        x,y,z=v('c3','headphone_axis')
        self.test('access','headphone full plug',self.cylinder('headphone-plug',x,y,z,v('c3','headphone_diameter'),v('c3','headphone_length'),'y'))
        sw,sd,sh=q('sd_card_size'); cy=q('sd_card_center_y'); cz=q('sd_card_z')
        for travel in [0,q('sd_eject_travel')]:
            self.test('access','microSD card '+str(travel),self.box('SD-card-'+str(travel),q('sd_card_face_x')-sw-travel,cy-sd/2,cz-sh/2,sw,sd,sh))
        self.test('access','microSD blunt tool',self.cylinder('SD-tool',-20,cy,cz,v('screw','thread_diameter'),22,'x'))
        postd=2*v('c5','post_af')/math.sqrt(3)
        for i,(x,y) in enumerate(v('full_board','mounts')):
            self.test('metal','lower spacer '+str(i),self.cylinder('lower-spacer-'+str(i),x,y,l['floor_top'],postd,v('c5','lower_post')))
            shims=v('c5','washers')*v('c5','washer_thickness')
            self.test('metal','upper spacer '+str(i),self.cylinder('upper-spacer-'+str(i),x,y,shims,postd,v('c5','upper_post')))
            self.test('metal','post shims '+str(i),self.cylinder('post-shims-'+str(i),x,y,0,v('c5','washer_od'),shims))
            self.test('access','Knurlie head '+str(i),self.cylinder('Knurlie-head-'+str(i),x,y,l['upper_top'],q('knurlie_head_diameter'),q('knurlie_head_height')+q('knurlie_washer')))
        # Vents: a small probe proves each slot is open through the wall, the
        # IP3X-sized rod must be rejected by the same slot (touch protection).
        x0=self.q('outer')[0]; wall=q('wall'); lo,hi=q('tray_vent_z'); zmid=(lo+hi)/2
        for y in q('tray_vent_y'):
            self.test('vents','tray intake open Y'+str(y),self.cylinder(f'vent-tray-open-{y}',x0-1,y,zmid,q('vent_open_probe_diameter'),wall+2,'x'))
            self.blocked('vents','tray intake rejects probe Y'+str(y),self.cylinder(f'vent-tray-probe-{y}',x0-1,y,zmid,q('vent_access_probe_diameter'),wall+2,'x'),'tray')
        y0,y1=q('seed_roof_vent_y'); roof=q('seed_inner_roof')
        for x in q('seed_roof_vent_x'):
            self.test('vents','Seed roof exhaust open X'+str(x),self.cylinder(f'vent-roof-open-{x}',x,(y0+y1)/2,roof-1,q('vent_open_probe_diameter'),wall+2))
            self.blocked('vents','Seed roof exhaust rejects probe X'+str(x),self.cylinder(f'vent-roof-probe-{x}',x,(y0+y1)/2,roof-1,q('vent_access_probe_diameter'),wall+2),'carrier')
        w=q('vent_slot_width')
        self.vent_area=dict(tray_intake_mm2=round(len(q('tray_vent_y'))*w*(hi-lo),3),
            seed_roof_exhaust_mm2=round(len(q('seed_roof_vent_x'))*w*(y1-y0),3))

    def report(self):
        self.landmarks(); sheets=self.svg_checks()
        failures=[x for x in self.checks if not x['passed']]
        fresh=self.manifest['parameters_sha256']==hashlib.sha256((ROOT/'v4-parameters.json').read_bytes()).hexdigest() and self.manifest['generator_sha256']==hashlib.sha256((ROOT/'build_v4.py').read_bytes()).hexdigest()
        fresh=fresh and self.manifest['geometry_helper_sha256']==hashlib.sha256((ROOT/'build_v4_coupons.py').read_bytes()).hexdigest()
        passed=fresh and all(x['passed'] for x in self.parts.values()) and all(x['passed'] for x in self.orientation.values()) and all(x['passed'] for x in sheets.values()) and not failures
        data=dict(generated_at_utc=datetime.now(timezone.utc).isoformat(),hardware='NICHT GEMESSEN',dimensions='NOMINAL',
            passed=passed,inputs_match_export=fresh,meaning='Actual STL/SVG digital integrity, orientation and specified nominal envelopes; not physical fit/force/strength acceptance.',
            parameters_sha256=hashlib.sha256((ROOT/'v4-parameters.json').read_bytes()).hexdigest(),
            validator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),parts=self.parts,
            orientation=self.orientation,svg=sheets,vent_open_area_nominal=self.vent_area,probes=self.probes,collisions=self.checks,failures=failures,
            limitations=['HYCW J7 body/shoulders unmeasured; GCT remains reference. Fit USB saddles with insulating shims without forcing PCB.',
                'Modeled component and Seed boxes are conservative CAD bounds, not measured solder profiles.',
                'Seed seating9, cap height13, toggle shoulder and nuts remain nominal. Recovery access assumes cover removed.',
                'Finger sphere sampled at five positions, no continuous human-hand or friction simulation.',
                'Vent touch protection is nominal slot geometry against a rigid 2.5-mm probe; printed slot width, airflow and operating temperature are not measured.',
                'No mechanical stress, thread torque, fatigue, acoustics or optical performance measured.'])
        (OUT/'validation.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf8',newline='\n')
        print(json.dumps(dict(passed=passed,parts={k:v['passed'] for k,v in self.parts.items()},orientation={k:v['passed'] for k,v in self.orientation.items()},svg=sheets,checks=len(self.checks),failures=failures),indent=2))
        return passed


if __name__=='__main__':
    task=Validation(); task.run()
    if not task.report(): raise SystemExit(1)
