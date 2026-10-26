"""One-piece direct-cap fit coupons; the user's 2026-10-26 simplification.

Reuse the established CSG/export implementation and the ONE parameter ledger.
No sleeves, moving printed parts, dummy PCB or dummy acrylic stack required.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

import build_v4_coupons as cad

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'simple-guide'


def main():
    parameters=ROOT/'v4-parameters.json'
    cad.P=json.loads(parameters.read_text(encoding='utf8'))
    cad.ROOT=OUT
    cad.PARTS={}
    v=cad.v
    z=v('c2','carrier_top'); h=v('c2','lower_plate_thickness')
    y=v('board','key_y'); e=v('general','csg_margin')
    x0=v('board','key_x')-2*v('c2','plate_margin_x')
    for shape,tag in [('square','S'),('round','R')]:
        solid=cad.c2_outline(z,h)
        openings=[]
        cap=v('c2','square_cap' if tag=='S' else 'round_cap')
        for i,gap in enumerate(v('c2','gaps')):
            x=v('board','key_x')+i*v('board','pitch')
            opening=cap+2*gap
            solid-=cad.keyhole(x,y,z-e,h+2*e,opening,shape)
            # Opaque FDM needs a light opening. Real clear lower acrylic may
            # remain closed over LEDs; this coupon tests cap guidance only.
            solid-=cad.hole_z(x,y-v('board','led_offset'),z,z+h,v('c2','led_hole'))
            openings.append(dict(source_xy=[x,y],size_mm=opening,clearance_per_side_mm=gap))
        cad.save('C2-direct-'+tag,solid,'C2-D'+tag,
            [x0,v('c2','plate_y_min')-v('general','label_pad_depth')+e,z],
            'One-piece side guide for original cap; no pull-out retention',
            family='direct-guide',shape=shape,openings=openings,
            print_orientation='flat underside on bed; no rotation or reflection',supports='none')
    manifest=dict(schema=1,hardware='NICHT GEMESSEN',
        generated_at_utc=datetime.now(timezone.utc).isoformat(),
        parameters='../v4-parameters.json',
        parameters_sha256=hashlib.sha256(parameters.read_bytes()).hexdigest(),
        generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        geometry_helper_sha256=hashlib.sha256(Path(cad.__file__).read_bytes()).hexdigest(),
        frame='RH: +X right/MIDI, +Y rear/audio, +Z up. Add installed_z_offset for PCB-top Z=0; no reflection on import.',
        decision='Original caps stay on switch stems; shared plate provides lateral guidance. Pull-out retention waived by user on 2026-10-26.',
        parts=cad.PARTS)
    (OUT/'coupons/manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8',newline='\n')
    subprocess.run([sys.executable,str(ROOT.parent/'check_stl_orientation.py'),
        '--v4-coupons',str(OUT),'--output',str(OUT/'coupons/orientation.json')],check=True)
    subprocess.run([sys.executable,str(ROOT/'validate_simple_guide.py')],check=True)
    # Each archive contains exactly ONE printable model, never the old catalog.
    target=ROOT.parents[2]/'build/case-v4-print'
    target.mkdir(parents=True,exist_ok=True)
    for name,meta in cad.PARTS.items():
        path=OUT/'coupons'/meta['file']
        archive=target/(name+'-EIN-TEIL.zip')
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as package:
            package.write(path,meta['file'])
            package.write(OUT/'README.md','START_HIER.md')
            package.write(OUT/'coupons/validation.json','validation.json')
        with zipfile.ZipFile(archive) as package:
            assert package.testzip() is None
            models=[p for p in package.namelist() if p.endswith('.stl')]
            assert models==[meta['file']]
            assert package.read(meta['file'])==path.read_bytes()
        print(archive)


if __name__=='__main__': main()
