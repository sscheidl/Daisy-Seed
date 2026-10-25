"""Reimport actual RH STLs and test independent plug/fingertip access envelopes."""

import argparse
import hashlib
import json
from pathlib import Path

import manifold3d as m
import numpy as np
import trimesh

ROOT=Path(__file__).resolve().parent


def evaluate():
    # Physical RH coordinates, literal functional targets, independent of cut helpers.
    plug=m.Manifold.cylinder(25,5,circular_segments=96).rotate((90,0,0)).translate((173.6,-110.7,16.9))
    finger=m.Manifold.cylinder(14,7,circular_segments=96).rotate((0,90,0)).translate((198.2,-93.8,19.9))
    wheel=m.Manifold.cylinder(1,7,circular_segments=128).translate((191.325,-91.6,15.9))
    probes={'headphone_plug_body_OD10mm_fully_seated':plug,
            'wheel_side_fingertip_envelope_OD14mm':finger,'wheel_nominal_rotating_disk_OD14mm':wheel}
    installed={'tray':0,'unified-carrier':20.4,'lower-acrylic-reference':23.4,
               'paper-reference':24.4,'upper-acrylic-reference':24.55}
    result=dict(date='2026-10-25',hardware='NICHT GEMESSEN',
        test_scope='Geometric envelopes only; no actual cable, hand or accessibility measurement',parts={})
    for name,z in installed.items():
        path=ROOT/'unified-v3'/f'{name}.stl'; mesh=trimesh.load(path,force='mesh')
        mesh.apply_translation((0,0,z))
        solid=m.Manifold(m.Mesh(np.asarray(mesh.vertices,dtype=np.float32),np.asarray(mesh.faces,dtype=np.uint32)))
        assert solid.status()==m.Error.NoError,name
        volumes={label:round((solid^probe).volume(),8) for label,probe in probes.items()}
        result['parts'][name]=dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            overlap_mm3=volumes,passed=all(v<1e-4 for v in volumes.values()))
    result['passed']=all(x['passed'] for x in result['parts'].values())
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--record-blocked-baseline',action='store_true')
    p.add_argument('--output',type=Path,default=ROOT/'orientation-audit/right-access-after.json')
    args=p.parse_args(); result=evaluate()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps(result,indent=2))
    if args.record_blocked_baseline: assert not result['passed'],'Old access unexpectedly passed'
    else: assert result['passed'],'Access envelope blocked in actual exported STL'


if __name__=='__main__': main()
