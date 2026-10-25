"""Build Stage 1 coupons, never a complete case. Dimensions come only from JSON.

The primitive/CSG/export approach follows build_case.py. No old generator is
imported: it would introduce a second, implicit source of dimensions.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys

import manifold3d as m
import numpy as np
import trimesh

ROOT = Path(__file__).resolve().parent
P = {}
PARTS = {}


def v(group, name):
    return P[group][name]["value"]


def box(x, y, z, w, d, h):
    return m.Manifold.cube((w, d, h)).translate((x, y, z))


def cyl(x, y, z, diameter, height, segments=None):
    return m.Manifold.cylinder(height, diameter/2,
        circular_segments=segments or int(v("general", "segments"))).translate((x, y, z))


def union(parts):
    return m.Manifold.batch_boolean(list(parts), m.OpType.Add)


def x_cyl(x, y, z, diameter, length):
    return cyl(0, 0, 0, diameter, length).rotate((0, 90, 0)).translate((x, y, z))


def y_cyl(x, y, z, diameter, length):
    return cyl(0, 0, 0, diameter, length).rotate((-90, 0, 0)).translate((x, y, z))


def hole_z(x, y, bottom, top, diameter=None):
    e = v("general", "csg_margin")
    return cyl(x, y, bottom-e, diameter or v("screw", "clearance"), top-bottom+2*e)


def hex_hole(x, y, z, height):
    af = v("screw", "nut_af") + v("screw", "nut_clearance")
    return cyl(0,0,0,2*af/math.sqrt(3),height,6).rotate((0,0,30)).translate((x,y,z))


# A deliberately small engineering label font; these are dimensionless glyphs.
GLYPHS = {
    "0": ["111","101","101","101","111"], "1": ["010","110","010","010","111"],
    "2": ["111","001","111","100","111"], "3": ["111","001","111","001","111"],
    "4": ["101","101","111","001","001"], "5": ["111","100","111","001","111"],
    "6": ["111","100","111","101","111"], "7": ["111","001","001","001","001"],
    "8": ["111","101","111","101","111"], "9": ["111","101","111","001","111"],
    "A": ["010","101","111","101","101"], "B": ["110","101","110","101","110"],
    "C": ["111","100","100","100","111"], "D": ["110","101","101","101","110"],
    "F": ["111","100","110","100","100"], "G": ["111","100","101","101","111"],
    "J": ["001","001","001","101","111"], "L": ["100","100","100","100","111"],
    "N": ["101","111","111","111","101"], "O": ["111","101","101","101","111"],
    "P": ["110","101","110","100","100"], "R": ["110","101","110","101","101"],
    "S": ["111","100","111","001","111"], "U": ["101","101","101","101","111"],
    "X": ["101","101","010","101","101"], "-": ["000","000","111","000","000"],
}


def labeled(solid, label, anchor):
    """Add a connected identification ear, outside the working fixture.

    A three-dot chiral witness is engraved on the ear. Its XY pattern has no
    reflection symmetry; check_stl_orientation tests void AND adjacent material.
    """
    x, y, z = anchor
    w, d, h = (v("general", k) for k in
               ("label_pad_width", "label_pad_depth", "label_pad_thickness"))
    # Thin acrylic dummies must not gain a taller ear that touches the next
    # layer. The label remains within that part's own thickness.
    bounds=solid.bounding_box()
    h=min(h,bounds[5]-bounds[2])
    ear = box(x, y, z, w, d, h)
    depth = v("general", "label_depth")
    e = v("general", "csg_margin")
    stroke = v("general", "label_stroke")
    cell = v("general", "label_height")/5
    cuts = []
    for i, char in enumerate(label):
        for row, line in enumerate(GLYPHS[char]):
            for col, bit in enumerate(line):
                if bit == "1":
                    cuts.append(box(x+stroke+i*4*cell+col*cell,
                        y+stroke+row*cell, z+h-depth, cell-e, cell-e, depth+e))
    pitch = v("general", "marker_pitch")
    r = v("general", "marker_radius")
    mx, my = x+w-3*pitch, y+d/2
    marks = [(mx, my), (mx+pitch, my), (mx, my+pitch)]
    cuts += [cyl(a, b, z+h-depth, 2*r, depth+e) for a,b in marks]
    ear -= union(cuts)
    # Record positive and negative witnesses in source coordinates. The external
    # checker also has literal PCB-key/LED witnesses, independent of this ear.
    witness = dict(void=[mx, my+pitch, z+h-depth/2],
                   solid=[mx+pitch/2, my+pitch, z+h-depth/2],
                   additional=[dict(void=[a,b,z+h-depth/2],solid=[a+pitch/2,b,z+h-depth/2]) for a,b in marks[:2]])
    return solid+ear, witness


def save(name, solid, label, anchor, purpose, print_orientation="largest flat underside on bed", supports="none", **extra):
    if extra.get("role")=="sleeve":
        # Small sleeves must not have ears that obstruct adjacent keys. One,
        # two or three shallow dimples encode the clearance station instead.
        x,y,z=anchor
        z=v("c2","flange_bottom")+v("c2","flange_thickness")
        rad=v("c2","flange")/2-v("general","marker_radius")*.75
        count=extra["station"]+1
        points=[]
        for i in range(count):
            a,b=x-rad,y+v("general","marker_radius")*(.75+i*1.25)
            points.append((a,b))
            solid-=cyl(a,b,z-v("general","label_depth"),v("general","marker_radius"),v("general","label_depth")+v("general","csg_margin"))
        a,b=points[0]
        marker=dict(void=[a,b,z-v("general","label_depth")/2],
                    solid=[x+rad,y,z-v("general","label_depth")/2])
    else:
        solid, marker = labeled(solid, label, anchor)
    assert solid.status() == m.Error.NoError, (name, solid.status())
    solid=solid.simplify(v("general","mesh_simplify_tolerance"))
    mesh = solid.to_mesh()
    mesh = trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3],
                           faces=np.asarray(mesh.tri_verts), process=True)
    # One and only one reflection; trimesh reverses winding for det < 0.
    mesh.apply_transform(np.diag([1., -1., 1., 1.]))
    installed_z = float(mesh.bounds[0, 2])
    mesh.apply_translation((0, 0, -installed_z))
    # STL uses float32. Weld the quantized mesh and discard collapsed slivers
    # BEFORE writing, rather than shipping a manifold CSG with broken STL edges.
    mesh=trimesh.Trimesh(vertices=mesh.vertices.astype(np.float32),
                         faces=mesh.faces,process=True)
    f=mesh.faces
    mesh.update_faces((f[:,0]!=f[:,1]) & (f[:,1]!=f[:,2]) & (f[:,0]!=f[:,2]))
    mesh.remove_unreferenced_vertices()
    path = ROOT / "coupons" / (name + ".stl")
    path.parent.mkdir(parents=True, exist_ok=True)
    mesh.export(path)
    PARTS[name] = dict(file=path.name, id=label, purpose=purpose,
        installed_z_offset=installed_z, print_orientation=print_orientation,
        supports=supports, ear_source_xyz=anchor, marker_source=marker, **extra)


def c1():
    p = "c1"
    xmin, xmax, bot, top, split = (v(p,k) for k in ("x_min","x_max","bottom","top","split_z"))
    step = v(p,"plane_step_x")
    planes = v(p,"planes_y")
    eps = v("general","csg_margin")
    for t in v(p,"thicknesses"):
        tname = str(int(t))
        slabs = [box(xmin,planes[0]-t,bot,step-xmin,t,top-bot),
                 box(step,planes[1]-t,bot,xmax-step,t,top-bot)]
        # The 0.035-mm step is intentional: the mounting planes differ.
        panel = union(slabs)
        holes = [y_cyl(x, y-t-eps, v(p,"axis_z"), v(p,"hole"), t+2*eps)
                 for x,y in zip(v(p,"axes_x"),planes)]
        closed = panel-union(holes)
        lowerbox = box(xmin-eps, min(planes)-t-eps, bot-eps,
                       xmax-xmin+2*eps, t+v(p,"closure_depth")+2*eps, split-bot+eps)
        upper = closed-lowerbox
        lower = closed ^ lowerbox
        for x in v(p,"screw_x"):
            y = planes[0] if x < step else planes[1]
            depth=v(p,"closure_depth"); fit=v(p,"closure_fit_gap")
            pad = box(x-depth/2,y-eps,split,depth,depth+eps,top-split)
            upper += pad
            # A forward finger belongs to the lower strip. It slides into a
            # rear recess in the upper part. Fasteners approach horizontally
            # from the audio side, never through the PCB from below.
            finger=box(x-depth/2,y-eps,bot,depth,depth/2+eps,split+depth-bot)
            lower += finger
            upper -= box(x-depth/2-fit/2,y-eps,split,depth+fit,depth/2+eps+fit,depth+fit)
            screw_z=split+depth/2
            drill=y_cyl(x,y-t-eps,screw_z,v("screw","clearance"),t+depth+2*eps)
            upper -= drill
            lower -= drill
            nut_h=v("screw","nut_height"); af=v("screw","nut_af")+v("screw","nut_clearance")
            nut=cyl(0,0,0,2*af/math.sqrt(3),nut_h+eps,6).rotate((0,0,30)).rotate((-90,0,0)).translate((x,y+depth-nut_h,screw_z))
            upper-=nut
        # Clearance only on the mating seam, not a slot through the bore.
        lower = lower.translate((0,0,-v(p,"closure_gap")))
        save(f"C1-A{tname}-upper",upper,f"C1-A{tname}U",[xmin-v("general","label_pad_width")+eps,min(planes)-t,top-v("general","label_pad_thickness")],
             "split Neutrik panel; upper half", "rear panel face on bed (proper 90-degree rotation)",
             "support nut roofs only; remove completely", family="C1", variant="A", thickness=t)
        save(f"C1-A{tname}-strip",lower,f"C1-A{tname}L",[xmin-v("general","label_pad_width")+eps,min(planes)-t,bot-v(p,"closure_gap")],
             "closing strip; two horizontal front screws (A2 M3x8 / A3 M3x10) into rear nuts", "rear panel face on bed (proper 90-degree rotation)",
             family="C1",variant="A",thickness=t)
        u = closed
        for x,y in zip(v(p,"axes_x"),planes):
            u -= box(x-v(p,"hole")/2,y-t-eps,bot-eps,
                     v(p,"hole"),t+2*eps,v(p,"axis_z")-bot+eps)
        save(f"C1-B{tname}",u,f"C1-B{tname}",[xmin-v("general","label_pad_width")+eps,min(planes)-t,top-v("general","label_pad_thickness")],
             "open-bottom U panel, NOT a load-approved design", "rear panel face on bed (proper 90-degree rotation)",
             family="C1",variant="B",thickness=t)


def c2_outline(z,h):
    x = v("board","key_x")-v("c2","plate_margin_x")
    w = (v("c2","count")-1)*v("board","pitch")+2*v("c2","plate_margin_x")
    # Screw ears are included in the rectangular stock, outside key footprints.
    return box(x-v("c2","plate_margin_x"),v("c2","plate_y_min"),z,
               w+2*v("c2","plate_margin_x"),v("c2","plate_y_max")-v("c2","plate_y_min"),h)


def keyhole(x,y,z,h,diam,shape):
    return cyl(x,y,z,diam,h) if shape == "round" else box(x-diam/2,y-diam/2,z,diam,diam,h)


def c2():
    p="c2"
    e=v("general","csg_margin")
    xs=[v("board","key_x")+i*v("board","pitch") for i in range(int(v(p,"count")))]
    y=v("board","key_y"); led_y=y-v("board","led_offset")
    for shape, tag in [("square","S"),("round","R")]:
        base=c2_outline(v(p,"carrier_bottom"),v(p,"carrier_top")-v(p,"carrier_bottom"))
        for x in xs:
            base -= keyhole(x,y,v(p,"carrier_bottom")-e,
                v(p,"carrier_top")-v(p,"carrier_bottom")+2*e,v(p,"plate_hole_square"),"square")
            base -= keyhole(x,y,v(p,"flange_bottom")-v(p,"flange_travel"),
                v(p,"carrier_top")-v(p,"flange_bottom")+v(p,"flange_travel")+e,
                v(p,"flange")+2*v(p,"pocket_gap"),shape)
            base -= hole_z(x,led_y,v(p,"carrier_bottom"),v(p,"carrier_top"),v(p,"led_tunnel"))
        for x in v(p,"fixture_screw_x"):
            base-=hole_z(x,v(p,"fixture_screw_y"),v(p,"carrier_bottom"),v(p,"carrier_top"))
        save(f"C2-{tag}-carrier",base,f"C2-{tag}",[xs[0]-2*v(p,"plate_margin_x"),v(p,"plate_y_min")-v("general","label_pad_depth")+e,v(p,"carrier_bottom")],
             "three sleeve stations: per-side radial clearances 0.1 / 0.2 / 0.3",family="C2",shape=shape,role="carrier")
        # S: common square guide usable with both sleeve shapes. R: separate round guide.
        for role,z,h in [("lower",v(p,"carrier_top"),v(p,"lower_plate_thickness")),
                         ("upper",v(p,"carrier_top")+v(p,"lower_plate_thickness")+v(p,"paper_thickness"),v(p,"upper_plate_thickness"))]:
            plate=c2_outline(z,h)
            for x in xs:
                size=v(p,"plate_hole_"+shape) if role=="lower" else v(p,"cap_paper_"+shape)
                plate-=keyhole(x,y,z-e,h+2*e,size,shape)
                # Real acrylic is clear and has NO LED holes. Only upper/paper
                # reference is perforated here for registration tests in opaque FDM.
                if role=="upper": plate-=hole_z(x,led_y,z,z+h,v(p,"led_hole"))
            for x in v(p,"fixture_screw_x"):
                plate-=hole_z(x,v(p,"fixture_screw_y"),z,z+h)
            save(f"C2-{tag}-{role}",plate,f"C2-{tag}{'L' if role=='lower' else 'U'}",[xs[0]-2*v(p,"plate_margin_x"),v(p,"plate_y_min")-v("general","label_pad_depth")+e,z],
                "opaque FDM plate DUMMY, not an optical acrylic substitute",family="C2",shape=shape,role=role)
        for i,(x,gap) in enumerate(zip(xs,v(p,"gaps"))):
            cap=v(p,"square_cap" if shape=="square" else "round_cap")
            inner=cap+2*gap; outer=inner+2*v(p,"sleeve_wall")
            bottom=v(p,"sleeve_bottom"); top=v(p,"sleeve_top")
            sleeve=keyhole(x,y,bottom,top-bottom,outer,shape)
            sleeve+=keyhole(x,y,v(p,"flange_bottom"),v(p,"flange_thickness"),v(p,"flange"),shape)
            # Open bottom: a lip under the cap would intrude into the switch
            # housing at the assumed 13-mm cap height. No artificial cap seat.
            sleeve-=keyhole(x,y,bottom-e,top-bottom+2*e,inner,shape)
            # Two compliant fingers with trial inward gripping teeth. Stock
            # square caps have no verified undercut: retention remains a test.
            for sign in [-1,1]:
                xx=x+sign*(outer/2-v(p,"sleeve_wall")/2)
                for side in [-1,1]:
                    sleeve-=box(xx-v(p,"sleeve_wall"),y+side*v(p,"latch_width")/2-v(p,"latch_slot")/2,
                        bottom+v(p,"latch_anchor_height"),2*v(p,"sleeve_wall"),v(p,"latch_slot"),v(p,"latch_height"))
                tooth=box(x+sign*(cap/2-v(p,"latch_tooth"))- (v(p,"sleeve_wall")+gap if sign<0 else 0),
                    y-v(p,"latch_width")/2,bottom+v(p,"latch_height")+e,
                    v(p,"sleeve_wall")+gap+v(p,"latch_tooth"),v(p,"latch_width"),v(p,"latch_anchor_height"))
                sleeve+=tooth
            save(f"C2-{tag}-sleeve-{i+1}",sleeve,f"C2-{tag}{i+1}",[x,y,bottom],
                "trial cap grip; identify by 1=.1, 2=.2, 3=.3 mm PER SIDE",supports="local supports for flange; keep inside and fingers clear",family="C2",shape=shape,role="sleeve",station=i,gap_per_side=gap,
                id_on_mesh="1 / 2 / 3 dimples on flange; shape identifies S or R")


def c3():
    p="c3"; e=v("general","csg_margin")
    for face in v(p,"faces_x"):
        xmin=v(p,"x_min"); ymin=v(p,"roof_y_min"); ymax=v(p,"roof_y_max")
        wall=v(p,"wall"); roof=v(p,"roof_top")
        bot=-(v("c5","floor")+v("c5","lower_post")+v("board","thickness"))
        part=box(xmin,ymin,bot,face-xmin,ymax-ymin,v("c5","floor"))
        part+=box(face-wall,ymin,bot,wall,ymax-ymin,roof-bot)
        part+=box(v(p,"roof_x_min"),ymin,v(p,"window_top"),face-v(p,"roof_x_min"),ymax-ymin,roof-v(p,"window_top"))
        part+=box(xmin,v(p,"front_y")-wall,bot,face-xmin,wall,roof-bot)
        # Closed top, a tapered entry only in Y; never secretly raise the roof.
        lo,hi=v(p,"niche_y")
        opening=box(v("board","width")+e,lo,v(p,"window_bottom"),face-v("board","width"),hi-lo,v(p,"window_top")-v(p,"window_bottom"))
        opening+=box(face-wall-e,lo,0,wall+2*e,hi-lo,v(p,"window_top"))
        # Flared side entry: widen by .6 mm at the outside, preserving Z limits.
        outline=m.CrossSection([[(face-wall,lo),(face+e,lo-v(p,"chamfer")),
                                 (face+e,hi+v(p,"chamfer")),(face-wall,hi)]])
        opening+=m.Manifold.extrude(outline,v(p,"window_top")-v(p,"window_bottom")).translate((0,0,v(p,"window_bottom")))
        # Never allow the below-PCB part of the niche into the PCB outline.
        opening-=box(xmin-e,ymin-e,bot-e,v("board","width")-xmin+e,ymax-ymin+2*e,-bot+e)
        part-=opening
        hx,hy,hz=v(p,"headphone_axis")
        part-=y_cyl(hx,hy-e,hz,v(p,"headphone_hole"),ymax-hy+2*e)
        # Board-edge passage is distinct from the fingertip niche. Remove the
        # PCB envelope before adding the H4 seating boss.
        pcb_bottom=-v("board","thickness")
        part-=box(xmin-e,ymin-e,pcb_bottom-v(p,"pcb_clearance"),
            v("board","width")-xmin+v(p,"pcb_clearance")+e,
            v("board","depth")-ymin+v(p,"pcb_clearance")+e,
            v("board","thickness")+2*v(p,"pcb_clearance"))
        h4x,h4y=v("board","h4_xy")
        pcb_bottom=-v("board","thickness")
        part+=cyl(h4x,h4y,bot,v(p,"dome_radius")*2,pcb_bottom-v(p,"pcb_clearance")-bot)
        part-=hole_z(h4x,h4y,bot,bot+v("c5","floor"))
        part-=hole_z(h4x,h4y,bot+v("c5","floor"),roof,v(p,"h4_post_clearance"))
        part-=cyl(h4x,h4y,bot-e,v("screw","counterbore_diameter"),v("screw","counterbore_depth")+e)
        # A PCB slot in the front wall. Jack/collar opening stays independent.
        part-=box(xmin-e,v(p,"front_y")-wall-e,pcb_bottom-v(p,"pcb_clearance"),
                  v("board","width")-xmin+e,wall+2*e,v("board","thickness")+2*v(p,"pcb_clearance"))
        save(f"C3-X{int(face)}",part,f"C3-X{int(face)}",[xmin,ymin-v("general","label_pad_depth")+e,bot],
            "closed-roof right corner; H4 + J5 front + wheel niche; J10 plug gauge",supports="roof and headphone arch: removable supports, no support trapped at wheel",family="C3",face_x=face)


def c4():
    p="c4"; e=v("general","csg_margin")
    for tag,key in [("J7","j7"),("S","seed")]:
        cx,cy,cz=v(p,key+"_center"); sx,sy,sz=v(p,key+"_shell")
        if key=="seed": cz+=v(p,"seed_pcb_top")
        x0=cx-sx/2; x1=cx+sx/2; z0=cz-sz/2
        rail=v(p,"rail"); gap=v(p,"shell_gap")
        bottom=max(v(p,"base_gap"),z0-v(p,"base_gap")-v(p,"base_height"))
        if key=="seed": bottom=max(bottom,v(p,"seed_pcb_top")+v(p,"base_gap"))
        spread=max(sy,v(p,"plug_width"))+2*gap
        # Above-PCB fork, with no floor under the connector or plug. A rear
        # crossbar joins the side rails; the detachable bridge closes the top.
        a=box(x1+gap,cy-spread/2-rail,bottom,rail,spread+2*rail,cz+sz/2+gap-bottom)
        for sign in [-1,1]:
            yy=cy+spread/2 if sign>0 else cy-spread/2-rail
            a+=box(x0,yy,bottom,sx+rail+gap,rail,cz+sz/2+gap-bottom)
        # A back stop contacts two shell side/rear corners, not PCB solder pins.
        for sign in [-1,1]:
            yy=cy+sign*(sy/2-v(p,"rear_stop_width")/2)-v(p,"rear_stop_width")/2
            a+=box(x1+gap,yy,max(bottom,z0),rail,v(p,"rear_stop_width"),cz+sz/2-max(bottom,z0))
        # Front-facing stop lands behind the mating face, beside the shield.
        # Its shoulder datum is explicitly unverified, NOT a GCT/HYCW equivalence.
        for sign in [-1,1]:
            yy=cy+sy/2+gap if sign>0 else cy-spread/2-e
            a+=box(x0+v(p,"shoulder_x_offset"),yy,max(bottom,z0),
                   v(p,"stop_thickness"),(spread-sy)/2+e,cz+sz/2-max(bottom,z0))
        boss_offset=spread/2+v(p,"boss_diameter")/2
        zz=cz+sz/2+gap
        for sign in [-1,1]:
            yy=cy+sign*boss_offset
            a+=cyl(cx,yy,bottom,v(p,"boss_diameter"),zz-bottom)
            a-=hole_z(cx,yy,bottom,zz,v(p,"pilot"))
        save(f"C4-{tag}-cradle",a,f"C4-{tag}",[x0,cy+spread/2+rail-e,bottom],
            "front/back shell-stop trial; use ONLY a dummy until shell shoulders measured",supports="front hooks if required; do not fill socket",family="C4",connector=key,role="cradle")
        # A removable bridge makes Seed removal possible; no permanent top cage.
        cap=box(x0,cy-spread/2-rail,zz,sx,spread+2*rail,rail)
        for sign in [-1,1]:
            yy=cy+sign*boss_offset
            cap+=cyl(cx,yy,zz,v(p,"boss_diameter"),rail)
            cap-=hole_z(cx,yy,zz,zz+rail)
        save(f"C4-{tag}-bridge",cap,f"C4-{tag}B",[x0,cy+spread/2+rail-e,zz],
            "removable bridge; M3 clamps at rails only, never Seed PCB",print_orientation="top face on bed (proper 180-degree rotation)",family="C4",connector=key,role="bridge")


def c5():
    p="c5"; e=v("general","csg_margin")
    floor=v(p,"floor"); x,y=v(p,"mount_xy"); dx,dy=v(p,"carrier_dome_xy")
    w,d=v(p,"footprint")
    pcb_top=floor+v(p,"lower_post")+v("board","thickness")
    # Express C5 in the same PCB-relative Z frame as every other coupon.
    z=-pcb_top
    floorpart=box(dx-v(p,"carrier_dome_radius"),-w/2,z,w,d,floor)
    dome_top=v(p,"plate_above_pcb")-v(p,"carrier_skin")
    dome_bot=z+floor
    floorpart+=cyl(dx,dy,dome_bot,2*v(p,"carrier_dome_radius"),dome_top-dome_bot)
    floorpart-=hole_z(x,y,z,z+floor)
    floorpart-=cyl(x,y,z-e,v("screw","counterbore_diameter"),v("screw","counterbore_depth")+e)
    insert_h=v("screw","insert_length")+v("screw","insert_bottom_allowance")
    floorpart-=cyl(dx,dy,dome_top-insert_h,v("screw","insert_pilot"),insert_h+e)
    # Clearance for the complete M3x12 tip projection plus bottom margin.
    projection=v(p,"carrier_screw")-(v(p,"carrier_skin")-v("screw","counterbore_depth"))
    floorpart-=cyl(dx,dy,dome_top-projection-v("screw","insert_tip_clearance"),
        v("screw","clearance"),projection+v("screw","insert_tip_clearance")+e)
    save("C5-floor",floorpart,"C5-F",[dx-v(p,"carrier_dome_radius"),-w/2-v("general","label_pad_depth")+e,z],
         "corner floor + off-PCB insert dome; real metal standoffs required",supports="insert/pilot axis vertical; none",family="C5",role="floor")
    px,py=v(p,"dummy_pcb_xy"); pw,pd=v(p,"dummy_pcb_size")
    pcb=box(px,py,-v("board","thickness"),pw,pd,v("board","thickness"))-hole_z(x,y,-v("board","thickness"),0,v("board","mount_hole"))
    save("C5-PCB-dummy",pcb,"C5-PCB",[px,py-v("general","label_pad_depth")+e,-v("board","thickness")],
         "PCB thickness dummy, NEVER put printed plastic between real post shoulders",family="C5",role="pcb")
    ax,ay=v(p,"plate_xy"); aw,ad=v(p,"plate_size")
    carrier=box(ax,ay,dome_top,aw,ad,v(p,"carrier_skin"))
    # Keep the metal PCB post stack free; this carrier does not clamp it.
    carrier-=hole_z(x,y,dome_top,v(p,"plate_above_pcb"),2*v(p,"carrier_dome_radius"))
    carrier-=hole_z(dx,dy,dome_top,v(p,"plate_above_pcb"))
    carrier-=cyl(dx,dy,v(p,"plate_above_pcb")-v("screw","counterbore_depth"),v("screw","counterbore_diameter"),v("screw","counterbore_depth")+e)
    save("C5-carrier",carrier,"C5-C",[ax,ay-v("general","label_pad_depth")+e,dome_top],
         "local skin 3.6, outside-PCB M3x12 counterbore",family="C5",role="carrier")
    zz=v(p,"plate_above_pcb")
    for role,thick in [("lower",v(p,"lower_acrylic")),("upper",v(p,"upper_acrylic"))]:
        if role=="upper": zz+=v(p,"lower_acrylic")+v(p,"paper")
        plate=box(ax,ay,zz,aw,ad,thick)-hole_z(x,y,zz,zz+thick)
        save("C5-"+role,plate,"C5-"+("L" if role=="lower" else "U"),[ax,ay-v("general","label_pad_depth")+e,zz],
             "plate DUMMY; use actual 0.15 paper between, no printed paper shim",family="C5",role=role)


def c6():
    p="c6"; e=v("general","csg_margin")
    xs=[v("board","key_x")+i*v("board","pitch") for i in range(int(v(p,"pairs")))]
    ky=v("board","key_y"); ly=ky-v("board","led_offset")
    x0=xs[0]-v(p,"plate_margin_x"); w=xs[-1]-xs[0]+2*v(p,"plate_margin_x")
    y0=v(p,"plate_y_min"); d=v(p,"plate_y_max")-y0
    lower=v(p,"lower_plate_z"); bottom=v(p,"base_bottom")
    led_top=v("board","led_size")[2]
    shoulder=led_top+v(p,"led_air")
    rod_top=lower-v(p,"top_gap")
    screw_x=[x0+v(p,"fixture_screw_inset"),x0+w-v(p,"fixture_screw_inset")]
    variants=[("O",v(p,"open_hole"))]+[(f"A{i+1}",bore) for i,bore in enumerate(v(p,"rod_bores"))]+[("B",v("plp2","bore"))]
    for tag,bore in variants:
        a=box(x0,y0,bottom,w,d,lower-bottom)
        for x in xs:
            a-=keyhole(x,ky,bottom-e,lower-bottom+2*e,v(p,"key_hole"),"square")
            # Explicit flange keep-out, even though this coupon uses dummy caps.
            a-=keyhole(x,ky,v(p,"pocket_floor"),v(p,"pocket_top")-v(p,"pocket_floor")+e,v(p,"pocket_width"),"square")
            if tag=="O":
                tunnel_bottom=shoulder-v(p,"shoulder_thickness")
                a+=cyl(x,ly,tunnel_bottom,v(p,"tunnel_outer"),bottom-tunnel_bottom+e)
                a-=hole_z(x,ly,tunnel_bottom,lower,bore)
            else:
                stop=shoulder if tag.startswith("A") else v(p,"plp_bottom_stop_z")
                zlow=stop-v(p,"shoulder_thickness")
                a+=cyl(x,ly,zlow,v(p,"tunnel_outer"),bottom-zlow+e)
                a-=hole_z(x,ly,zlow,stop,v(p,"shoulder_hole"))
                if tag.startswith("A"):
                    a-=hole_z(x,ly,stop,lower,bore)
                else:
                    grip_top=v(p,"plp_seat_z")
                    grip_bot=grip_top-v(p,"plp_grip_thickness")
                    a-=hole_z(x,ly,stop,grip_bot,v(p,"rod_bores")[-1])
                    a-=hole_z(x,ly,grip_bot,grip_top,bore)
                    a-=hole_z(x,ly,grip_top,lower,v(p,"plp_counterbore"))
        for x in screw_x: a-=hole_z(x,v(p,"fixture_screw_y"),bottom,lower)
        save("C6-"+tag,a,"C6-"+tag,[x0,y0-v("general","label_pad_depth")+e,bottom],
             "optional LED comparison: O open, A PMMA rod, B clear PLP2-250",print_orientation="flat TOP on bed (proper 180-degree rotation), tubes upward",family="C6",variant=tag,bore=bore,
             rod_length=rod_top-shoulder if tag.startswith("A") else None)
    # Closed lower acrylic dummy is mandatory for the captive-rod test. Real
    # clear acrylic replaces it for any optical assessment.
    for role,z,h in [("lower",lower,v("c2","lower_plate_thickness")),
                     ("upper",lower+v("c2","lower_plate_thickness")+v("c2","paper_thickness"),v("c2","upper_plate_thickness"))]:
        plate=box(x0,y0,z,w,d,h)
        for x in xs: plate-=keyhole(x,ky,z-e,h+2*e,v(p,"key_hole"),"square")
        for x in screw_x: plate-=hole_z(x,v(p,"fixture_screw_y"),z,z+h)
        save("C6-"+role+"-dummy",plate,"C6-"+("L" if role=="lower" else "U"),[x0,y0-v("general","label_pad_depth")+e,z],
             "closed opaque plate DUMMY: use transparent acrylic for light test",family="C6",variant=role)
    # A paper reference is SVG, not a false 0.15-mm FDM spacer.
    svg=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}mm" height="{d}mm" viewBox="{x0} {y0} {w} {d}">',
         f'<path d="M{x0},{y0}h{w}v{d}h{-w}Z" fill="none" stroke="black" stroke-width="0.1"/>']
    for x in xs:
        svg += [f'<rect x="{x-v("c2","cap_paper_square")/2}" y="{ky-v("c2","cap_paper_square")/2}" width="{v("c2","cap_paper_square")}" height="{v("c2","cap_paper_square")}" fill="none" stroke="black" stroke-width="0.1"/>',
                f'<circle cx="{x}" cy="{ly}" r="{v(p,"paper_hole")/2}" fill="none" stroke="black" stroke-width="0.1"/>']
    for x in screw_x:
        svg.append(f'<circle cx="{x}" cy="{v(p,"fixture_screw_y")}" r="{v("screw","clearance")/2}" fill="none" stroke="black" stroke-width="0.1"/>')
    svg.append('</svg>')
    (ROOT/"coupons"/"C6-paper.svg").write_text("\n".join(svg)+"\n",encoding="utf8")


def c7():
    p="c7"; e=v("general","csg_margin")
    for tag,key in [("F","fixed_xy"),("A","adjustable_xy")]:
        x,y=v(p,key); z=v(p,"floor_z"); w=v(p,"base_width")
        base=box(x-w/2,y-w/2,z,w,w,v(p,"floor"))
        # No carbon-filled printed surface is a qualified PCB insulator.
        top=-v("board","thickness")-v(p,"fixed_gap") if tag=="F" else -v("board","thickness")-v("screw","nut_height")-v(p,"nominal_adjustment_gap")
        base+=cyl(x,y,z+v(p,"floor")-e,2*v(p,"radius"),top-z-v(p,"floor")+e)
        if tag=="A":
            base-=hole_z(x,y,z,top)
            base-=hex_hole(x,y,top-v("screw","nut_height"),v("screw","nut_height")+e)
        save("C7-"+tag,base,"C7-"+tag,[x-w/2,y-w/2-v("general","label_pad_depth")+e,z],
             "F fixed non-contact gap / A captive M3 nut + nylon screw; isolated tip",family="C7",variant=tag)


def main():
    global P
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parameters",type=Path,default=ROOT/"v4-parameters.json")
    parser.add_argument("--skip-validation",action="store_true",help="debug only; never use for delivery")
    args=parser.parse_args()
    P=json.loads(args.parameters.read_text(encoding="utf8"))
    for fn in [c1,c2,c3,c4,c5,c6,c7]:
        fn()
    manifest=dict(schema=1,hardware="NICHT GEMESSEN",parameters=os.path.relpath(args.parameters.resolve(),ROOT).replace('\\','/'),
        parameters_sha256=hashlib.sha256(args.parameters.read_bytes()).hexdigest(),
        generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        frame="RH: +X right/MIDI, +Y rear/audio, +Z up. Add installed_z_offset for PCB-top Z=0; no XY reflection on import.",parts=PARTS)
    (ROOT/"coupons"/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf8")
    if not args.skip_validation:
        subprocess.run([sys.executable,str(ROOT.parent/"check_stl_orientation.py"),"--v4-coupons",str(ROOT),"--output",str(ROOT/"coupons"/"orientation.json")],check=True)
        subprocess.run([sys.executable,str(ROOT/"validate_v4_coupons.py"),"--parameters",str(args.parameters)],check=True)
    print(f"Exported {len(PARTS)} individual coupon meshes. Hardware NICHT GEMESSEN.")


if __name__=="__main__": main()
