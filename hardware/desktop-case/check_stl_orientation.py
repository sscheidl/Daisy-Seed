"""Check actual exported STL handedness against independent PCB landmark witnesses.

No generator code or export transformation is imported here. The literal witness
pairs were independently checked against the manufacturer's board and the real
pre-correction meshes. Each opening has a neighbouring solid-material witness,
so an empty point outside a mirrored part cannot accidentally pass.
"""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import trimesh

ROOT=Path(__file__).resolve().parent
# Physical frame: view the populated side from above, audio at the back.
# +X = right (MIDI), +Y = rear, +Z = up. Origin = PCB rear-left.
# Z values below are installed coordinates, except the explicitly local guides.
TRAY=[('rear audio J4',(39.915,7.8,20),(80,7.8,20)),
      ('right DIN J11',(204.8,-20.3,20),(204.8,-108,20)),
      ('left USB J7',(-7.8,-41.09,18),(-7.8,-55,18)),
      ('left Seed USB',(-7.8,-73,20),(-7.8,-55,20))]
V1_TRAY=[('rear audio J4',(39.915,7.8,12),(80,7.8,12)),
         ('right DIN J11',(204.8,-20.3,12),(204.8,-108,12)),
         ('left USB J7',(-7.8,-41.09,12),(-7.8,-20,12))]
CARRIER=[('CV_OUT1',(82,-15.8,22),(89,-15.8,22)),
         ('key S1',(69,-85,21.5),(74.8,-85,21.5)),
         ('LED D1',(69,-78,21.5),(73,-78,21.5)),
         ('Seed RESET provisional',(15.895,-70.655,31.5),(18.395,-70.655,31.5))]


def winding(mesh,point):
    """Solid-angle integral: independent of ray-casting/CSG libraries."""
    relative=mesh.triangles-np.asarray(point,float)
    a,bb,c=relative[:,0],relative[:,1],relative[:,2]
    la,lb,lc=np.linalg.norm(relative,axis=2).T
    numerator=np.einsum('ij,ij->i',a,np.cross(bb,c))
    denominator=(la*lb*lc+np.einsum('ij,ij->i',a,bb)*lc+
        np.einsum('ij,ij->i',bb,c)*la+np.einsum('ij,ij->i',c,a)*lb)
    return float(np.sum(2*np.arctan2(numerator,denominator))/(4*np.pi))


def evaluate(root):
    jobs=[('prototype/tray.stl',0,V1_TRAY),('closed-v2/closed-tray.stl',0,TRAY),
          ('unified-v3/tray.stl',0,TRAY),('unified-v3/unified-carrier.stl',20.4,CARRIER)]
    result=dict(date='2026-10-25',hardware='NICHT GEMESSEN',
        physical_frame='RH: +X right/MIDI, +Y rear/audio, +Z populated side up; origin PCB rear-left',
        oracle='Independent solid-angle witnesses; no generator imports or test-time reflection',
        manufacturer_source='daisyaudio/Seed3-DevKit-Desktop@987297fb7c9a8c58721ef40abf6d6eedd1fcda2a',parts={})
    for name,z,witnesses in jobs:
        path=root/name
        mesh=trimesh.load(path,force='mesh'); mesh.apply_translation((0,0,z))
        checks=[]
        for label,void,solid in witnesses:
            empty=winding(mesh,void); filled=winding(mesh,solid)
            ok=abs(empty)<1e-4 and abs(abs(filled)-1)<1e-4
            checks.append(dict(landmark=label,opening_mm=void,material_mm=solid,
                opening_winding=round(empty,8),material_winding=round(filled,8),passed=ok))
        result['parts'][name]=dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            closed=bool(mesh.is_watertight),outward_normals=bool(mesh.is_winding_consistent and mesh.volume>0),
            bounds_mm=mesh.bounds.round(5).tolist(),checks=checks,
            passed=all(x['passed'] for x in checks) and bool(mesh.is_watertight and mesh.is_winding_consistent and mesh.volume>0))
    result['passed']=all(p['passed'] for p in result['parts'].values())
    return result


def evaluate_v4(root):
    """Check the reimported coupons, without importing any V4 CAD helpers.

    Small parts have orientation witnesses in their identification features.
    Functional witnesses additionally tie C1/C2/C3/C6 to the PCB frame. A
    reflected-in-place negative control must fail for every individual mesh.
    """
    manifest=json.loads((root/'coupons/manifest.json').read_text(encoding='utf8'))
    params=json.loads((root/manifest['parameters']).read_text(encoding='utf8'))
    def val(group,key): return params[group][key]['value']
    result=dict(hardware='NICHT GEMESSEN',frame=manifest['frame'],
        oracle='Actual STL solid-angle witnesses, functional PCB references and Y-reflection negative controls; no CAD generator imports',parts={})
    for name,part in manifest['parts'].items():
        path=root/'coupons'/part['file']
        mesh=trimesh.load(path,force='mesh')
        mesh.apply_translation((0,0,part['installed_z_offset']))
        marker=part['marker_source']
        def physical(p): return (p[0],-p[1],p[2])
        witnesses=[('chiral ID',physical(marker['void']),physical(marker['solid']))]
        for i,pair in enumerate(marker.get('additional',[])):
            witnesses.append((f'chiral ID additional {i+1}',physical(pair['void']),physical(pair['solid'])))
        family=part['family']
        if family=='C1':
            z=6 if name.endswith('strip') else 10
            y=-(3.5-part['thickness']/2)
            witnesses.append(('rear jack J4 / center web',(39.915,y,z),(51,y,z)))
        if family=='C2' and part['role']=='carrier':
            z=val('c2','carrier_top')-.5
            witnesses += [('S1 / between keys',(69,-85,z),(74,-85,z)),
                          ('D1 behind S1',(69,-78,z),(72,-78,z))]
        if family=='C3':
            x=part['face_x']-.25
            witnesses.append(('right wheel window / closed roof',(x,-91.6,2),(x,-91.6,8)))
        if family=='C6' and part['variant'] not in ('lower','upper'):
            z=val('c6','lower_plate_z')-.25
            witnesses += [('S1 / web',(69,-85,z),(74,-85,z)),
                          ('D1 behind S1',(69,-78,z),(72,-78,z))]
        checks=[]
        for label,empty,filled in witnesses:
            a,b=winding(mesh,empty),winding(mesh,filled)
            checks.append(dict(landmark=label,opening_mm=empty,material_mm=filled,
                opening_winding=round(a,7),material_winding=round(b,7),
                passed=bool(abs(a)<1e-4 and abs(b-1)<1e-4)))
        # Mirror about the part's own Y midpoint, rather than merely moving it
        # to the other side of the global origin. This exercises chirality.
        bad=mesh.copy()
        bad.apply_transform(np.array([[1,0,0,0],[0,-1,0,float(mesh.bounds[:,1].sum())],
                                     [0,0,1,0],[0,0,0,1]]))
        negative=[]
        for label,empty,filled in witnesses:
            a,b=winding(bad,empty),winding(bad,filled)
            negative.append(dict(landmark=label,opening_winding=round(a,7),material_winding=round(b,7)))
        rejected=not all(abs(c['opening_winding'])<1e-4 and abs(c['material_winding']-1)<1e-4 for c in negative)
        result['parts'][name]=dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            checks=checks,reflected_negative_control_rejected=bool(rejected),reflected_checks=negative,
            passed=all(c['passed'] for c in checks) and bool(rejected))
    result['passed']=all(p['passed'] for p in result['parts'].values())
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=ROOT)
    p.add_argument('--output',type=Path)
    p.add_argument('--record-mirrored-baseline',action='store_true')
    p.add_argument('--v4-coupons',type=Path,help='V4 folder; check each coupon, not full-case witnesses')
    args=p.parse_args(); result=evaluate_v4(args.v4_coupons) if args.v4_coupons else evaluate(args.root)
    output=args.output or (args.v4_coupons/'coupons/orientation.json' if args.v4_coupons else ROOT/'orientation-audit/after-correction.json')
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps(dict(passed=result['passed'],parts={n:r['passed'] for n,r in result['parts'].items()}),indent=2))
    if args.record_mirrored_baseline:
        assert not result['passed'],'The old baseline unexpectedly passed; check the oracle'
    else:
        assert result['passed'],'Export orientation failed: inspect the per-landmark evidence'


if __name__=='__main__': main()
