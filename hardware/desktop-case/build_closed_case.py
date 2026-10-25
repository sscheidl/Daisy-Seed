"""Closed V2 construction study: metal posts, two acrylic layers, key guides.

Nominal assembly dimensions and unresolved fit conditions are kept separate.
No complete component model is available for collision/force certification.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

import manifold3d as m
from reportlab.lib.pagesizes import A4, landscape
from reportlab.lib.units import mm
from reportlab.pdfgen import canvas

import build_case as b

ROOT = Path(__file__).resolve().parent
C = json.loads((ROOT/'closed-mechanics.json').read_text())
PCB_BOTTOM = b.FLOOR + C['lower_metal_post']
PCB_TOP = PCB_BOTTOM + b.BOARD['thickness']
LOWER_Z = PCB_TOP + C['plate_above_pcb']
UPPER_Z = LOWER_Z + C['lower_acrylic'] + C['paper']
GUIDE_Z = PCB_TOP + C['guide_base_above_pcb']
ROOF_Z = PCB_TOP + 23
SERVICE_Z = PCB_TOP + C['service_roof_above_pcb']
SHELL_T = 2.4
GUIDE_SCREWS = [(60, 78), (164, 78), (60, 105), (164, 105)]
REAR_SCREWS = [(24,43),(169,43)]
MIDI_SCREWS = [(181,85),(202,85)]
HOOD_SCREWS = REAR_SCREWS + MIDI_SCREWS
SERVICE_SCREWS = [(4, 61), (49, 61), (4, 88), (49, 88)]


def circle(x, y, d):
    return m.CrossSection.circle(d/2, 64).translate((x, y))


def extrude(section, z, height):
    return m.Manifold.extrude(section, height).translate((0, 0, z))


def rear_outline():
    return b.rectangle(20,-9,153.5,53) - b.rectangle(74,6,55,39)


def midi_outline():
    return b.rectangle(178.5,7.5,27.5,78.5)


def hood_outline():
    # The 5-mm channel between covers preserves a continuous acrylic bridge to H3.
    # An outside bridge at deck height would obstruct the horizontal MIDI plugs.
    return rear_outline() + midi_outline()


def service_outline():
    shape = b.rectangle(-9,63.5,65.5,24.5)
    p=b.BOARD['pots'][0]
    return shape-circle(p['x'],p['y'],18)


def guide_section(style='mixed'):
    section = b.rectangle(57, 75, 110, 33)
    for p in b.BOARD['buttons']:
        square = style == 'square' or (style == 'mixed' and int(p['ref'][1:]) <= 8)
        aperture = (b.rectangle(p['x']-C['cap_aperture']/2, p['y']-C['cap_aperture']/2,
                                C['cap_aperture'], C['cap_aperture']) if square else
                    circle(p['x'], p['y'], C['cap_aperture']))
        section -= aperture
    for p in b.BOARD['leds']:
        section -= circle(p['x'], p['y'], 3.6)
    for x, y in GUIDE_SCREWS:
        section -= circle(x, y, 2.4)
    return section


def layer_section(layer):
    section = b.rounded_rectangle(-9, -9, 215, 129, 3)
    # The fixed lower plate passes around connector bodies under the hoods.
    if layer == 'lower':
        for x in b.AUDIO_AXES:
            section -= b.rectangle(x-10,-10,20,39)
        section -= b.rectangle(178,9,30,74)
        section -= b.rectangle(-10,64,63.5,21)
    else:
        section -= hood_outline().offset(.35)
        section -= service_outline().offset(.35)
    for p in b.BOARD['pots']:
        section -= circle(p['x'], p['y'], C['pot_opening'])
    for p in b.BOARD['buttons']:
        # A single generous shape in the acrylic accepts both guide variants.
        section -= b.rectangle(p['x']-3.2, p['y']-3.2, 6.4, 6.4)
    for p in C['jacks']:
        section -= circle(p['x'], p['y'], C['jack_lower_hole'] if layer == 'lower'
                          else C['jack_upper_hole'])
    if layer != 'lower':
        # Join adjacent nut openings to avoid a fragile 0.9-mm acrylic web.
        for y in [15.8,31.4]:
            section -= b.rectangle(112.3,y-C['jack_upper_hole']/2,10.1,C['jack_upper_hole'])
    for p in b.BOARD['toggles']:
        section -= circle(p['x'], p['y'], C['toggle_lower_hole'] if layer == 'lower'
                          else C['toggle_upper_hole'])
    section -= circle(191.325, 91.6, 16)  # Headphone level wheel.
    for p in b.BOARD['mounts']:
        section -= circle(p['x'], p['y'], 3.4)
    for x, y in GUIDE_SCREWS:
        section -= circle(x, y, 2.4 if layer == 'lower' else 5.0)
    for x, y in HOOD_SCREWS + SERVICE_SCREWS:
        section -= circle(x, y, 2.4 if layer == 'lower' else 7.0)
    return section


def port_cuts(zmax=45):
    cuts = []
    for x in b.AUDIO_AXES:
        # Rear sockets; only an access aperture, no assumed ferrule clamping.
        cuts.append(m.Manifold.cylinder(15, 6.8, circular_segments=64)
                    .rotate((90, 0, 0)).translate((x, 2, PCB_TOP+8.9)))
    for y in [20.3, 44.3, 68.3]:
        cuts.append(m.Manifold.cylinder(16, 10.5, circular_segments=64)
                    .rotate((0, 90, 0)).translate((193, y, PCB_TOP+10)))
    cuts += [b.box(-11, 33.5, PCB_TOP-.5, 15, 15.2, 8),
             b.box(-11, 67.5, PCB_TOP+5, 15, 15, 9),
             b.box(-11, 83, PCB_TOP-1, 15, 22, 7),
             b.box(167, 108, PCB_TOP-.5, 14, 14, 9)]
    return b.union(cuts)


def build():
    outer = b.rounded_rectangle(-9, -9, 215, 129, 3)
    inner = b.rounded_rectangle(-6.6, -6.6, 210.2, 124.2, 2)
    tray = extrude(outer, 0, LOWER_Z) - extrude(inner, b.FLOOR, LOWER_Z)
    # Floor seats locate separate metal lower spacers; no printed load-bearing posts.
    for p in b.BOARD['mounts']:
        tray -= b.cylinder(p['x'], p['y'], -1, 1.7, b.FLOOR+2)
        tray -= b.cylinder(p['x'], p['y'], -.1, 3.2, 1.5)
    for x, y, w, d in b.tie_slots():
        tray -= b.box(x, y, -1, w, d, b.FLOOR+2)
    tray -= port_cuts()

    hood = extrude(rear_outline(),LOWER_Z+1,ROOF_Z-LOWER_Z+SHELL_T-1)
    hood -= extrude(b.rectangle(22.4,-6.6,150.1,48.2),LOWER_Z+.9,ROOF_Z-LOWER_Z-.9)
    # Thin inner collar clears the adjacent jack housings in plan view.
    collar = b.rectangle(73,5,57,39)-b.rectangle(74,6,55,40)
    hood += extrude(collar,LOWER_Z+1,ROOF_Z-LOWER_Z+SHELL_T-1)
    hood -= port_cuts()
    midi = extrude(midi_outline(),LOWER_Z+1,ROOF_Z-LOWER_Z+SHELL_T-1)
    midi -= extrude(b.rectangle(179.5,9.9,24.1,73.7),LOWER_Z+.9,ROOF_Z-LOWER_Z-.9)
    midi -= port_cuts()
    for solid_name, screws in [('rear',REAR_SCREWS),('midi',MIDI_SCREWS)]:
        solid = hood if solid_name=='rear' else midi
        for x,y in screws:
            solid += b.cylinder(x,y,LOWER_Z+1,3,3)
            solid -= b.cylinder(x,y,LOWER_Z,.8,6)
        if solid_name=='rear': hood=solid
        else: midi=solid
    # Slots in the raised roof; open area is a design feature, cooling is untested.
    for x in range(32, 69, 6):
        hood -= b.box(x, 32, ROOF_Z-1, 2.2, 8, 5)

    service = extrude(service_outline(), LOWER_Z+1, SERVICE_Z-LOWER_Z+SHELL_T-1)
    # The front wall is 1 mm so it clears the module edge and the first pot aperture.
    service -= extrude(b.rectangle(-6.6,64.5,60.7,21.1), LOWER_Z+.9, SERVICE_Z-LOWER_Z-.9)
    service -= port_cuts()
    for x, y in SERVICE_SCREWS:
        service += b.cylinder(x, y, LOWER_Z+1, 3, 3)
        service -= b.cylinder(x, y, LOWER_Z, .8, 6)
    for p in C['service_positions_provisional']:
        # Sleeves accept a removable nonconductive service tool: no permanent switch preload.
        service += b.cylinder(p['x'], p['y'], SERVICE_Z-3.5, 3.5, 3.5)
        service -= b.cylinder(p['x'], p['y'], SERVICE_Z-3.6, 1.5, 8)
    for x in range(27, 49, 5):
        service -= b.box(x, 65, SERVICE_Z-1, 2, 16, 5)

    parts = {'closed-tray': (tray, 0), 'rear-hood': (hood, LOWER_Z+1), 'midi-hood':(midi,LOWER_Z+1),
             'seed-hood': (service, LOWER_Z+1),
             'lower-acrylic-reference': (extrude(layer_section('lower'), 0, C['lower_acrylic']), LOWER_Z),
             'upper-acrylic-reference': (extrude(layer_section('upper'), 0, C['upper_acrylic']), UPPER_Z)}
    for style in ['mixed', 'square', 'round']:
        guide = extrude(guide_section(style), 0, C['guide_height'])
        for x,y in GUIDE_SCREWS:
            guide -= b.cylinder(x,y,-.1,4.2/math.sqrt(3),1.7,6)
        parts[f'key-guide-{style}'] = (guide, GUIDE_Z)
    # Loose tool, removed after use. Final length requires the assembled module height.
    tool_length = C['service_roof_above_pcb']+SHELL_T-C['seed_module_pcb_top_assumed']-C['seed_switch_top_above_module_assumed']+.25
    rod = b.cylinder(0, 0, 0, 1.2, tool_length) + b.cylinder(0, 0, tool_length, 3, 2)
    parts['removable-service-tool-fit-study'] = (rod, 0)
    # A single-position guide/cover coupon is checked on an existing switch, not installed as a new key.
    coupon = b.box(0, 0, 0, 42, 18, 3)
    for x, d in [(7, 5.4), (20, 5.8), (33, 6.2)]:
        coupon -= b.box(x-d/2, 4-d/2, -1, d, d, 5)
        coupon -= b.cylinder(x, 12, -1, d/2, 5)
    parts['guide-clearance-coupon'] = (coupon, 0)
    # A specific H2-to-S9 bridge references a known mounting pad, not arbitrary components.
    h = C['plate_above_pcb']+C['lower_acrylic']+C['paper']+C['upper_acrylic']
    gauge = b.cylinder(5,106,0,3.5,h) + b.box(5,104,h-1,70,5,1)
    gauge += b.box(63,96.5,h-1,12,12.5,1)
    gauge -= b.box(65.8,97.8,h-1.1,6.4,6.4,2)
    gauge -= b.cylinder(5,106,-1,1.7,h+2)
    parts['H2-S9-cap-height-gauge'] = (gauge, 0)
    paper = layer_section('upper') - b.rectangle(*b.KEY_WINDOW)
    parts['paper-reference'] = (extrude(paper,0,C['paper']), LOWER_Z+C['lower_acrylic'])
    return parts


def verify(parts):
    result = {'date': '2026-10-25', 'hardware': 'NICHT GEMESSEN','stl_frame':b.STL_FRAME,
              'manufacturing_ready': False, 'parts': {}}
    for name, (solid, z) in parts.items():
        local = solid.translate((0, 0, -min(v[2] for v in b.mesh_of(solid).vertices)))
        mesh = b.mesh_of(local)
        assert mesh.is_watertight and mesh.is_winding_consistent, name
        assert len(solid.decompose()) == 1, (name, len(solid.decompose()))
        result['parts'][name] = dict(size_mm=mesh.extents.round(4).tolist(), closed=True,
                                    components=1, installed_z=z)
    for kind in ['lower', 'upper']:
        section = layer_section(kind)
        for p in b.BOARD['buttons']:
            assert (section ^ b.rectangle(p['x']-2.8, p['y']-2.8, 5.6, 5.6)).area() < 1e-5
    installed = {name: solid if name in ['closed-tray','rear-hood','midi-hood','seed-hood']
                 else solid.translate((0, 0, z)) for name,(solid,z) in parts.items()}
    pairs = [('closed-tray','lower-acrylic-reference'), ('rear-hood','lower-acrylic-reference'),
                 ('rear-hood','upper-acrylic-reference'),('midi-hood','lower-acrylic-reference'),
                 ('midi-hood','upper-acrylic-reference'), ('seed-hood','upper-acrylic-reference'),
                 ('key-guide-mixed','lower-acrylic-reference')]
    for a,d in pairs:
        overlap = (installed[a] ^ installed[d]).volume()
        assert overlap < 1e-4, (a,d,overlap)
    # Swept connector access volumes must also clear both low acrylic layers.
    for name in ['lower-acrylic-reference','upper-acrylic-reference']:
        overlap=(installed[name]^port_cuts()).volume()
        assert overlap<1e-4,(name,'connector access blocked',overlap)
    post_engagement = C['knurlie_usable_min'] - C['lower_acrylic'] - C['paper'] - C['upper_acrylic']
    assert 3 <= post_engagement <= C['post_female_thread_min']
    lower_top_screw = C['upper_post_male_stud'] - b.BOARD['thickness']
    lower_floor_screw = C['floor_screw_length'] - b.FLOOR + C['floor_head_recess']
    screw_gap = C['lower_metal_post'] - lower_top_screw - lower_floor_screw
    assert screw_gap >= .5, 'Opposing screws overlap inside lower metal spacer'
    result['stack_mm'] = dict(pcb_top=PCB_TOP, lower_bottom=LOWER_Z,
                            lower_top=LOWER_Z+C['lower_acrylic'], upper_bottom=UPPER_Z,
                            upper_top=UPPER_Z+C['upper_acrylic'], metal_upper_post=C['plate_above_pcb'],
                            knurlie_min_engagement=round(post_engagement,3),
                            lower_spacer_internal_screw_gap=round(screw_gap,3),
                            jack_thread_remaining=C['jack_thread_tip_nominal']-C['plate_above_pcb']-C['lower_acrylic'],
                            cap_rest_assumption=C['cap_rest_height_assumed'],
                            cap_pressed_exposure_assumption=round(C['cap_rest_height_assumed']-C['cap_travel_nominal']-C['plate_above_pcb']-C['lower_acrylic']-C['paper']-C['upper_acrylic'],3))
    result['unresolved'] = [
        'Actual cap seating height, variation, release clearance and guide friction',
        'Actual toggle shoulder elevation and included nut/washer stack',
        'Seed header seating height, module transform and service actuator stroke/gap',
        'Connector housing/plug and complete component Z collisions',
        'Fastener fit, acrylic tolerances, structural forces and thermal behaviour']
    result['checks'] = ['All exported parts: closed, consistent winding, one connected component',
                        'Both acrylics clear all 16 cap envelopes', f'{len(pairs)} named assembly collision pairs clear',
                        'Both acrylics clear nominal connector access sweeps',
                        'Conservative Knurlie thread budget 3.85 mm']
    result['source_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in [Path(__file__),ROOT/'closed-mechanics.json',ROOT/'board.json',ROOT/'build_case.py']}
    return result


def svg(section, path):
    b.contour_svg(section, path)


def draw_section(c, section, ox, oy):
    c.setLineWidth(.12*mm)
    for contour in section.to_polygons():
        path = c.beginPath()
        for i, (x,y) in enumerate(contour):
            (path.moveTo if i == 0 else path.lineTo)(ox+(x+9)*mm, oy+(120-y)*mm)
        path.close()
        c.drawPath(path)


def pdf(out, report):
    c = canvas.Canvas(str(out), pagesize=landscape(A4), invariant=1)
    c.setTitle('Desktop Case V2 - Sandwich und Hoehenstudie')
    for kind, title in [('lower','Untere Traegerplatte, 1 mm'),('upper','Obere Klarplatte, 1 mm')]:
        c.setFont('Helvetica-Bold',16); c.drawString(15*mm,193*mm,'Desktop Case V2 / '+title)
        c.setFont('Helvetica',9); c.drawString(15*mm,185*mm,'1:1 PASS-STUDIE - NICHT ZUR FERTIGUNG FREIGEGEBEN - A4 quer, 100 %, Seitenanpassung AUS')
        draw_section(c, layer_section(kind), 41*mm, 39*mm)
        c.setFont('Helvetica',8)
        c.drawString(15*mm,29*mm, 'Vier M3-Knurlies ueber den PCB-Bohrungen; Metallpfosten tragen das Plattenpaket.')
        c.drawString(15*mm,23*mm, 'Untere Platte: Buchsen-/Schaltermuttern. Obere Platte: Freistellungen um Muttern und feste Hauben.')
        c.line(175*mm,13*mm,275*mm,13*mm)
        for x in [175,275]: c.line(x*mm,11*mm,x*mm,15*mm)
        c.drawCentredString(225*mm,17*mm,'Kontrollstrecke exakt 100 mm')
        b.orientation_footer(c)
        c.showPage()
    c.setFont('Helvetica-Bold',19); c.drawString(17*mm,190*mm,'V2 / Metallpfosten und Hoehenbudget')
    c.setFont('Helvetica',10)
    lines = [
        'Alle Z-Masse relativ zur Oberseite der grossen Desktop-Platine. Hardware: NICHT GEMESSEN.',
        '', 'Nominaler Aufbau: 9,00 mm Metallpfosten + 1,00 mm Acryl + 0,15 mm Papier + 1,00 mm Acryl.',
        'Oberseite Klarplatte: 11,15 mm ueber PCB. Alternative fuer 9-mm-Pfosten: 8 mm + 1 mm feste Distanzscheibe.',
        'Knurlies M3: konservativ 6,00 mm nutzbar; 2,15 mm Plattenpaket -> 3,85 mm Gewindeeingriff.',
        'Unten 10-mm-Metallabstandshalter; oben M3-Metallpfosten durch das Board in die unteren Abstandshalter.',
        '', 'QingPu-Buchsen: nominale Auflage 9 mm; untere Platte 1 mm -> 3,5 mm bis Gewindeende.',
        'Nur die untere Platte wird mit den Buchsenmuttern befestigt. Die obere umgeht Muttern mit groesseren Loechern.',
        'Dailywell-Schalter: 10-48 UNS, kein M5. B1-Huelse 5,59 mm. Einbauhoehe und Mutterpaket noch pruefen.',
        '', 'Tastenkappen: 9 mm Einzelteilhoehe ist NICHT die montierte Hoehe. Im CAD zunaechst 13 mm angenommen.',
        'Nominaler Hub 0,25 mm -> unter dieser Annahme 1,60 mm Ueberstand im gedrueckten Zustand.',
        'Fuer mindestens 0,8 mm Ueberstand muss die niedrigste montierte Kappe mindestens 12,20 mm erreichen.',
        'Raster: 3 mm hoch; Unterseite 6 mm ueber PCB. Austauschbare Varianten rund / quadratisch / gemischt.',
        'Kein Anschlag auf dem Tastergehaeuse; keine Vorspannung. Leichtgaengigkeit und Rueckkehr pruefen.',
        '', 'BOOT/RESET: geschlossene Seed-Haube mit Fuehrungen fuer herausnehmbare, isolierende Druckstifte.',
        'Stift nach Gebrauch entfernen. Modulhoehe, Ausrichtung und Anschlaglaenge vor Benutzung pruefen.',
        '', 'Hoehenlehre: Auflage auf dem H2-Montagepad; Pruefoeffnung ueber S9, Ziel-Oberseite 11,15 mm ueber PCB.',
        'Nur am stromlosen Board, spannungsfrei auflegen und zuvor freie Auflage pruefen. Druckstuetzen erforderlich.',
        '', 'Vor Druckauftrag: Kappen und Gewindebudget am echten Board pruefen. Kein Ergebnis ist als GEMESSEN eingetragen.'
    ]
    for i, line in enumerate(lines): c.drawString(17*mm,(179-i*6)*mm,line)
    b.orientation_footer(c)
    c.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'output/closed-v2')
    args = parser.parse_args()
    parts = build(); report = verify(parts)
    args.output.mkdir(parents=True,exist_ok=True)
    for name,(solid,z) in parts.items():
        # STL coordinates are always local to the print bed; installed Z is in validation.json.
        mesh = b.physical_mesh_of(solid); mesh.apply_translation((0,0,-mesh.bounds[0,2]))
        mesh.export(args.output/f'{name}.stl')
    svg(layer_section('lower'), args.output/'lower-acrylic-1mm.svg')
    svg(layer_section('upper'), args.output/'upper-acrylic-1mm.svg')
    # In paper only, the LEDs get holes; both acrylic layers remain clear above them.
    paper = layer_section('upper') - b.rectangle(*b.KEY_WINDOW)
    svg(paper,args.output/'paper-overlay.svg')
    pdf(args.output/'sandwich-and-height-study-a4.pdf',report)
    report['artifact_sha256'] = {p.name:hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in sorted(args.output.iterdir()) if p.suffix in ['.stl','.svg','.pdf']}
    (args.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps({'parts':len(parts),'stack_mm':report['stack_mm'],'manufacturing_ready':False},indent=2))


if __name__=='__main__': main()
