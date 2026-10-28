"""Check saved study scenes against their original assembly, without rendering."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

import bpy

parser = argparse.ArgumentParser()
parser.add_argument('--assembly', type=Path, required=True)
parser.add_argument('--studies', type=Path, required=True)
args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
ROOT = Path(__file__).resolve().parent.parent


def meshes(names=None):
    result = {}
    for o in bpy.data.objects:
        if o.type != 'MESH' or (names is not None and o.name not in names):
            continue
        digest = hashlib.sha256()
        for v in o.data.vertices:
            digest.update(struct.pack('<3f', *v.co))
        result[o.name] = dict(transform=[list(row) for row in o.matrix_world],
                              vertices=len(o.data.vertices), sha256=digest.hexdigest())
    return result


bpy.ops.wm.open_mainfile(filepath=str(args.assembly))
original = meshes()
parameters = json.loads((ROOT/'v4/v4-parameters.json').read_text(encoding='utf8'))
for prefix, points in [('cap-square-', parameters['full_board']['keys']['value']),
                       ('pot-shaft-', parameters['full_board']['pots']['value'])]:
    for i, (x, y) in enumerate(points):
        o = bpy.data.objects[prefix+str(i)]
        world = [o.matrix_world@v.co for v in o.data.vertices]
        center = [(min(v[d] for v in world)+max(v[d] for v in world))/2 for d in range(2)]
        assert abs(center[0]-x) < .01 and abs(center[1]+y) < .01, (o.name, center, x, y)

result = dict(status='PASS / DIGITAL ONLY', source_meshes=len(original), studies={})
for key in 'abc':
    bpy.ops.wm.open_mainfile(filepath=str(args.studies/f'{key}-study.blend'))
    current = meshes(original)
    assert current == original, f'{key}: original assembly geometry or transform changed'
    skin = bpy.data.objects['Exterior study - NOT FOR PRINT']
    bounds = [(min(v.co[i] for v in skin.data.vertices), max(v.co[i] for v in skin.data.vertices))
              for i in range(3)]
    assert bounds[0][0] >= -4.001 and bounds[0][1] <= 199.001, bounds
    assert bounds[1][0] >= -119.801 and bounds[1][1] <= 8.801, bounds
    assert bounds[2][0] >= 21.199 and bounds[2][1] <= 42.001, bounds
    assert bpy.data.objects['upper-acrylic'].hide_render
    result['studies'][key] = dict(original_geometry_and_transforms='unchanged',
                                  exterior_bounds_mm=bounds,
                                  hardware='NICHT GEMESSEN')

manifest = json.loads((ROOT/'v4/full/manifest.json').read_text(encoding='utf8'))
for name, part in manifest['parts'].items():
    path = ROOT/'v4/full'/part['file']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == part['sha256'], name
result['v4_stl_hashes'] = 'all 7 match the original manifest'
for path, expected in [
    ('v4/full/V4-KOMPLETTDRUCK.zip', '149744844e458cf70defb956b921fbe456a8cf2aa84202ca4ef05e46d20a13c9'),
    ('v4/fit-print/V4-PASSPROBE-SPARPROFIL.zip', '37e77416012026e7782121c7d9d67f9be9bf878070a1c2150890f01e927d0b43'),
]:
    assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest() == expected, path
result['v4_zip_hashes'] = 'both match the handover'
(args.studies/'review.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf8')
print(json.dumps(result, indent=2))
