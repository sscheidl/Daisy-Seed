"""Reopen V3 STL files for review probes, independent of cut-generator helpers."""

import hashlib
import json
import math
from pathlib import Path

import manifold3d as m
import numpy as np
import trimesh

ROOT=Path(__file__).resolve().parent


def main():
    # Physical RH coordinates. Dimensions are drawing nominal or explicit test targets.
    probes={}
    for diameter in [11.4,12,14]:
        probes[f'J4_rear_OD{diameter:g}_axis_PCB_plus_8.14']=(
            m.Manifold.cylinder(25,diameter/2,circular_segments=128)
            .rotate((-90,0,0)).translate((39.915,2.29,22.54)))
    # Reference GCT axis only: the BOM names a different populated USB part.
    probes['J7_reference_USB_plug_body_12x6']=(m.Manifold.cube((25,12,6))
        .translate((-25.28,-47.09,13.08)))
    # A SW8 nut is a test target, not a claim about the supplied jack nut.
    probes['CV_OUT1_SW8_nut_hypothesis']=(m.Manifold.cylinder(1,8/math.sqrt(3),circular_segments=6)
        .translate((82,-15.8,24.55)))
    installed={'tray':0,'unified-carrier':20.4,'lower-acrylic-reference':23.4,
               'paper-reference':24.4,'upper-acrylic-reference':24.55}
    result=dict(date='2026-10-25',hardware='NICHT GEMESSEN',
        scope='Design targets and nominal drawing geometry, not actual plug or nut measurements',parts={})
    for name,z in installed.items():
        path=ROOT/'unified-v3'/f'{name}.stl'
        mesh=trimesh.load(path,force='mesh'); mesh.apply_translation((0,0,z))
        solid=m.Manifold(m.Mesh(np.asarray(mesh.vertices,dtype=np.float32),np.asarray(mesh.faces,dtype=np.uint32)))
        assert solid.status()==m.Error.NoError,name
        result['parts'][name]=dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            overlap_mm3={label:round((solid^probe).volume(),6) for label,probe in probes.items()})
    result['calculations']=dict(audio_axis_error_mm=round(8.9-8.14,3),
        ideal_coaxial_plug_limit_with_axis_offset=round(13.6-2*abs(8.9-8.14),3),
        SW8_hex_corner_diameter=8/math.cos(math.pi/6),square_5mm_diagonal=5*math.sqrt(2),
        LED_point_at_PCB_original_limit_degrees=math.degrees(math.atan(2.4/9)),
        LED_point_at_PCB_cone_if_lower_3mm_at_height_6mm=math.degrees(math.atan(1.5/6)),
        LED_point_at_PCB_cone_if_upper_8mm_at_height_9mm=math.degrees(math.atan(4/9)),
        knurlie_current_conservative_engagement=6-.8-1-.15-1,
        knurlie_alternative_conservative_engagement=6-1-.15-1,
        round_cap_diagram_height=9.05,round_cap_diagram_max_diameter=5.08)
    path=ROOT/'orientation-audit/functional-review-probes.json'
    path.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
