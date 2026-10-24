"""Prepare a read-only KiCad render copy with resolved STEP paths (KiCad Python).

The source PCB is never overwritten. Missing packages stay listed explicitly.
Run with KiCad's bundled Python, not the case mesh virtual environment.
"""

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
REF = REPO/'build/case-reference'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--board',type=Path,default=REF/'ES-Seed3-Devkit-Desktop-Rev4.kicad_pcb')
    p.add_argument('--models',type=Path,default=Path('C:/Program Files/KiCad/10.0/share/kicad/3dmodels'))
    p.add_argument('--cli',default='C:/Program Files/KiCad/10.0/bin/kicad-cli.exe')
    p.add_argument('--output',type=Path,default=ROOT/'output/realistic-v3')
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    board=pcbnew.LoadBoard(str(args.board))
    rows=[]; source_models={}
    for fp in board.GetFootprints():
        resolved=[]
        for model in fp.Models():
            rel=model.m_Filename.split('}/')[-1]
            if '${KIPRJMOD}' in model.m_Filename:
                path=REF/Path(rel).name
            else:
                path=args.models/rel
            if path.suffix.lower()=='.wrl': path=path.with_suffix('.step')
            if fp.GetReference()=='A1':
                continue  # Replace obsolete Seed model with the actual Seed3 FBX in Blender.
            if path.is_file():
                model.m_Filename=path.as_posix()
                resolved.append(model)
                source_models[path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
        # Clone values before clearing the SWIG vector.
        copies=[]
        for item in resolved:
            clone=pcbnew.FP_3DMODEL()
            clone.m_Filename=item.m_Filename
            clone.m_Offset=item.m_Offset
            clone.m_Rotation=item.m_Rotation
            clone.m_Scale=item.m_Scale
            copies.append(clone)
        fp.Models().clear()
        for item in copies: fp.Models().push_back(item)
        pos=fp.GetPosition()
        rows.append(dict(ref=fp.GetReference(),footprint=str(fp.GetFPID().GetLibItemName()),
            x=round(pcbnew.ToMM(pos.x)-50,6),y=round(pcbnew.ToMM(pos.y)-44,6),
            rotation=fp.GetOrientationDegrees(),models=[Path(x.m_Filename).name for x in copies]))
    target=args.output/'resolved-board.kicad_pcb'
    pcbnew.SaveBoard(str(target),board)
    inventory=dict(kicad_version=pcbnew.Version(),source_commit='987297fb7c9a8c58721ef40abf6d6eedd1fcda2a',
        source_board_sha256=hashlib.sha256(args.board.read_bytes()).hexdigest(),
        components=rows,model_sha256=source_models,
        imported_model_footprints=sum(bool(x['models']) for x in rows),
        missing_model_refs=[x['ref'] for x in rows if not x['models']],
        seed3_fbx_sha256=hashlib.sha256((REF/'seed3-models/ES_Daisy_Seed3_Rev5.fbx').read_bytes()).hexdigest())
    (args.output/'board-model-inventory.json').write_text(json.dumps(inventory,indent=2)+'\n',encoding='utf8',newline='\n')
    cmd=[args.cli,'pcb','export','glb','--force','--user-origin','50x44mm','--include-pads','--include-silkscreen',
         '--output',str(args.output/'board-raw.glb'),str(target)]
    subprocess.run(cmd,check=True)
    print(json.dumps(dict(footprints=len(rows),modelled=inventory['imported_model_footprints'],
        missing=inventory['missing_model_refs']),indent=2))


if __name__=='__main__': main()
