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


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=ROOT)
    p.add_argument('--output',type=Path,default=ROOT/'orientation-audit/after-correction.json')
    p.add_argument('--record-mirrored-baseline',action='store_true')
    args=p.parse_args(); result=evaluate(args.root)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps(dict(passed=result['passed'],parts={n:r['passed'] for n,r in result['parts'].items()}),indent=2))
    if args.record_mirrored_baseline:
        assert not result['passed'],'The old baseline unexpectedly passed; check the oracle'
    else:
        assert result['passed'],'Export orientation failed: inspect the per-landmark evidence'


if __name__=='__main__': main()
