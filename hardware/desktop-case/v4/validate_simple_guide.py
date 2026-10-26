"""Independent actual-STL checks for the direct-cap coupons; no CAD imports."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
import trimesh

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'simple-guide'
sys.path.insert(0,str(ROOT.parent))
from check_stl_orientation import evaluate_v4, winding
from validate_v4_coupons import source_box, source_cylinder, to_solid


def main():
    parameters=ROOT/'v4-parameters.json'
    p=json.loads(parameters.read_text(encoding='utf8'))
    manifest=json.loads((OUT/'coupons/manifest.json').read_text(encoding='utf8'))
    def v(group,key): return p[group][key]['value']
    tolerance=v('general','collision_tolerance')
    probes=ROOT.parents[2]/'build/v4-checks/direct-guide-probes'
    probes.mkdir(parents=True,exist_ok=True)
    def imported_probe(name,mesh):
        path=probes/(name+'.stl')
        mesh.export(path)
        loaded=trimesh.load(path,force='mesh')
        assert loaded.is_watertight and loaded.is_winding_consistent
        return to_solid(loaded)
    results={}
    for name,meta in manifest['parts'].items():
        path=OUT/'coupons'/meta['file']
        mesh=trimesh.load(path,force='mesh')
        closed=bool(mesh.is_watertight and mesh.is_winding_consistent and mesh.volume>0)
        single=len(to_solid(mesh).decompose())==1
        on_bed=bool(abs(mesh.bounds[0,2])<1e-5)
        mesh.apply_translation((0,0,meta['installed_z_offset']))
        solid=to_solid(mesh)
        # PCB-derived independent physical witnesses. They do not come from
        # the generator's marker coordinates or export transformation.
        z=v('c2','carrier_top')+v('c2','lower_plate_thickness')/2
        witnesses=[]
        for label,empty,filled in [('S1',(69,-85,z),(73,-85,z)),
                                   ('D1 behind S1',(69,-78,z),(72,-78,z))]:
            a,b=winding(mesh,empty),winding(mesh,filled)
            witnesses.append(dict(landmark=label,opening_mm=empty,material_mm=filled,
                opening_winding=a,material_winding=b,passed=bool(abs(a)<1e-4 and abs(b-1)<1e-4)))
        cap=v('c2','square_cap' if meta['shape']=='square' else 'round_cap')
        height=v('c2','cap_height_'+meta['shape'])
        checks=[]
        for i in range(int(v('c2','count'))):
            x=v('board','key_x')+i*v('board','pitch'); y=v('board','key_y')
            bottom=v('c2','cap_top')-height
            shape=source_box(x-cap/2,y-cap/2,bottom,cap,cap,height) if meta['shape']=='square' else source_cylinder(x,y,bottom,cap,height)
            cap_solid=imported_probe(name+'-cap-'+str(i+1),shape)
            # The cap remains intentionally removable. No hidden flange or
            # extra printed holder may collide at rest, stroke or withdrawal.
            for state,dz in [('rest',0),('pressed',-v('c2','stroke')),('withdrawn',v('c2','cap_height_'+meta['shape']))]:
                volume=max(0.,float((solid^cap_solid.translate((0,0,dz))).volume()))
                checks.append(dict(station=i+1,state=state,intersection_mm3=round(volume,8),passed=volume<tolerance))
            sw,sd,sh=v('c2','switch_housing_box')
            housing=imported_probe(name+'-housing-'+str(i+1),source_box(x-sw/2,y-sd/2,0,sw,sd,sh))
            volume=max(0.,float((solid^housing).volume()))
            checks.append(dict(station=i+1,state='switch housing',intersection_mm3=round(volume,8),passed=volume<tolerance))
        results[name]=dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            watertight_outward=closed,connected_components=1 if single else 'FAILED',
            bed_z_zero=on_bed,independent_pcb_witnesses=witnesses,collisions=checks,
            passed=closed and single and on_bed and all(c['passed'] for c in checks+witnesses))
    orientation=evaluate_v4(OUT)
    passed=orientation['passed'] and all(r['passed'] for r in results.values())
    report=dict(generated_at_utc=datetime.now(timezone.utc).isoformat(),hardware='NICHT GEMESSEN',
        status='NOMINAL',parameters_sha256=hashlib.sha256(parameters.read_bytes()).hexdigest(),
        validator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        orientation=orientation,parts=results,passed=passed,
        meaning='One-piece lateral guides ONLY; original cap/socket grip and hardware fit unmeasured. Caps intentionally NOT captive.',
        limits=['Cap height 13 and typical stroke .25 are assumptions; measure actual hardware.',
                'No pull-out retention required after user decision of 2026-10-26.',
                'FDM guide is a fit coupon, not a transparent acrylic optical test.'])
    (OUT/'coupons/validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps(dict(passed=passed,print_parts_per_shape=1,shapes=len(results)),indent=2))
    if not passed: raise SystemExit(1)


if __name__=='__main__': main()
