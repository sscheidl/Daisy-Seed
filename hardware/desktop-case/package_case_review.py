"""Package only the independently checked RH case export, with revisioned names."""

import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT=Path(__file__).resolve().parent
REV='RH-2026-10-25'
OUT=ROOT/'output/realistic-v3'
DEST=ROOT/'output'/f'Daisy-Desktop-V3-{REV}'


def main():
    # Both checks re-open actual files. A failed gate must stop distribution.
    for script in ['check_stl_orientation.py','validate_realistic_case.py']:
        subprocess.run([sys.executable,str(ROOT/script)],check=True,capture_output=True,text=True)
    DEST.mkdir(exist_ok=True)
    records=[]

    def add(source,relative):
        target=DEST/relative; target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
        records.append(dict(path=relative,sha256=hashlib.sha256(target.read_bytes()).hexdigest()))

    for stem in ['tray','unified-carrier']:
        add(ROOT/'unified-v3'/f'{stem}.stl',f'print-parts/{stem}-{REV}.stl')
    for stem in ['guide-clearance-coupon','H2-S9-cap-height-gauge']:
        add(ROOT/'unified-v3'/f'{stem}.stl',f'fit-tests/{stem}-{REV}.stl')
    for name in ['lower-acrylic-1mm.svg','upper-acrylic-1mm.svg','paper-overlay.svg','acrylic-fit-study-a4.pdf']:
        add(ROOT/'unified-v3'/name,'cut-contours/'+name)
    add(OUT/'desktop-case-assembly.glb',f'desktop-case-assembly-{REV}.glb')
    add(OUT/'desktop-case-realistic.blend',f'desktop-case-realistic-{REV}.blend')
    add(OUT/'case-assembly-view.stl',f'view-only/case-assembly-view-{REV}.stl')
    for name in ['studio','rear','exploded']:
        add(OUT/f'case-realistic-{name}.png',f'images/case-realistic-{name}-{REV}.png')
    add(ROOT/'output/orientation-proof'/f'orientation-proof-{REV}.png',f'orientation-proof-{REV}.png')
    for name in ['before-correction.json','after-correction.json','withdrawn-packages.json']:
        add(ROOT/'orientation-audit'/name,'checks/'+name)
    for name in ['render-manifest.json','export-validation.json','board-model-inventory.json']:
        add(OUT/name,'checks/'+name)
    text=('Daisy Desktop Case V3 / '+REV+'\n\n'
        'ALTE PAKETE VOR DEM 25.10.2026 NICHT DRUCKEN: gespiegelte Druckdateien.\n'
        'Neu: Bestueckte Oberseite; Audio hinten, Seed links, MIDI rechts.\n'
        'Nur print-parts/ enthaelt die beiden Gehaeuse-Druckteile, jeweils in mm.\n'
        'view-only/ ist nur Montageansicht, kein Druckteil.\n'
        'GLB: Meter, farbige Baugruppe. BLEND: Arbeitsmassstab 1 Einheit = 1 mm.\n'
        'cut-contours/: Acryl/Papier. fit-tests/: noch unbestaetigte Passproben.\n'
        'Orientierung: unabhaengig an tatsaechlichen Dateien geprueft.\n'
        'Hardware-Passung und Kappenhoehe: NICHT GEMESSEN.\n')
    (DEST/'README.txt').write_text(text,encoding='utf8',newline='\n')
    records.append(dict(path='README.txt',sha256=hashlib.sha256((DEST/'README.txt').read_bytes()).hexdigest()))
    manifest=dict(revision=REV,hardware='NICHT GEMESSEN',
        orientation='Verified physical RH: audio rear, Seed left, MIDI right',files=records)
    (DEST/'package-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8',newline='\n')
    archive=DEST.with_suffix('.zip')
    with ZipFile(archive,'w',ZIP_DEFLATED,compresslevel=6) as z:
        for item in records: z.write(DEST/item['path'],item['path'])
        z.write(DEST/'package-manifest.json','package-manifest.json')
    with ZipFile(archive) as z:
        assert z.testzip() is None
        for item in records:
            assert hashlib.sha256(z.read(item['path'])).hexdigest()==item['sha256'],item['path']
    print(json.dumps(dict(package=str(archive),size_mb=round(archive.stat().st_size/1e6,2),
        sha256=hashlib.sha256(archive.read_bytes()).hexdigest()),indent=2))


if __name__=='__main__': main()
