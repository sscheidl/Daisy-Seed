"""Merge KiCad GLB face primitives by material and align them with case mm datums."""

import json
from pathlib import Path
import numpy as np
import trimesh

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'output/realistic-v3'
scene=trimesh.load(OUT/'board-raw.glb',force='scene')
groups={}; materials={}
for node in scene.graph.nodes_geometry:
    transform,name=scene.graph[node]
    geom=scene.geometry[name]
    material=geom.visual.material
    rgba=tuple(material.baseColorFactor.tolist())
    mesh=geom.copy(); mesh.apply_transform(transform)
    groups.setdefault(rgba,[]).append(mesh)
    materials[rgba]=material.copy()
result=trimesh.Scene()
# KiCad GLB is metres, Y up, board top at +1.6 mm, board Y toward positive Z.
# Keep glTF Y up: Blender's importer performs the Y-up to Z-up conversion.
# This intermediate is in mm; the final assembly export restores glTF metres.
matrix=np.array([[1000,0,0,0],[0,1000,0,12.8],[0,0,1000,0],[0,0,0,1]],float)
for i,(key,meshes) in enumerate(groups.items()):
    mesh=trimesh.util.concatenate(meshes); mesh.apply_transform(matrix)
    mesh.visual=trimesh.visual.TextureVisuals(material=materials[key])
    result.add_geometry(mesh,node_name=f'KiCad_material_{i}',geom_name=f'KiCad_material_{i}')
result.export(OUT/'board-mm.glb')
print(json.dumps(dict(input_primitives=len(scene.geometry),material_groups=len(groups),
    bounds_mm=result.bounds.tolist(),triangles=sum(len(g.faces) for g in result.geometry.values())),indent=2))
