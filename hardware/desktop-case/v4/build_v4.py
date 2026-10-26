"""Four-piece V4 whole-case trial. All mechanical inputs: v4-parameters.json.

No electronics source is edited. Source PCB X/right Y/player is reflected once
at export into physical RH X/right Y/rear Z/up. STL imports never reflect again.
"""
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

import manifold3d as m
import numpy as np
import trimesh

import build_v4_coupons as c

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'full'
P={}
PARTS={}


def v(group,name): return P[group][name]['value']
def q(name): return v('case',name)
def box(*args): return c.box(*args)
def cyl(*args): return c.cyl(*args)
def hole(*args): return c.hole_z(*args)
def union(parts): return c.union(parts)
def rect(x,y,w,d): return m.CrossSection.square((w,d)).translate((x,y))
def circle(x,y,d): return m.CrossSection.circle(d/2,int(v('general','segments'))).translate((x,y))
def extrude(section,z,h): return m.Manifold.extrude(section,h).translate((0,0,z))


def rounded(x,y,w,d,r):
    return rect(x+r,y+r,w-2*r,d-2*r).offset(r)


def levels():
    pcb=v('c5','floor')+v('c5','lower_post')+v('board','thickness')
    top=v('c5','plate_above_pcb')
    return dict(bottom=-pcb,floor_top=-pcb+v('c5','floor'),
        deck_bottom=top-q('skin'),deck_top=top,
        lower_bottom=top,lower_top=top+v('c5','lower_acrylic'),
        upper_bottom=top+v('c5','lower_acrylic')+v('c5','paper'),
        upper_top=top+v('c5','lower_acrylic')+v('c5','paper')+v('c5','upper_acrylic'))


def outer(): return rounded(*q('outer'),q('corner_radius'))


def hood_sections():
    sections=[rect(*q(k)) for k in ['left_audio_hood','right_audio_hood','midi_hood','seed_hood']]
    # At H3 the MIDI hood is relieved through Knurlie height, so a continuous
    # acrylic tongue can reach the fourth metal post instead of forming an island.
    for x,y in v('full_board','mounts'):
        sections[2]-=circle(x,y,q('knurlie_clearance_diameter'))
    return sections


def rear_slot():
    xmin,xmax=q('rear_plate_x'); t=q('rear_plate_thickness'); gap=q('plate_clearance')
    xstep=q('rear_plate_step_x'); planes=q('audio_planes_y')
    return rect(xmin-gap,planes[0]-t-gap,xstep-xmin+gap,t+2*gap)+rect(
        xstep,planes[1]-t-gap,xmax-xstep+gap,t+2*gap)


def sheet(kind):
    inset=q('plate_edge_inset'); x,y,w,d=q('outer')
    section=rounded(x+inset,y+inset,w-2*inset,d-2*inset,q('corner_radius'))
    for h in hood_sections(): section-=h.offset(q('plate_clearance'))
    section-=rear_slot()
    # The strip between the two audio tunnels, BEHIND the separate vertical
    # rear panel, would be an unsupported acrylic island. Omit it explicitly.
    left=q('left_audio_hood'); right=q('right_audio_hood')
    end=max(q('audio_planes_y'))+q('plate_clearance')
    section-=rect(left[0]+left[2],q('outer')[1],right[0]-left[0]-left[2],end-q('outer')[1])
    for x,y in v('full_board','mounts'): section-=circle(x,y,v('screw','clearance'))
    for x,y in v('full_board','keys'):
        size=q('cap_hole') if kind=='lower' else q('paper_cap_hole')
        section-=rect(x-size/2,y-size/2,size,size)
    for x,y in v('full_board','pots'): section-=circle(x,y,q('pot_hole'))
    for x,y in v('full_board','jacks'):
        section-=circle(x,y,q('jack_hole') if kind=='lower' else q('jack_upper_hole'))
    if kind!='lower':
        for y in sorted({xy[1] for xy in v('full_board','jacks')}):
            centers=sorted(xy[0] for xy in v('full_board','jacks') if xy[1]==y)
            # Join only the close input/gate pair; keep output labels separate.
            x1,x2=centers[-2:]
            section-=rect(x1,y-q('jack_upper_hole')/2,x2-x1,q('jack_upper_hole'))
        tw,td=q('toggle_upper_window')
        xs=[xy[0] for xy in v('full_board','toggles')]; y=v('full_board','toggles')[0][1]
        section-=rounded(min(xs)-tw/2,y-td/2,max(xs)-min(xs)+tw,td,tw/2)
    else:
        for x,y in v('full_board','toggles'): section-=circle(x,y,q('toggle_lower_hole'))
    if kind=='paper':
        for x,y in v('full_board','leds'): section-=circle(x,y,q('paper_led_hole'))
    return section


def port_cuts():
    e=v('general','csg_margin'); x0,y0,w,d=q('outer')
    cuts=[]
    for x,plane in zip(q('audio_axis_x'),q('audio_planes_y')):
        end=plane-q('rear_plate_thickness')
        cuts.append(c.y_cyl(x,y0-e,v('c1','axis_z'),q('audio_port_diameter'),end-y0+e))
    for y in q('midi_axes_y'):
        a=q('midi_port_size')
        cuts.append(box(q('midi_port_start_x'),y-a/2,q('midi_axis_z')-a/2,w,a,a))
    for key,zkey in [('j7','usb_j7_window_z'),('seed','usb_seed_window_z')]:
        _,cy,_=v('c4',key+'_center'); lo,hi=q(zkey)
        cuts.append(box(x0-e,cy-q('usb_window_y_size')/2,lo,-x0+2*e,q('usb_window_y_size'),hi-lo))
    sx,sy,sw,sd=q('sd_window'); lo,hi=q('sd_window_z')
    cuts.append(box(sx,sy,lo,sw,sd,hi-lo))
    hx,hy,hw,hd=q('headphone_window'); lo,hi=q('headphone_window_z')
    cuts.append(box(hx,hy,lo,hw,hd,hi-lo))
    # Wheel is a flat disk. Limit the below-PCB finger cavity to outside X197.
    lo,hi=q('wheel_niche'); zlo,zhi=q('wheel_window_z'); face=x0+w
    cuts.append(box(v('board','width')+e,lo,zlo,face-v('board','width'),hi-lo,zhi-zlo))
    cuts.append(box(face-q('wall')-e,lo,0,q('wall')+2*e,hi-lo,zhi))
    cut=m.CrossSection([[(face-q('wall'),lo),(face+e,lo-q('wheel_chamfer')),
        (face+e,hi+q('wheel_chamfer')),(face-q('wall'),hi)]])
    niche=extrude(cut,zlo,zhi-zlo)
    niche-=box(x0-e,y0-e,zlo-e,v('board','width')-x0+e,d+2*e,-zlo+e)
    cuts.append(niche)
    return union(cuts)


def tray_vents():
    # Intake below the Seed: horizontal slots through the left tray wall,
    # above the PCB plane so air reaches the Seed hood rather than the underside.
    e=v('general','csg_margin'); x0=q('outer')[0]; w=q('vent_slot_width'); lo,hi=q('tray_vent_z')
    return union([box(x0-e,y-w/2,lo,q('wall')+2*e,w,hi-lo) for y in q('tray_vent_y')])


def roof_vents():
    # Exhaust through the Seed hood roof beside the service recess.
    e=v('general','csg_margin'); w=q('vent_slot_width'); lo,hi=q('seed_roof_vent_y')
    return union([box(x-w/2,lo,q('seed_inner_roof')-e,w,hi-lo,q('wall')+2*e) for x in q('seed_roof_vent_x')])


def engrave(solid,position,bottom=False):
    x,y,z=position; depth=v('general','label_depth'); e=v('general','csg_margin')
    pitch=v('general','marker_pitch'); rad=v('general','marker_radius')
    cuts=[]
    # Neutral three-dot witness, no logo. Negative control tests real geometry.
    for dx,dy in [(0,0),(pitch,0),(0,pitch)]:
        cuts.append(cyl(x+dx,y+dy,z-e if bottom else z-depth,2*rad,depth+e))
    solid-=union(cuts)
    zz=z+depth/2 if bottom else z-depth/2
    return solid,dict(void=[x,y+pitch,zz],solid=[x+pitch/2,y+pitch,zz])


def export(name,solid,position,bottom=False,role='print',print_rotation=None):
    if role=='print':
        solid,marker=engrave(solid,position,bottom)
    else:
        mid=position[2]-(v('c5','paper') if name=='paper' else v('c5','lower_acrylic'))/2
        marker=dict(void=[69,85,mid],solid=[73,85,mid])
    solid=solid.simplify(v('general','mesh_simplify_tolerance'))
    raw=solid.to_mesh()
    mesh=trimesh.Trimesh(vertices=np.asarray(raw.vert_properties)[:,:3],faces=np.asarray(raw.tri_verts),process=True)
    mesh.apply_transform(np.diag([1.,-1.,1.,1.]))
    offset=float(mesh.bounds[0,2]); mesh.apply_translation((0,0,-offset))
    mesh=trimesh.Trimesh(vertices=np.asarray(mesh.vertices,dtype=np.float32),faces=mesh.faces,process=True)
    f=mesh.faces; mesh.update_faces((f[:,0]!=f[:,1]) & (f[:,1]!=f[:,2]) & (f[:,0]!=f[:,2]))
    mesh.remove_unreferenced_vertices()
    folder=OUT/('print' if role=='print' else 'reference-NOT-FOR-PRINT')
    folder.mkdir(parents=True,exist_ok=True); path=folder/(name+'.stl'); mesh.export(path)
    PARTS[name]=dict(file=path.relative_to(OUT).as_posix(),role=role,installed_z_offset=offset,
        marker_source=marker,rotation_for_print_degrees=print_rotation or [0,0,0],
        sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def pcb_clearance():
    e=q('pcb_edge_clearance')
    return box(-e,-e,-v('board','thickness')-e,v('board','width')+2*e,v('board','depth')+2*e,v('board','thickness')+2*e)


def tray():
    l=levels(); x,y,w,d=q('outer'); wall=q('wall'); floor=v('c5','floor')
    a=extrude(outer(),l['bottom'],l['deck_bottom']-l['bottom'])
    a-=extrude(rounded(x+wall,y+wall,w-2*wall,d-2*wall,q('corner_radius')),l['floor_top'],l['deck_bottom']-l['floor_top']+v('general','csg_margin'))
    for yy in q('floor_rib_y'):
        a+=box(v('case','corner_radius')+q('wall'),yy,l['floor_top'],v('board','width')-2*(v('case','corner_radius')+q('wall')),q('wall'),q('floor_rib_height'))
    dome_top=v('c5','plate_above_pcb')-v('c5','carrier_skin')
    for xx,yy in q('mounting_domes'):
        a+=cyl(xx,yy,l['floor_top'],q('dome_diameter'),dome_top-l['floor_top'])
        pilot=v('screw','insert_length')+v('screw','insert_bottom_allowance')
        a-=cyl(xx,yy,dome_top-pilot,v('screw','insert_pilot'),pilot+v('general','csg_margin'))
        projection=v('c5','carrier_screw')-(v('c5','carrier_skin')-v('screw','counterbore_depth'))
        a-=cyl(xx,yy,dome_top-projection-v('screw','insert_tip_clearance'),v('screw','clearance'),projection+v('screw','insert_tip_clearance'))
        # The reinforced carrier skin descends below the ordinary tray rim.
        a-=cyl(xx,yy,dome_top,q('dome_diameter')+2*q('plate_clearance'),l['deck_bottom']-dome_top+v('general','csg_margin'))
    for xx,yy in v('full_board','mounts'):
        a-=hole(xx,yy,l['bottom'],l['floor_top'])
        a-=cyl(xx,yy,l['bottom']-v('general','csg_margin'),v('screw','counterbore_diameter'),v('screw','counterbore_depth')+v('general','csg_margin'))
    top=-v('board','thickness')-v('screw','nut_height')-v('c7','nominal_adjustment_gap')
    for xx,yy in q('nylon_supports'):
        a+=cyl(xx,yy,l['floor_top'],2*v('c7','radius'),top-l['floor_top'])
        a-=hole(xx,yy,l['bottom'],top)
        a-=c.hex_hole(xx,yy,top-v('screw','nut_height'),v('screw','nut_height')+v('general','csg_margin'))
    return a-port_cuts()-pcb_clearance()-tray_vents()


def hood(xywh,inner_roof,left_wall=None,right_wall=None):
    l=levels(); x,y,w,d=xywh; wall=q('wall'); e=v('general','csg_margin')
    lw=wall if left_wall is None else left_wall; rw=wall if right_wall is None else right_wall
    a=box(x,y,l['deck_bottom'],w,d,inner_roof+wall-l['deck_bottom'])
    a-=box(x+lw,y+wall,l['deck_bottom']-e,w-lw-rw,d-2*wall,inner_roof-l['deck_bottom']+e)
    return a,rect(x+lw,y+wall,w-lw-rw,d-2*wall)


def carrier():
    l=levels(); e=v('general','csg_margin')
    a=extrude(outer(),l['deck_bottom'],q('skin'))
    hoods=[hood(q('left_audio_hood'),q('audio_inner_roof')),
           hood(q('right_audio_hood'),q('audio_inner_roof'),right_wall=q('audio_right_wall')),
           hood(q('midi_hood'),q('midi_inner_roof'),left_wall=q('midi_left_wall')),
           hood(q('seed_hood'),q('seed_inner_roof'))]
    for h,inside in hoods:
        a-=extrude(inside,l['deck_bottom']-e,q('skin')+2*e)
        a+=h
    # An insert boss is wholly outside the PCB on each perimeter end.
    for x,y in q('mounting_domes'):
        a+=cyl(x,y,l['deck_top']-v('c5','carrier_skin'),q('dome_diameter'),v('c5','carrier_skin'))
        a-=hole(x,y,l['deck_top']-v('c5','carrier_skin'),l['deck_top'])
        a-=cyl(x,y,l['deck_top']-v('screw','counterbore_depth'),v('screw','counterbore_diameter'),v('screw','counterbore_depth')+e)
    for x,y in v('full_board','mounts'):
        a-=hole(x,y,l['deck_bottom']-e,l['upper_top']+q('knurlie_clearance_top'),q('metal_post_clearance_diameter'))
        a-=cyl(x,y,l['deck_top'],q('knurlie_clearance_diameter'),q('knurlie_clearance_top')-l['deck_top'])
    for x,y in v('full_board','keys'):
        s=q('cap_hole'); a-=box(x-s/2,y-s/2,l['deck_bottom']-e,s,s,q('skin')+2*e)
    for x,y in v('full_board','leds'):
        a-=hole(x,y,l['deck_bottom'],l['deck_top'],q('led_hole'))
    for x,y in v('full_board','pots'):
        a-=hole(x,y,l['deck_bottom'],l['deck_top'],q('pot_hole'))
        pw,pd,ptop=q('pot_pocket')
        a-=box(x-pw/2,y-pd/2,l['deck_bottom']-e,pw,pd,ptop-l['deck_bottom']+e)
    for x,y in v('full_board','jacks'):
        w,d=q('jack_body_pocket')
        a-=box(x-w/2,y+q('jack_body_offset_y')-e,l['deck_bottom']-e,w,d,q('skin')+2*e)
    for x,y in v('full_board','toggles'):
        w,d,h=q('toggle_body')
        a-=box(x-w/2-e,y-d/2-e,l['deck_bottom']-e,w+2*e,d+2*e,h+q('underside_component_gap')-l['deck_bottom']+e)
        a-=hole(x,y,l['deck_bottom'],l['deck_top'],q('toggle_lower_hole'))
    # Locally pocket tall STEP-modeled components, leaving a thin non-load skin.
    for component in P['modeled_component_boxes']['value']:
        if component['ref'] in ['J7','MICRO_SD7']: continue
        x,y,z,w,d,h=component['xyzwhd']; gap=q('underside_component_gap')
        if z+h+gap>l['deck_bottom']:
            a-=box(x-gap,y-gap,l['deck_bottom']-e,w+2*gap,d+2*gap,z+h+gap-l['deck_bottom']+e)
    a-=extrude(rear_slot(),l['deck_bottom']-e,q('rear_plate_top')-l['deck_bottom']+2*e)
    # Rear-panel fasteners face horizontally outward and are clear of PCB.
    for x,z in q('rear_plate_mounts'):
        plane=q('audio_planes_y')[0 if x<q('rear_plate_step_x') else 1]
        a+=c.y_cyl(x,plane,z,q('rear_mount_pad_diameter'),q('rear_mount_pad_length'))
        a-=c.y_cyl(x,plane-e,z,v('screw','clearance'),q('rear_mount_pad_length')+2*e)
        af=v('screw','nut_af')+v('screw','nut_clearance'); nh=v('screw','nut_height')
        nut=cyl(0,0,0,2*af/math.sqrt(3),nh+e,6).rotate((0,0,30)).rotate((-90,0,0)).translate((x,plane+q('rear_mount_pad_length')-nh,z))
        a-=nut
    # Top saddles and rear/side stops at USB shell reference faces. Neutral
    # fit is completed with measured insulating shims, never PCB preload.
    for key in ['j7','seed']:
        cx,cy,cz=v('c4',key+'_center'); sx,sy,sz=v('c4',key+'_shell')
        if key=='seed': cz+=v('c4','seed_pcb_top')
        top=cz+sz/2; roof=l['deck_top'] if key=='j7' else q('seed_inner_roof')+q('wall')
        x1,x2=q('usb_top_pad_x')
        a+=box(x1,cy-q('usb_top_pad_width')/2,top+q('usb_gap'),x2-x1,q('usb_top_pad_width'),roof-top-q('usb_gap'))
        bottom=q('usb_lower_stop_z') if key=='j7' else top-q('usb_seed_stop_drop')
        for sign in [-1,1]:
            yy=cy+sign*(sy/2-q('usb_corner_inset'))-q('usb_stop_width')/2
            a+=box(cx+sx/2+q('usb_gap'),yy,bottom,q('usb_stop_thickness'),q('usb_stop_width'),roof-bottom)
            # Sideward-facing front stop beside a hypothetical robust shoulder.
            side_width=max(sy,q('usb_j7_keepout_width')) if key=='j7' else sy
            side_stop=q('usb_j7_side_stop_width') if key=='j7' else q('usb_stop_thickness')
            yy=cy+side_width/2+q('usb_gap') if sign>0 else cy-side_width/2-q('usb_gap')-side_stop
            a+=box(cx-sx/2+v('c4','shoulder_x_offset'),yy,bottom,
                q('usb_stop_thickness'),side_stop,roof-bottom)
    mx,mw,md,mh=q('midi_body'); sx,sy=q('midi_saddle_size')
    for yy in q('midi_axes_y'):
        a+=box(mx+mw/2-sx/2,yy-sy/2,mh+q('usb_gap'),sx,sy,q('midi_inner_roof')+q('wall')-mh-q('usb_gap'))
    # Neutrik top restraint above the actual drawing envelope.
    for x,plane in zip(q('audio_axis_x'),q('audio_planes_y')):
        a+=box(x-q('usb_top_pad_width')/2,plane+q('rear_plate_flange_depth')+q('wall'),
            q('audio_body')[2]+q('usb_gap'),q('usb_top_pad_width'),q('usb_top_pad_width'),
            q('audio_inner_roof')+q('wall')-q('audio_body')[2]-q('usb_gap'))
    # Seed service hatch; roof bosses are above module components, not on PCB.
    sx,sy,sw,sd=q('service_window'); lip=q('service_lip')
    top=q('seed_inner_roof')+q('wall'); cover_bottom=top-q('service_cover_thickness')
    a-=box(sx,sy,q('seed_inner_roof')-e,sw,sd,q('wall')+2*e)
    a-=box(sx-lip,sy-lip,cover_bottom,sw+2*lip,sd+2*lip,q('service_cover_thickness')+e)
    for x,y in q('service_screws'):
        a+=cyl(x,y,q('service_boss_bottom'),q('service_boss_diameter'),cover_bottom-q('service_boss_bottom'))
        a-=cyl(x,y,q('service_boss_bottom')-e,v('screw','insert_pilot'),cover_bottom-q('service_boss_bottom')+2*e)
    return a-port_cuts()-pcb_clearance()-roof_vents()


def rear_plate():
    x0,x1=q('rear_plate_x'); step=q('rear_plate_step_x'); t=q('rear_plate_thickness')
    p0,p1=q('audio_planes_y')[:2]; bot=q('rear_plate_bottom'); top=q('rear_plate_top'); e=v('general','csg_margin')
    a=box(x0,p0-t,bot,step-x0,t,top-bot)+box(step,p1-t,bot,x1-step,t,top-bot)
    # An integral return makes the long 2-mm panel an L-section.
    inset=q('rear_plate_flange_end_inset'); flange_z=q('rear_plate_flange_top')
    a+=box(x0+inset,p0-e,flange_z-q('rear_plate_flange_thickness'),step-x0-inset,q('rear_plate_flange_depth')+e,q('rear_plate_flange_thickness'))
    a+=box(step,p1-e,flange_z-q('rear_plate_flange_thickness'),x1-step-inset,q('rear_plate_flange_depth')+e,q('rear_plate_flange_thickness'))
    # Interrupt the return at the two hood divider walls. The front panel
    # remains continuous and all three flange spans stay attached to it.
    left=q('left_audio_hood'); right=q('right_audio_hood'); gap=q('plate_clearance')
    for lo,hi in [(left[0]+left[2]-q('wall'),left[0]+left[2]),(right[0],right[0]+q('wall'))]:
        a-=box(lo-gap,p1+gap/2,flange_z-q('rear_plate_flange_thickness')-e,
            hi-lo+2*gap,q('rear_plate_flange_depth'),q('rear_plate_flange_thickness')+2*e)
    for x,plane in zip(q('audio_axis_x'),q('audio_planes_y')):
        a-=c.y_cyl(x,plane-t-e,v('c1','axis_z'),v('c1','hole'),t+2*e)
    for x,z in q('rear_plate_mounts'):
        plane=p0 if x<step else p1
        a-=c.y_cyl(x,plane-t-e,z,v('screw','clearance'),t+2*e)
    return a


def service_cover():
    x,y,w,d=q('service_window'); lip=q('service_lip'); gap=q('service_cover_gap')
    top=q('seed_inner_roof')+q('wall'); h=q('service_cover_thickness')
    a=box(x-lip+gap,y-lip+gap,top-h,w+2*lip-2*gap,d+2*lip-2*gap,h)
    for xx,yy in q('service_screws'): a-=hole(xx,yy,top-h,top)
    return a


def svg(section,path):
    x,y,w,d=q('outer')
    paths=[]
    for polygon in section.to_polygons():
        points=np.asarray(polygon)
        paths.append('M'+' L'.join(f'{a:.6f},{b:.6f}' for a,b in points)+' Z')
    text=f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}mm" height="{d}mm" viewBox="{x} {y} {w} {d}">\n'
    text+='<title>V4 1:1 top view, audio rear/top, Seed left, MIDI right; NOMINAL</title>\n'
    text+='\n'.join(f'<path d="{p}" fill="none" stroke="black" stroke-width="0.1"/>' for p in paths)
    path.parent.mkdir(parents=True,exist_ok=True); path.write_text(text+'\n</svg>\n',encoding='utf8',newline='\n')


def main():
    global P
    P=json.loads((ROOT/'v4-parameters.json').read_text(encoding='utf8')); c.P=P
    l=levels()
    export('tray',tray(),[12,12,l['bottom']],bottom=True)
    export('carrier',carrier(),[60,70,l['deck_top']])
    export('rear-panel',rear_plate(),[85,q('audio_planes_y')[1]-q('rear_plate_thickness'),q('rear_plate_top')],print_rotation=[-90,0,0])
    sx,sy,_,_=q('service_window')
    export('seed-service-cover',service_cover(),[sx,sy,q('seed_inner_roof')+q('wall')])
    for name,kind,z,h in [('lower-acrylic','lower',l['lower_bottom'],v('c5','lower_acrylic')),
        ('upper-acrylic','upper',l['upper_bottom'],v('c5','upper_acrylic')),
        ('paper','paper',l['lower_top'],v('c5','paper'))]:
        section=sheet(kind)
        export(name,extrude(section,z,h),[12,12,z+h],role='reference')
        svg(section,OUT/'sheets'/(name+'.svg'))
    data=dict(generated_at_utc=datetime.now(timezone.utc).isoformat(),hardware='NICHT GEMESSEN',
        parameters='../v4-parameters.json',parameters_sha256=hashlib.sha256((ROOT/'v4-parameters.json').read_bytes()).hexdigest(),
        generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        geometry_helper_sha256=hashlib.sha256(Path(c.__file__).read_bytes()).hexdigest(),
        frame='RH: +X right/MIDI, +Y rear/audio, +Z populated side up; add installed_z_offset to each STL for PCB top Z=0',
        intent='Full first print on nominal dimensions, explicitly requested 2026-10-26 instead of coupons; no sleeve capture or light pipes',
        levels_pcb_relative=l,parts=PARTS)
    (OUT/'manifest.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf8',newline='\n')
    print('Four printable parts and three sheet contours exported. Hardware NICHT GEMESSEN.')


if __name__=='__main__': main()
