"""V3 fit study: one printed structural deck with integral hoods and key guide.

Reuses the pinned V2 board coordinates and connector access volumes. All hardware
heights remain nominal or provisional; geometric validation is not a fit test.
"""

import argparse
import hashlib
import json
from pathlib import Path

from reportlab.lib.pagesizes import A4, landscape
from reportlab.lib.units import mm
from reportlab.pdfgen import canvas

import build_case as b
import build_closed_case as v2

ROOT = Path(__file__).resolve().parent
C = v2.C
PCB = v2.PCB_TOP
DECK_TOP = v2.LOWER_Z
SKIN = 2.4
DECK_BOTTOM = DECK_TOP-SKIN
POST_BODY = 8.0
POST_SHIM = .2
POST_TOP = PCB+POST_BODY+POST_SHIM
LED_APERTURE = 4.8
# Centers extracted from the pinned Rev4 KiCad footprints. Body height is nominal.
CAPACITORS = [(6.2,59.4),(32.5,36.5),(23,20.5),(6.2,52.2),(23,36.5),
              (172.8,86.6),(165.4,86.6),(23,28.5),(40.5,36.5),(20.5,55)]


def layer_section(kind):
    section = b.rounded_rectangle(-9,-9,215,129,3)
    # Both clear layers now surround integral hoods; no separate hood fasteners.
    section -= v2.hood_outline().offset(.35)
    section -= v2.service_outline().offset(.35)
    for p in b.BOARD['pots']:
        section -= v2.circle(p['x'],p['y'],C['pot_opening'])
    for p in b.BOARD['buttons']:
        section -= b.rectangle(p['x']-3.2,p['y']-3.2,6.4,6.4)
    for p in C['jacks']:
        section -= v2.circle(p['x'],p['y'],C['jack_lower_hole'] if kind=='lower' else C['jack_upper_hole'])
    if kind=='upper':
        for y in [15.8,31.4]:
            section -= b.rectangle(112.3,y-4.6,10.1,9.2)
    for p in b.BOARD['toggles']:
        section -= v2.circle(p['x'],p['y'],C['toggle_lower_hole'] if kind=='lower' else C['toggle_upper_hole'])
    section -= v2.circle(191.325,91.6,16)
    for p in b.BOARD['mounts']:
        section -= v2.circle(p['x'],p['y'],3.4)
    return section


def guide_section():
    section = b.rectangle(57,75,110,33)
    # Square guides accept either square or round 5-mm caps; test coupon first.
    for p in b.BOARD['buttons']:
        section -= b.rectangle(p['x']-2.9,p['y']-2.9,5.8,5.8)
    for p in b.BOARD['leds']:
        section -= v2.circle(p['x'],p['y'],LED_APERTURE)
    return section


def skin_section():
    section = b.rounded_rectangle(-9,-9,215,129,3)
    for x in b.AUDIO_AXES:
        section -= b.rectangle(x-10,-10,20,39)
    section -= b.rectangle(178,9,30,74)
    section -= b.rectangle(-10,64,63.5,21)
    for p in b.BOARD['pots']:
        section -= v2.circle(p['x'],p['y'],C['pot_opening'])
    for p in b.BOARD['buttons']:
        section -= b.rectangle(p['x']-2.9,p['y']-2.9,5.8,5.8)
    for p in b.BOARD['leds']:
        section -= v2.circle(p['x'],p['y'],LED_APERTURE)
    # Jack bodies pass through the carrier; only the 1-mm lower acrylic is clamped.
    for p in C['jacks']:
        section -= b.rectangle(p['x']-5,p['y']-6.5,10,11.5)
    for p in b.BOARD['toggles']:
        section -= b.rectangle(p['x']-4,p['y']-6.5,8,13)
    section -= v2.circle(191.325,91.6,16)
    for p in b.BOARD['mounts']:
        section -= v2.circle(p['x'],p['y'],3.4)
    return section


def build():
    outer = b.rounded_rectangle(-9,-9,215,129,3)
    inner = b.rounded_rectangle(-6.6,-6.6,210.2,124.2,2)
    tray = v2.extrude(outer,0,DECK_BOTTOM)-v2.extrude(inner,b.FLOOR,DECK_BOTTOM)
    for p in b.BOARD['mounts']:
        tray -= b.cylinder(p['x'],p['y'],-1,1.7,b.FLOOR+2)
        tray -= b.cylinder(p['x'],p['y'],-.1,3.2,1.5)
    for x,y,w,d in b.tie_slots():
        tray -= b.box(x,y,-1,w,d,b.FLOOR+2)
    tray -= v2.port_cuts()

    deck = v2.extrude(skin_section(),DECK_BOTTOM,SKIN)
    deck += v2.extrude(guide_section(),v2.GUIDE_Z,3)
    # Recesses clear nominal taller components without raising the control face.
    for p in b.BOARD['pots']:
        deck -= b.box(p['x']-6,p['y']-7,DECK_BOTTOM-.1,12,14,PCB+7.3-DECK_BOTTOM+.1)
    for x,y in CAPACITORS:
        deck -= b.cylinder(x,y,PCB,5,8.3)
    for p in b.BOARD['mounts']:
        # The metal post seats against the remaining 0.8-mm local land.
        deck -= b.cylinder(p['x'],p['y'],DECK_BOTTOM-.1,3.5,POST_TOP-DECK_BOTTOM+.1)

    # Overlap the walls into the skin by 0.1 mm to form one printable solid.
    start = DECK_TOP-.1
    rear = v2.extrude(v2.rear_outline(),start,v2.ROOF_Z+v2.SHELL_T-start)
    rear -= v2.extrude(b.rectangle(22.4,-6.6,150.1,48.2),start-.1,v2.ROOF_Z-start+.1)
    collar = b.rectangle(73,5,57,39)-b.rectangle(74,6,55,40)
    rear += v2.extrude(collar,start,v2.ROOF_Z+v2.SHELL_T-start)
    for x in range(32,69,6):
        rear -= b.box(x,32,v2.ROOF_Z-1,2.2,8,5)
    midi = v2.extrude(v2.midi_outline(),start,v2.ROOF_Z+v2.SHELL_T-start)
    midi -= v2.extrude(b.rectangle(179.5,9.9,24.1,73.7),start-.1,v2.ROOF_Z-start+.1)
    seed = v2.extrude(v2.service_outline(),start,v2.SERVICE_Z+v2.SHELL_T-start)
    seed -= v2.extrude(b.rectangle(-6.6,64.5,60.7,21.1),start-.1,v2.SERVICE_Z-start+.1)
    for p in C['service_positions_provisional']:
        seed += b.cylinder(p['x'],p['y'],v2.SERVICE_Z-3.5,3.5,3.5)
        seed -= b.cylinder(p['x'],p['y'],v2.SERVICE_Z-3.6,1.5,8)
    for x in range(27,49,5):
        seed -= b.box(x,65,v2.SERVICE_Z-1,2,16,5)
    deck = b.union([deck,rear,midi,seed])-v2.port_cuts()

    parts = {'tray':(tray,0),'unified-carrier':(deck,v2.GUIDE_Z),
             'lower-acrylic-reference':(v2.extrude(layer_section('lower'),0,1),DECK_TOP),
             'upper-acrylic-reference':(v2.extrude(layer_section('upper'),0,1),v2.UPPER_Z)}
    previous = v2.build()
    for name in ['removable-service-tool-fit-study','guide-clearance-coupon','H2-S9-cap-height-gauge']:
        parts[name] = previous[name]
    paper = layer_section('upper')-b.rectangle(*b.KEY_WINDOW)
    parts['paper-reference'] = (v2.extrude(paper,0,C['paper']),DECK_TOP+1)
    return parts


def installed(parts):
    return {name:solid if name in ['tray','unified-carrier'] else solid.translate((0,0,z))
            for name,(solid,z) in parts.items()}


def verify(parts):
    report = {'date':'2026-10-24','hardware':'NICHT GEMESSEN','manufacturing_ready':False,'parts':{}}
    for name,(solid,z) in parts.items():
        mesh = b.mesh_of(solid)
        assert mesh.is_watertight and mesh.is_winding_consistent,name
        assert len(solid.decompose())==1,(name,len(solid.decompose()))
        installed_z = float(mesh.bounds[0,2]) if name in ['tray','unified-carrier'] else z
        report['parts'][name] = dict(size_mm=mesh.extents.round(4).tolist(),closed=True,components=1,installed_z=installed_z)
    assembly = installed(parts)
    main = ['tray','unified-carrier','lower-acrylic-reference','paper-reference','upper-acrylic-reference']
    pairs = [(a,d) for i,a in enumerate(main) for d in main[i+1:]]
    for a,d in pairs:
        overlap = (assembly[a]^assembly[d]).volume()
        assert overlap<1e-4,(a,d,overlap)
    for name in main:
        assert (assembly[name]^v2.port_cuts()).volume()<1e-4,(name,'port blocked')
    for p in b.BOARD['buttons']:
        swept = b.box(p['x']-2.8,p['y']-2.8,PCB+4,5.6,5.6,15)
        for name in ['unified-carrier','lower-acrylic-reference','upper-acrylic-reference']:
            assert (assembly[name]^swept).volume()<1e-4,(name,p['ref'])
    for p in b.BOARD['leds']:
        sight = b.cylinder(p['x'],p['y'],PCB,LED_APERTURE/2-.01,24)
        assert (assembly['unified-carrier']^sight).volume()<1e-4,('LED blocked',p['ref'])
    # Check nominal component envelopes in addition to connector access sweeps.
    envelopes = []
    for x,y in CAPACITORS:
        envelopes.append(b.cylinder(x,y,PCB,3.15,7.7))
    for p in b.BOARD['pots']:
        envelopes.append(b.box(p['x']-4.5,p['y']-5.3,PCB,9,10.6,7))
    for p in C['jacks']:
        envelopes.append(b.box(p['x']-4.5,p['y']-6,PCB,9,10.5,9))
    for p in b.BOARD['mounts']:
        envelopes.append(b.cylinder(p['x'],p['y'],PCB,5.5/(3**.5),POST_BODY+POST_SHIM,6))
    for index,body in enumerate(envelopes):
        assert (assembly['unified-carrier']^body).volume()<1e-4,('body collision',index)
    land = DECK_TOP-POST_TOP
    grip = C['knurlie_usable_min']-land-1-C['paper']-1
    screw_gap = C['lower_metal_post']-(C['upper_post_male_stud']-POST_SHIM-b.BOARD['thickness'])-(C['floor_screw_length']-b.FLOOR+C['floor_head_recess'])
    assert 3<=grip<=C['post_female_thread_min']
    assert screw_gap>=.5
    report['stack_mm'] = dict(pcb_top=PCB,carrier_bottom=DECK_BOTTOM,carrier_top=DECK_TOP,
        carrier_local_seat=round(land,3),upper_post_body=POST_BODY,upper_post_shim=POST_SHIM,
        lower_bottom=DECK_TOP,lower_top=DECK_TOP+1,upper_bottom=v2.UPPER_Z,upper_top=v2.UPPER_Z+1,
        knurlie_min_engagement=round(grip,3),lower_spacer_internal_screw_gap=round(screw_gap,3),
        cap_rest_assumption=C['cap_rest_height_assumed'],cap_pressed_exposure_assumption=round(
            C['cap_rest_height_assumed']-C['cap_travel_nominal']-(v2.UPPER_Z+C['upper_acrylic']-PCB),3))
    report['checks'] = ['Eight closed, consistently wound, single-component bodies',
        'Unified carrier is exactly one connected solid',f'{len(pairs)} assembly collision pairs clear',
        'All five assembly solids clear nominal connector access sweeps',
        'All 16 key clearance envelopes pass through carrier and both acrylics',
        'All 16 LED vertical apertures clear',
        f'{len(envelopes)} nominal capacitor, pot, jack and metal-post envelopes clear the carrier',
        'Conservative Knurlie grip 3.05 mm; opposing lower-post screws have 1.2 mm gap']
    report['unresolved'] = ['Actual cap seating height, friction, release and guide tolerances',
        'Actual toggle body/shoulder elevation and included nuts/washers',
        'Seed seating/transform, provisional service positions and tool length',
        'Full component and plug models, all revision-dependent component envelopes',
        'Metal post/shim/thread tolerances; only 0.05 mm reserve above 3 mm nominal grip target',
        'Thin local seats/pocket roofs, support removal, printed strength and thermal behaviour']
    report['source_sha256'] = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in
        [Path(__file__),ROOT/'build_closed_case.py',ROOT/'closed-mechanics.json',ROOT/'board.json',ROOT/'build_case.py']}
    return report


def pdf(path):
    c = canvas.Canvas(str(path),pagesize=landscape(A4),invariant=1)
    c.setTitle('Desktop Case V3 - Einteiliger Traeger / Acryl-Passschablonen')
    for kind,label in [('lower','Untere Acrylplatte, 1 mm'),('upper','Obere Klarplatte, 1 mm')]:
        c.setFont('Helvetica-Bold',16); c.drawString(15*mm,193*mm,'V3 / '+label)
        c.setFont('Helvetica',9); c.drawString(15*mm,185*mm,'1:1 PASS-STUDIE - NICHT ZUR FERTIGUNG FREIGEGEBEN - A4 quer, 100 %, Seitenanpassung AUS')
        v2.draw_section(c,layer_section(kind),41*mm,39*mm)
        c.setFont('Helvetica',8); c.drawString(15*mm,29*mm,'Beide Platten umgehen die integrierten Hauben. Nur die untere Platte wird mit Buchsenmuttern geklemmt.')
        c.drawString(15*mm,23*mm,'Einteiliger Drucktraeger darunter: Tasterraster + Seed-Haube + Buchsenhauben; obere Klarplatte per Knurlies loesbar.')
        c.line(175*mm,13*mm,275*mm,13*mm)
        for x in [175,275]: c.line(x*mm,11*mm,x*mm,15*mm)
        c.drawCentredString(225*mm,17*mm,'Kontrollstrecke exakt 100 mm'); c.showPage()
    c.save()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'output/unified-v3')
    args=parser.parse_args()
    parts=build(); report=verify(parts)
    args.output.mkdir(parents=True,exist_ok=True)
    for name,(solid,z) in parts.items():
        mesh=b.mesh_of(solid); mesh.apply_translation((0,0,-mesh.bounds[0,2])); mesh.export(args.output/f'{name}.stl')
    for kind in ['lower','upper']:
        b.contour_svg(layer_section(kind),args.output/f'{kind}-acrylic-1mm.svg')
    b.contour_svg(layer_section('upper')-b.rectangle(*b.KEY_WINDOW),args.output/'paper-overlay.svg')
    pdf(args.output/'acrylic-fit-study-a4.pdf')
    report['artifact_sha256'] = {p.name:hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(args.output.iterdir()) if p.suffix in ['.stl','.svg','.pdf']}
    (args.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps({'parts':len(parts),'stack_mm':report['stack_mm'],'checks':report['checks']},indent=2))


if __name__=='__main__': main()
