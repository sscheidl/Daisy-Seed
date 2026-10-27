"""Reproduce inaccessible old fasteners and reject old STL with new checks.

Reads the old files from local Git history; never checks out or changes them.
"""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys

import trimesh

from validate_v4 import Validation, to_solid

ROOT=Path(__file__).resolve().parent
REPO=ROOT.parents[2]
REF='b904a7e'
PREFIX=ROOT.relative_to(REPO).as_posix()


def blob(relative):
    return subprocess.check_output(['git','show',REF+':'+PREFIX+'/'+relative],cwd=REPO)


def main():
    manifest=json.loads(blob('full/manifest.json'))
    params=json.loads(blob('v4-parameters.json'))
    old=Validation(); hashes={}
    for name,meta in manifest['parts'].items():
        raw=blob('full/'+meta['file']); hashes[name]=hashlib.sha256(raw).hexdigest()
        assert hashes[name]==meta['sha256']
        mesh=trimesh.load(io.BytesIO(raw),file_type='stl',force='mesh')
        mesh.apply_translation((0,0,meta['installed_z_offset']))
        old.meshes[name]=mesh; old.solids[name]=to_solid(mesh)
    old.manifest=manifest
    original=[]
    for i,(x,z) in enumerate(params['case']['rear_plate_mounts']['value']):
        case=lambda k:params['case'][k]['value']
        screw=lambda k:params['screw'][k]['value']
        plane=case('audio_planes_y')[0 if x<case('rear_plate_step_x') else 1]
        face=plane-case('rear_plate_thickness'); start=case('outer')[1]-old.q('rear_driver_reach')
        for label,y,diameter,length in [('head seated',face-screw('head_height'),screw('head_diameter'),screw('head_height')),
            ('head insertion',start,screw('head_diameter'),face-start),
            ('straight hex key',start,old.q('rear_driver_diameter'),face-start)]:
            probe=old.cylinder(f'regression-old-{i}-{label.replace(" ","-")}',x,y,z,diameter,length,'y')
            volume=max(0.,float((old.solids['carrier']^probe).volume()))
            assert volume>old.tol
            original.append(dict(mount_x=x,test=label,blocked_by='carrier',intersection_mm3=round(volume,7)))
    old.rear_fastener_checks(); old.paired_jack_checks(); old.landmarks()
    failures=[x for x in old.checks if not x['passed']]
    failed_landmarks=[dict(part=name,landmark=x['landmark']) for name,data in old.orientation.items() for x in data['checks'] if not x['passed']]
    assert sum(x['group']=='paired_jacks' for x in failures)==2
    assert sum(x['group']=='rear_fasteners' for x in failures)==6
    assert len(failed_landmarks)==4
    new=Validation(); new.rear_fastener_checks(); new.paired_jack_checks(); new.landmarks()
    assert all(x['passed'] for x in new.checks)
    assert all(x['passed'] for x in new.orientation.values())
    data=dict(passed=True,old_git_reference=REF,old_stl_sha256=hashes,
        parameters_sha256=hashlib.sha256((ROOT/'v4-parameters.json').read_bytes()).hexdigest(),
        original_fastener_obstructions=original,new_checks_reject_old_geometry=failures,
        old_geometry_failed_landmarks=failed_landmarks,
        new_geometry_targeted_checks=len(new.checks),new_geometry_failures=0,
        corrected_stl_sha256={name:meta['sha256'] for name,meta in new.manifest['parts'].items()},
        hardware='NICHT GEMESSEN',validator_sha256=hashlib.sha256((ROOT/'validate_v4.py').read_bytes()).hexdigest(),
        regression_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    (ROOT/'full/access-regression.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps(dict(passed=True,original_obstructions=len(original),old_geometry_rejections=len(failures),
        old_landmark_failures=len(failed_landmarks),new_targeted_checks=len(new.checks),new_failures=0)))


if __name__=='__main__': main()
