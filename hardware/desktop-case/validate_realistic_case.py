"""Verify final model units, envelopes, geometry provenance and rendered images."""

import hashlib
import json
from pathlib import Path

import numpy as np
import trimesh
from PIL import Image

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'output/realistic-v3'
PARTS=ROOT/'unified-v3'
report=json.loads((OUT/'render-manifest.json').read_text())
case=json.loads((PARTS/'validation.json').read_text())
for name,digest in report['source_sha256'].items():
    p=PARTS/name if name=='validation.json' else ROOT/name
    assert hashlib.sha256(p.read_bytes()).hexdigest()==digest,('source hash',name)
for name,digest in report['artifact_sha256'].items():
    assert hashlib.sha256((OUT/name).read_bytes()).hexdigest()==digest,('artifact hash',name)
original=[]
for name in ['tray','unified-carrier','lower-acrylic-reference','paper-reference','upper-acrylic-reference']:
    p=PARTS/f'{name}.stl'
    assert hashlib.sha256(p.read_bytes()).hexdigest()==case['artifact_sha256'][p.name],name
    mesh=trimesh.load(p,force='mesh')
    assert mesh.is_watertight and mesh.is_winding_consistent,name
    original.append(name)
stl=trimesh.load(OUT/'case-assembly-view.stl',force='mesh')
assert np.isfinite(stl.vertices).all()
assert np.allclose(stl.extents,[215,129,39.8],atol=.01),stl.extents
glb=trimesh.load(OUT/'desktop-case-assembly.glb',force='scene')
assert np.isfinite(glb.bounds).all()
assert np.allclose(glb.extents*1000,[215,39.8,129],atol=.1),glb.extents
for name in ['studio','rear','exploded']:
    im=Image.open(OUT/f'case-realistic-{name}.png')
    assert im.size==(2400,1600),(name,im.size)
result=dict(date='2026-10-24',hardware='NICHT GEMESSEN',
    print_source_parts_unchanged_and_closed=original,
    assembly_stl_extents_mm=stl.extents.round(5).tolist(),
    assembly_stl_merged_watertight=bool(stl.is_watertight),
    assembly_stl_note='Visualization only: touching layers retain their own coincident interface faces',
    glb_extents_metres=glb.extents.round(8).tolist(),glb_meshes=len(glb.geometry),
    render_resolution=[2400,1600],source_and_artifact_hashes_verified=True,
    visual_review='Separate manual inspection of all three images required')
(OUT/'export-validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8',newline='\n')
print(json.dumps(result,indent=2))
