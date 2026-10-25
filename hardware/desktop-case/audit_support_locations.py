"""Check the concept's complete support discs against rear copper pad/via shapes.

Run using KiCad Python. This is a 2D exclusion check, not a support approval:
solder tails, mask, tracks, board bow and material compliance remain unmeasured.
"""

import hashlib
import json
from pathlib import Path

import pcbnew as p

ROOT = Path(__file__).resolve().parent
BOARD = ROOT.parents[1] / 'build/case-reference/ES-Seed3-Devkit-Desktop-Rev4.kicad_pcb'
POINTS = [('USB J7',10,41), ('Audio left',43.5,11), ('Audio right',144,11),
          ('MIDI',178,37), ('CV 1',102,23), ('CV 2',77.5,23.5),
          ('Pot 1',82,51.5), ('Pot 2',112.5,51.5), ('Pot 3',143,51.5),
          ('Keys 1',87,93), ('Keys 2',137,93), ('Toggle',39,90)]


def main():
    board = p.LoadBoard(str(BOARD))
    pads = [(f.GetReference(), pad) for f in board.GetFootprints() for pad in f.Pads()
            if pad.IsOnLayer(p.B_Cu)]
    vias = [t for t in board.GetTracks() if isinstance(t,p.PCB_VIA) and t.IsOnLayer(p.B_Cu)]
    report = dict(date='2026-10-25',hardware='NICHT GEMESSEN',
        scope='Exact KiCad B.Cu pad shapes and via copper circles; no solder-tail or mechanical model',
        board_sha256=hashlib.sha256(BOARD.read_bytes()).hexdigest(),
        source_frame='PCB rear-left; X right, Y toward player',
        back_footprints=[f.GetReference() for f in board.GetFootprints() if f.GetLayer()==p.B_Cu],
        back_pad_count=len(pads),via_count=len(vias),supports=[])
    declared=json.loads((ROOT/'board.json').read_text())
    mechanics=json.loads((ROOT/'closed-mechanics.json').read_text())
    footprints={f.GetReference():f for f in board.GetFootprints()}
    controls=[]
    for group in ['mounts','buttons','pots','leds','toggles']:
        controls.extend(declared[group])
    controls.extend(mechanics['jacks'])
    report['control_xy_errors_mm']={}
    for control in controls:
        pos=footprints[control['ref']].GetPosition()
        dx=p.ToMM(pos.x)-50-control['x']; dy=p.ToMM(pos.y)-44-control['y']
        report['control_xy_errors_mm'][control['ref']]=[round(dx,8),round(dy,8)]
    report['all_52_control_centres_match']=all(abs(a)<1e-6 and abs(b)<1e-6
        for a,b in report['control_xy_errors_mm'].values())
    assert report['all_52_control_centres_match']
    for label,x,y in POINTS:
        center=p.VECTOR2I(p.FromMM(x+50),p.FromMM(y+44))
        probe=p.SHAPE_CIRCLE(center,p.FromMM(3))
        hits=[]
        for ref,pad in pads:
            if pad.GetEffectiveShape(p.B_Cu).Collide(probe):
                pos=pad.GetPosition()
                hits.append(dict(ref=ref,pad=pad.GetNumber(),
                    center_xy=[round(p.ToMM(pos.x)-50,6),round(p.ToMM(pos.y)-44,6)]))
        via_hits=[]
        for via in vias:
            pos=via.GetPosition()
            radius=p.FromMM(3)+via.GetWidth(p.B_Cu)/2
            if (pos.x-center.x)**2+(pos.y-center.y)**2 <= radius**2:
                via_hits.append([round(p.ToMM(pos.x)-50,6),round(p.ToMM(pos.y)-44,6)])
        report['supports'].append(dict(label=label,center_xy=[x,y],diameter=6,
            pad_intersections=hits,via_intersections=via_hits,
            rear_copper_clear=not hits and not via_hits))
    report['connector_footprints']=[]
    for f in board.GetFootprints():
        if f.GetReference() not in ['J1','J2','J3','J4','J5','J7','J8','J10','J11','MICRO_SD7','A1','RV1']:
            continue
        box=f.GetBoundingBox(False,False)
        report['connector_footprints'].append(dict(ref=f.GetReference(),
            xy=[p.ToMM(f.GetPosition().x)-50,p.ToMM(f.GetPosition().y)-44],
            rotation=f.GetOrientationDegrees(),
            footprint=str(f.GetFPID().GetLibItemName()),
            graphics_and_pads_bounds_xy=[p.ToMM(box.GetX())-50,p.ToMM(box.GetY())-44,
                p.ToMM(box.GetRight())-50,p.ToMM(box.GetBottom())-44]))
    target=ROOT/'orientation-audit/support-location-review.json'
    target.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps(dict(back_pads=len(pads),vias=len(vias),
        supports=[dict(label=s['label'],pads=len(s['pad_intersections']),vias=len(s['via_intersections']))
            for s in report['supports']]),indent=2))


if __name__=='__main__':
    main()
