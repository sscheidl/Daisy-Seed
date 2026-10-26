"""Independent post-export validation. Never import construction helpers.

All tested parts and all probes are imported from STL before intersection.
Known nominal access failures are findings, not a disguised fit certification.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import manifold3d as m
import numpy as np
import trimesh

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent))
from check_stl_orientation import evaluate_v4


def to_solid(mesh):
    solid=m.Manifold(m.Mesh(np.asarray(mesh.vertices,dtype=np.float32),
                           np.asarray(mesh.faces,dtype=np.uint32)))
    if solid.status()!=m.Error.NoError:
        raise ValueError(f"Invalid imported solid: {solid.status()}")
    return solid


def source_box(x,y,z,w,d,h):
    a=trimesh.creation.box(extents=[w,d,h])
    a.apply_translation((x+w/2,-y-d/2,z+h/2))
    return a


def source_cylinder(x,y,z,d,h,axis="z",sections=80):
    a=trimesh.creation.cylinder(radius=d/2,height=h,sections=sections)
    if axis=="x":
        a.apply_transform(trimesh.transformations.rotation_matrix(math.pi/2,[0,1,0]))
        a.apply_translation((x+h/2,-y,z))
    elif axis=="y":
        a.apply_transform(trimesh.transformations.rotation_matrix(math.pi/2,[1,0,0]))
        a.apply_translation((x,-y-h/2,z))
    else: a.apply_translation((x,-y,z+h/2))
    return a


class Check:
    def __init__(self,parameters):
        self.p=json.loads(parameters.read_text(encoding="utf8"))
        self.manifest=json.loads((ROOT/"coupons/manifest.json").read_text(encoding="utf8"))
        self.parts={}; self.solids={}; self.results=[]
        self.probe_records={}
        self.probes=ROOT.parents[2]/"build/v4-checks/probes"
        self.probes.mkdir(parents=True,exist_ok=True)
        self.tol=self.v("general","collision_tolerance")
        for name,meta in self.manifest["parts"].items():
            path=ROOT/"coupons"/meta["file"]
            mesh=trimesh.load(path,force="mesh")
            solid=to_solid(mesh)
            components=len(solid.decompose())
            self.parts[name]=dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                watertight=bool(mesh.is_watertight),consistent_outward=bool(mesh.is_winding_consistent and mesh.volume>0),
                connected_components=components,volume_mm3=round(float(mesh.volume),5),
                bounds_mm=mesh.bounds.round(5).tolist(),bed_z_mm=float(mesh.bounds[0,2]),
                passed=bool(mesh.is_watertight and mesh.is_winding_consistent and mesh.volume>0 and components==1 and abs(mesh.bounds[0,2])<1e-5))
            mesh.apply_translation((0,0,meta["installed_z_offset"]))
            self.solids[name]=to_solid(mesh)

    def v(self,g,k): return self.p[g][k]["value"]

    def probe(self,name,mesh):
        path=self.probes/(name+".stl")
        mesh.export(path)
        imported=trimesh.load(path,force="mesh")
        assert imported.is_watertight and imported.is_winding_consistent and imported.volume>0,name
        self.probe_records[name]=dict(file=str(path.relative_to(ROOT.parents[2])).replace('\\','/'),
            sha256=hashlib.sha256(path.read_bytes()).hexdigest(),watertight=True)
        return to_solid(imported)

    def hit(self,name,part,probe,classification="clearance",expected_clear=True,note=""):
        volume=max(0.,float((self.solids[part]^probe).volume()))
        self.results.append(dict(check=name,part=part,intersection_mm3=round(volume,7),
            classification=classification,nominal_clear=volume<self.tol,
            required_clear_in_this_coupon=expected_clear,note=note))

    def pair(self,a,b,note=""):
        self.hit(a+" / "+b,a,self.solids[b],note=note)

    def pcb(self):
        a=to_solid(source_box(0,0,-self.v("board","thickness"),self.v("board","width"),self.v("board","depth"),self.v("board","thickness")))
        x,y=self.v("board","h4_xy")
        a-=to_solid(source_cylinder(x,y,-2*self.v("board","thickness"),self.v("board","mount_hole"),3*self.v("board","thickness")))
        raw=a.to_mesh()
        return self.probe("PCB-with-H4",trimesh.Trimesh(vertices=np.asarray(raw.vert_properties)[:,:3],faces=np.asarray(raw.tri_verts),process=True))

    def run(self):
        v=self.v; pcb=self.pcb()
        for name,meta in self.manifest["parts"].items():
            family=meta["family"]
            if family=="C1":
                self.hit("PCB clearance",name,pcb)
                if meta['variant']=='A':
                    index=v('c1','thicknesses').index(meta['thickness'])
                    length=v('c1','closure_screw_lengths')[index]
                    for i,x in enumerate(v('c1','screw_x')):
                        y=v('c1','planes_y')[i]-meta['thickness']
                        z=v('c1','split_z')+v('c1','closure_depth')/2
                        shaft=self.probe(f"C1-M3-shaft-{index}-{i}",source_cylinder(x,y,z,v('screw','thread_diameter'),length,'y'))
                        self.hit('horizontal closing-strip screw',name,shaft)
                for i,(x,y) in enumerate(zip(v("c1","axes_x"),v("c1","planes_y"))):
                    bore=self.probe(f"jack-bore-{meta['thickness']}-{i}",source_cylinder(x,y-meta["thickness"],v("c1","axis_z"),v("c1","hole"),meta["thickness"],"y"))
                    self.hit(f"closed nominal bore gauge {i+1}",name,bore)
                    # Gauge ends at the front panel face: no invented nut length.
                    end=y-meta["thickness"]
                    start=end-v("c1","socket_depth")
                    gauge=self.probe(f"socket-{meta['thickness']}-{i}",source_cylinder(x,start,v("c1","axis_z"),v("c1","socket_diameter"),v("c1","socket_depth"),"y"))
                    self.hit(f"OD16 socket {i+1}",name,gauge)
                    if name.endswith("upper"):
                        # Broad generic wrench head, not a measured tool. It
                        # intentionally exposes the PCB-space concern.
                        wr=self.probe(f"wrench-{meta['thickness']}-{i}",source_cylinder(x,end-v("c1","wrench_depth"),v("c1","axis_z"),v("c1","wrench_outer_width"),v("c1","wrench_depth"),"y"))
                        vol=float((wr^pcb).volume())
                        self.results.append(dict(check="generic SW11 head / PCB",part=name,
                            intersection_mm3=round(max(0,vol),7),classification="tool_trial",
                            nominal_clear=vol<self.tol,required_clear_in_this_coupon=False,
                            note="20-mm outer-head envelope; actual wrench is not measured. Keep wrench outside PCB edge or choose thinner tool."))
            elif family=="C3":
                self.hit("PCB clearance incl H4 hole",name,pcb)
                wx,wy=v("c3","wheel_center"); r=v("c3","wheel_radius")
                wheel=self.probe("wheel-disk",source_cylinder(wx,wy,v("c3","wheel_z_bottom"),2*r,v("c3","wheel_thickness")))
                self.hit("flat toothed disk envelope",name,wheel)
                mx,my,mz=v("c3","midi_axis")
                midi=self.probe("MIDI-inserted",source_cylinder(mx,my,mz,v("c3","midi_diameter"),v("c3","midi_length"),"x"))
                self.hit("J10 inserted MIDI body",name,midi)
                hx,hy,hz=v("c3","headphone_axis")
                phone=self.probe("headphone-body",source_cylinder(hx,hy,hz,v("c3","headphone_diameter"),v("c3","headphone_length"),"y"))
                self.hit("J5 inserted headphone body",name,phone)
                body=self.probe("J5-housing",source_box(*v("c3","headphone_body")))
                self.hit("J5 housing H5",name,body)
                h4x,h4y=v('board','h4_xy'); postd=2*v('c5','post_af')/math.sqrt(3)
                lowerpost=self.probe('H4-lower-metal-envelope',source_cylinder(h4x,h4y,-v('board','thickness')-v('c5','lower_post'),postd,v('c5','lower_post')))
                upperpost=self.probe('H4-upper-metal-envelope',source_cylinder(h4x,h4y,v('c5','washers')*v('c5','washer_thickness'),postd,v('c5','upper_post')))
                washer=self.probe('H4-shim-envelope',source_cylinder(h4x,h4y,0,v('c5','washer_od'),v('c5','washers')*v('c5','washer_thickness')))
                for label,probe in [('lower post',lowerpost),('upper post',upperpost),('shims',washer)]: self.hit('H4 '+label,name,probe)
                for angle in v("c3","finger_angles"):
                    rad=math.radians(angle); rf=v("c3","finger_radius")
                    # Finger travels in the wheel plane; it is tangent to the
                    # flat wheel rim, not to a fictitious spherical knob.
                    a=trimesh.creation.icosphere(subdivisions=4,radius=rf)
                    a.apply_translation((wx+(r+rf)*math.cos(rad),-(wy+(r+rf)*math.sin(rad)),v("c3","wheel_z_bottom")+v("c3","wheel_thickness")/2))
                    finger=self.probe("finger-"+str(angle),a)
                    self.hit(f"finger tangent at {angle} degrees",name,finger,
                        classification="access_trial",expected_clear=meta["face_x"]==v("c3","primary_face_x"),
                        note="Full sphere R7, sampled rotation path; X200 is deliberately retained as comparison, not accepted geometry.")
                    # Under-board access is checked against actual PCB too.
                    vol=float((finger^pcb).volume())
                    self.results.append(dict(check=f"finger {angle} / PCB",part=name,intersection_mm3=round(max(0,vol),7),
                        classification="clearance",nominal_clear=vol<self.tol,required_clear_in_this_coupon=True,note=""))
            elif family=="C4":
                key=meta["connector"]; cx,cy,cz=v("c4",key+"_center"); sx,sy,sz=v("c4",key+"_shell")
                if key=="seed": cz+=v("c4","seed_pcb_top")
                shell=self.probe(key+"-shell-reference",source_box(cx-sx/2,cy-sy/2,cz-sz/2,sx,sy,sz))
                plug=self.probe(key+"-plug-12x6",source_box(cx-sx/2-v("c4","plug_length"),cy-v("c4","plug_width")/2,cz-v("c4","plug_height")/2,v("c4","plug_length"),v("c4","plug_width"),v("c4","plug_height")))
                self.hit("12x6 plug body full insertion",name,plug)
                self.hit("reference shield envelope",name,shell)
                self.hit("main PCB plane",name,pcb)
                if key=="seed":
                    bx,by,bw,bd,bh=v("c4","seed_pcb_box")
                    module=self.probe("Seed-PCB-conservative-box",source_box(bx,by,v("c4","seed_pcb_top")-bh,bw,bd,bh))
                    self.hit("Seed PCB conservative rectangle",name,module)
                if meta["role"]=="cradle":
                    self.pair(name,name.replace("cradle","bridge"))
                    # Remove bridge, lift cradle first, THEN lift module. A
                    # module must not be lifted into an installed cradle.
                    for dz in np.linspace(0,v("c4","removal_lift"),int(v("c4","removal_steps"))):
                        moved=self.solids[name].translate((0,0,float(dz)))
                        vol=max(0.,float((moved^shell).volume()))
                        self.results.append(dict(check=f"cradle removal +Z {dz:g}",part=name,intersection_mm3=round(vol,7),classification="clearance",nominal_clear=vol<self.tol,required_clear_in_this_coupon=True,note="Bridge removed first; shell envelope only. Neighbor SMDs not modeled."))
            elif family=="C6":
                ky=v("board","key_y"); ly=ky-v("board","led_offset")
                pipes=[]
                for i in range(int(v("c6","pairs"))):
                    x=v("board","key_x")+i*v("board","pitch")
                    ledw,ledd,ledh=v("board","led_size")
                    led=self.probe(f"LED-{i}",source_box(x-ledw/2,ly-ledd/2,0,ledw,ledd,ledh))
                    self.hit("LED 0603 envelope "+str(i+1),name,led)
                    cap=self.probe(f"cap-{i}",source_box(x-v("c2","square_cap")/2,ky-v("c2","square_cap")/2,
                        v("c2","cap_top")-v("c2","cap_height_square"),v("c2","square_cap"),v("c2","square_cap"),v("c2","cap_height_square")))
                    self.hit("square cap at rest "+str(i+1),name,cap)
                    tag=meta["variant"]
                    if tag.startswith("A"):
                        z=ledh+v("c6","led_air"); h=v("c6","lower_plate_z")-v("c6","top_gap")-z
                        rod=self.probe(f"PMMA-rod-{i}",source_cylinder(x,ly,z,v("c6","rod_diameter"),h))
                        pipes.append(rod)
                        self.hit("PMMA rod "+str(i+1),name,rod)
                        self.hit("rod / lower acrylic "+str(i+1),"C6-lower-dummy",rod)
                        self.hit("rod / upper acrylic "+str(i+1),"C6-upper-dummy",rod)
                        vol=max(0.,float((rod^led).volume()))
                        self.results.append(dict(check="rod / LED",part=name,intersection_mm3=round(vol,7),classification="clearance",nominal_clear=vol<self.tol,required_clear_in_this_coupon=True,note=""))
                        pw=v("c6","pocket_width")
                        keep=self.probe(f"flange-pocket-{i}",source_box(x-pw/2,ky-pw/2,v("c6","pocket_floor"),pw,pw,v("c6","pocket_top")-v("c6","pocket_floor")))
                        # Rod versus flange keep-out (both reimported STL).
                        volume=max(0,float((rod^keep).volume()))
                        self.results.append(dict(check="rod / sleeve pocket",part=name,intersection_mm3=round(volume,7),classification="clearance",nominal_clear=volume<self.tol,required_clear_in_this_coupon=True,note=""))
                    if tag=="B":
                        z=v("c6","plp_seat_z")-v("plp2","body_length")
                        shaft=to_solid(source_cylinder(x,ly,z,v("plp2","shaft_diameter"),v("plp2","body_length")))
                        shaft+=to_solid(source_cylinder(x,ly,v("c6","plp_seat_z"),v("plp2","head_diameter"),v("plp2","head_height")))
                        a=shaft.to_mesh()
                        plp=self.probe(f"PLP2-250-{i}",trimesh.Trimesh(vertices=np.asarray(a.vert_properties)[:,:3],faces=np.asarray(a.tri_verts),process=True))
                        pipes.append(plp)
                        self.hit("PLP2 shaft + conservative cylindrical head "+str(i+1),name,plp)
                        self.hit("PLP2 / lower acrylic "+str(i+1),"C6-lower-dummy",plp)
                        self.hit("PLP2 / upper acrylic "+str(i+1),"C6-upper-dummy",plp)
                        vol=float((plp^led).volume())
                        self.results.append(dict(check="PLP2 / LED",part=name,intersection_mm3=round(max(0,vol),7),classification="clearance",nominal_clear=vol<self.tol,required_clear_in_this_coupon=True,note=""))
                        ribs=self.probe(f"PLP2-rib-envelope-{i}",source_cylinder(x,ly,v("c6","plp_seat_z")-v("c6","plp_grip_thickness"),v("plp2","rib_diameter_ref"),v("c6","plp_grip_thickness")))
                        self.hit("REF rib cylinder upper-bound interference "+str(i+1),name,ribs,classification="intentional_press_fit_envelope",expected_clear=False,
                            note="Full ring overestimates discrete ribs; not actual molded rib volume or holding force.")
                if meta["variant"] not in ("lower","upper"):
                    self.pair(name,"C6-lower-dummy"); self.pair(name,"C6-upper-dummy")
                    for tag in ["S","R"]:
                        for i in range(int(v("c6","pairs"))): self.pair(name,f"C2-{tag}-sleeve-{i+1}","C6 compatible with both C2 sleeve shapes")
                if len(pipes)==2:
                    vol=max(0,float((pipes[0]^pipes[1]).volume()))
                    self.results.append(dict(check="neighboring light guides",part=name,intersection_mm3=round(vol,7),classification="clearance",nominal_clear=vol<self.tol,required_clear_in_this_coupon=True,note="Actual reimported guide STLs at 12.285714 pitch."))
            elif family=="C7":
                self.hit("PCB gap",name,pcb)
        # The two halves share a seam with zero nominal interference.
        for t in v("c1","thicknesses"):
            self.pair(f"C1-A{int(t)}-upper",f"C1-A{int(t)}-strip")
        for shape,tag in [("square","S"),("round","R")]:
            carrier=f"C2-{tag}-carrier"; lower=f"C2-{tag}-lower"; upper=f"C2-{tag}-upper"
            self.pair(carrier,lower); self.pair(lower,upper)
            for i in range(int(v("c2","count"))):
                name=f"C2-{tag}-sleeve-{i+1}"
                self.pair(carrier,name); self.pair(lower,name); self.pair(upper,name)
                # A round flange also works with the COMMON square lower plate.
                if tag=="R": self.pair("C2-S-lower",name,"common square guide with round sleeve")
                if tag=="R": self.pair("C2-S-carrier",name,"common square carrier with round sleeve")
                for j in range(i+1,int(v("c2","count"))): self.pair(name,f"C2-{tag}-sleeve-{j+1}")
                x=v("board","key_x")+i*v("board","pitch"); y=v("board","key_y")
                capsize=v("c2","square_cap" if tag=="S" else "round_cap")
                caph=v("c2","cap_height_"+shape); z=v("c2","cap_top")-caph
                mesh=source_box(x-capsize/2,y-capsize/2,z,capsize,capsize,caph) if tag=="S" else source_cylinder(x,y,z,capsize,caph)
                cap=self.probe(f"C2-{tag}-cap-{i}",mesh)
                sw,sd,sh=v('c2','switch_housing_box')
                housing=self.probe(f'switch-housing-{i}',source_box(x-sw/2,y-sd/2,0,sw,sd,sh))
                for part in [carrier,name]: self.hit('conservative TL1105 housing',part,housing)
                moving=self.solids[name].translate((0,0,-v('c2','stroke')))
                volume=max(0,float((moving^housing).volume()))
                self.results.append(dict(check=name+' pressed / switch housing',part=name,intersection_mm3=round(volume,7),classification='clearance',nominal_clear=volume<self.tol,required_clear_in_this_coupon=True,note='Conservative 6x6x4 housing, sleeve open below. Actuator and actual cap socket must be measured.'))
                for part in [carrier,lower,upper]: self.hit("cap at rest",part,cap)
                for part in [carrier,lower,upper]: self.hit("cap pressed",part,cap.translate((0,0,-v("c2","stroke"))))
                self.hit("trial gripping teeth / stock cap envelope",name,cap,
                    classification="intentional_cap_grip_trial",expected_clear=False,
                    note="Elastic tooth interference, NOT a verified undercut. Grip force, snap action and return unmeasured.")
                # Stroke test: translate the reimported sleeve and cap, not a
                # freshly regenerated construction shape.
                moving=self.solids[name].translate((0,0,-v("c2","stroke")))
                for fixed in [carrier,lower,upper]:
                    volume=max(0,float((self.solids[fixed]^moving).volume()))
                    self.results.append(dict(check=name+" pressed stroke",part=fixed,intersection_mm3=round(volume,7),classification="clearance",nominal_clear=volume<self.tol,required_clear_in_this_coupon=True,note="Typical .25 stroke only; actual maximum and switch housing must be measured."))
                lift=v("c2","carrier_top")-v("c2","flange_bottom")-v("c2","flange_thickness")+v("general","csg_margin")
                lifted=self.solids[name].translate((0,0,lift))
                volume=max(0,float((self.solids[lower]^lifted).volume()))
                self.results.append(dict(check=name+" flange extraction stop",part=lower,intersection_mm3=round(volume,7),classification="expected_positive_stop",nominal_clear=volume<self.tol,required_clear_in_this_coupon=False,required_contact=True,note="Deliberately lift 0.05 past nominal stop: flange must contact FIXED lower plate, with upper plate removed."))
                volume=max(0,float((self.solids[upper]^lifted).volume()))
                self.results.append(dict(check=name+" lifted sleeve / upper plate",part=upper,intersection_mm3=round(volume,7),classification="clearance",nominal_clear=volume<self.tol,required_clear_in_this_coupon=True,note="Fixed lower plate stops lift before sleeve can reach paper or upper plate."))
        for a,b in [("C5-floor","C5-carrier"),("C5-floor","C5-PCB-dummy"),("C5-PCB-dummy","C5-carrier"),("C5-carrier","C5-lower"),("C5-lower","C5-upper")]: self.pair(a,b)
        self.pair("C6-lower-dummy","C6-upper-dummy")
        # Check nylon head/shaft at nominal non-preloaded setting, against the
        # reimported adjustable dome and actual PCB probe.
        x,y=v("c7","adjustable_xy")
        top=-v("board","thickness")-v("c7","nominal_adjustment_gap")
        neck=top-v("c7","nylon_head_height")
        head=self.probe("C7-nylon-head",source_cylinder(x,y,neck,v("c7","nylon_head_diameter"),v("c7","nylon_head_height")))
        self.hit("nylon head / dome", "C7-A",head)
        vol=float((head^pcb).volume())
        self.results.append(dict(check="nylon head / PCB at nominal gap",part="C7-A",intersection_mm3=round(max(0,vol),7),classification="clearance",nominal_clear=vol<self.tol,required_clear_in_this_coupon=True,note="M3x10 nylon screw from above; adjust before fitting PCB."))

    def calculations(self):
        v=self.v
        floor=v("c5","floor"); cb=v("screw","counterbore_depth")
        bottom_eng=v("c5","bottom_screw")-(floor-cb)
        male=v("c5","male_thread")-v("c5","washers")*v("c5","washer_thickness")-v("board","thickness")
        acrylic=v("c5","lower_acrylic")+v("c5","paper")+v("c5","upper_acrylic")
        top=floor+v("c5","lower_post")+v("board","thickness")
        source=v("c6","lower_plate_z"); ledh=v("board","led_size")[2]
        wheelz=v("c3","wheel_z_bottom")+v("c3","wheel_thickness")/2
        finger_planes=[]
        for face in v("c3","faces_x"):
            for angle in v("c3","finger_angles"):
                rf=v("c3","finger_radius"); rw=v("c3","wheel_radius")
                center=v("c3","wheel_center")[0]+(rw+rf)*math.cos(math.radians(angle))
                span=math.sqrt(max(0,rf*rf-(center-face)**2))
                finger_planes.append(dict(face_x=face,angle_degrees=angle,
                    z_min=wheelz-span,z_max=wheelz+span,
                    nominal_top_clearance=v("c3","window_top")-(wheelz+span)))
        return dict(status="NOMINAL",hardware="NICHT GEMESSEN",finger_outer_plane=finger_planes,
            stack=dict(pcb_absolute_z=top,lower_acrylic_absolute_z=top+v("c5","plate_above_pcb"),
                upper_plate_top_absolute_z=top+v("c5","plate_above_pcb")+acrylic,
                bottom_screw_engagement=bottom_eng,male_in_lower_post=male,
                opposing_tip_gap=v("c5","lower_post")-bottom_eng-male,
                knurlie_engagement=v("c5","knurlie_usable")-acrylic,
                knurlie_bottom_gap=v("c5","upper_thread_depth")-(v("c5","knurlie_usable")-acrylic),
                upper_spacing=v("c5","upper_post")+v("c5","washers")*v("c5","washer_thickness"),
                carrier_tip_below_skin=v("c5","carrier_screw")-(v("c5","carrier_skin")-cb)),
            closing_strip_screws=[dict(panel_thickness=t,screw_length=l,
                nominal_nut_engagement=l-t-v('c1','closure_depth')+v('screw','nut_height'),
                tip_beyond_nut=l-t-v('c1','closure_depth')) for t,l in zip(v('c1','thicknesses'),v('c1','closure_screw_lengths'))],
            led=dict(rod_length=source-v("c6","top_gap")-(ledh+v("c6","led_air")),rod_to_led_gap=v("c6","led_air"),
                rod_to_acrylic_gap=v("c6","top_gap"),
                plp_shaft_bottom=v("c6","plp_seat_z")-v("plp2","body_length"),
                plp_head_top=v("c6","plp_seat_z")+v("plp2","head_height"),
                plp_to_led_gap=v("c6","plp_seat_z")-v("plp2","body_length")-ledh,
                plp_to_acrylic_gap=source-v("c6","plp_seat_z")-v("plp2","head_height"),
                rod_web_worst= v("board","led_offset")-v("c6","pocket_width")/2-max(v("c6","rod_bores"))/2,
                plp_web=v("board","led_offset")-v("c6","pocket_width")/2-v("c6","plp_counterbore")/2,
                open_web=v("board","led_offset")-v("c6","pocket_width")/2-v("c6","open_hole")/2,
                rod_blank_length_for_16_plus_spares=(16+v("c6","rod_spares"))*(source-v("c6","top_gap")-(ledh+v("c6","led_air"))+v("c6","rod_cut_kerf")),
                point_model_bare_degrees=math.degrees(math.atan((v("c2","cap_top")-ledh)/(v("board","led_offset")-v("c2","square_cap")/2))),
                point_model_raised_degrees=math.degrees(math.atan((v("c2","cap_top")-source)/(v("board","led_offset")-v("c2","square_cap")/2)))),
            cap=dict(square_max_sleeve=v("c2","square_cap")+2*max(v("c2","gaps"))+2*v("c2","sleeve_wall"),
                round_max_sleeve=v("c2","round_cap")+2*max(v("c2","gaps"))+2*v("c2","sleeve_wall"),
                flange_to_lower_plate_upward_gap=v("c2","carrier_top")-v("c2","flange_bottom")-v("c2","flange_thickness"),
                flange_downward_clearance=v("c2","flange_travel"),
                common_guide_overlap_per_side=(v("c2","flange")-v("c2","plate_hole_square"))/2))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--parameters",type=Path,default=ROOT/"v4-parameters.json")
    args=p.parse_args(); check=Check(args.parameters); check.run()
    orientation=evaluate_v4(ROOT)
    calc=check.calculations()
    unintended=[x for x in check.results if x['required_clear_in_this_coupon'] and not x['nominal_clear']]
    trials=[x for x in check.results if not x['required_clear_in_this_coupon'] and not x['nominal_clear']]
    ledger=[]
    def inspect_entries(value,path=""):
        if isinstance(value,dict):
            if "value" in value:
                ledger.append(dict(path=path,valid=value.get("status") in ("NOMINAL","GEMESSEN") and bool(value.get("source")) and bool(value.get("unit"))))
            else:
                for k,item in value.items(): inspect_entries(item,path+"/"+k)
    inspect_entries(check.p)
    plp_checks=dict(grip_panel_in_drawing_range=check.v("plp2","panel_min")<=check.v("c6","plp_grip_thickness")<=check.v("plp2","panel_max"),
        bore_in_drawing_range=check.v("plp2","bore_min")<=check.v("plp2","bore")<=check.v("plp2","bore_max"),
        web_target_met=calc['led']['plp_web']+1e-9>=check.v("c6","web_target"),
        led_clear_with_general_length_tolerance=calc['led']['plp_to_led_gap']>check.v("plp2","general_tolerance"),
        acrylic_clear_with_head_tolerance=calc['led']['plp_to_acrylic_gap']>check.v("plp2","general_tolerance"))
    contact_stops=all(not c['nominal_clear'] for c in check.results if c.get('required_contact'))
    passed=all(x['passed'] for x in check.parts.values()) and orientation['passed'] and not unintended and all(x['valid'] for x in ledger) and all(plp_checks.values()) and contact_stops
    report=dict(date="2026-10-25",hardware="NICHT GEMESSEN",all_dimensions="NOMINAL unless explicitly marked GEMESSEN in parameter ledger",
        parameters_sha256=hashlib.sha256(args.parameters.read_bytes()).hexdigest(),
        validator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        orientation_checker_sha256=hashlib.sha256((ROOT.parent/'check_stl_orientation.py').read_bytes()).hexdigest(),
        runtime=dict(python=sys.version.split()[0],numpy=np.__version__,trimesh=trimesh.__version__),
        passed=passed,meaning_of_pass="Mesh integrity, handedness and required digital clearances ONLY. Access/grip trials below can fail; no hardware or production approval.",
        parts=check.parts,orientation=orientation,parameter_entries=ledger,plp_drawing_checks=plp_checks,positive_flange_stops_passed=contact_stops,collision_tolerance_mm3=check.tol,
        probe_files=check.probe_records,
        collisions=check.results,unintended_collisions=unintended,trial_findings=trials,calculations=calc,
        limitations=["No solder tails, all SMDs, molded-cap internals or cable bending envelopes modeled.",
            "C4 references are NOT actual HYCW shell/shoulder proof; measure before fitting.",
            "Finger sphere sampled at discrete angles; no biomechanics or grip/friction model.",
            "PLP2 ribs modeled only as a conservative REF cylinder, not exact molded rib profile.",
            "Printed dummies are not clear acrylic; optical assessment requires clear plates.",
            "No mechanical strength, torque, press-fit force or cap extraction force measured."])
    path=ROOT/"coupons/validation.json"
    path.write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
    print(json.dumps(dict(passed=passed,meshes=len(check.parts),collision_checks=len(check.results),unintended_collisions=unintended,trial_findings=len(trials)),indent=2))
    if not passed: raise SystemExit(1)


if __name__=="__main__": main()
