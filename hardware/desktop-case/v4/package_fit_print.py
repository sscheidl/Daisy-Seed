"""Package a cheaper fit-print request without changing validated geometry."""
import hashlib
import json
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT=Path(__file__).resolve().parent
FULL=ROOT/'full'
OUT=ROOT/'fit-print'


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    manifest=json.loads((FULL/'manifest.json').read_text(encoding='utf8'))
    validation=json.loads((FULL/'validation.json').read_text(encoding='utf8'))
    full_package=json.loads((FULL/'print-validation.json').read_text(encoding='utf8'))
    regression=json.loads((FULL/'access-regression.json').read_text(encoding='utf8'))
    assert validation['passed'] and validation['inputs_match_export'] and full_package['passed'] and regression['passed']
    parameter_hash=sha(ROOT/'v4-parameters.json')
    assert manifest['parameters_sha256']==validation['parameters_sha256']==full_package['parameters_sha256']==regression['parameters_sha256']==parameter_hash
    assert manifest['generator_sha256']==sha(ROOT/'build_v4.py')
    assert manifest['geometry_helper_sha256']==sha(ROOT/'build_v4_coupons.py')
    assert validation['validator_sha256']==regression['validator_sha256']==sha(ROOT/'validate_v4.py')
    assert full_package['package_generator_sha256']==sha(ROOT/'package_v4.py')
    assert regression['regression_script_sha256']==sha(ROOT/'verify_access_regression.py')
    names=['tray','carrier','rear-panel','seed-service-cover']
    files={}
    for name in names:
        meta=manifest['parts'][name]; path=FULL/meta['file']
        assert sha(path)==meta['sha256']==validation['parts'][name]['sha256']==regression['corrected_stl_sha256'][name]
        files[meta['file']]=path.read_bytes()
    for folder,extension in [('plates','*.3mf'),('sheets','*.svg')]:
        selected=sorted((FULL/folder).glob(extension)); assert len(selected)==3
        for path in selected:
            name=path.relative_to(FULL).as_posix()
            assert sha(path)==full_package['contents'][name]
            files[name]=path.read_bytes()
    for name in ['validation.json','access-regression.json','manifest.json']:
        assert sha(FULL/name)==full_package['contents'][name]
        files[name]=(FULL/name).read_bytes()
    assert sha(FULL/'V4.md')==full_package['contents']['V4.md']
    files['MONTAGE_V4.md']=(FULL/'V4.md').read_bytes()
    request=(OUT/'README.md').read_text(encoding='utf8').replace('../full/V4.md','MONTAGE_V4.md')
    files['DRUCKAUFTRAG.md']=request.encode('utf8')
    report=dict(passed=True,purpose='Mechanical fit trial; no geometry changes',hardware='NICHT GEMESSEN',
        print_part_count=4,geometry_check_count=len(validation['collisions']),parameters_sha256=parameter_hash,
        requested_profile=dict(material='PLA',layer_height_mm=.2,perimeters=3,infill_percent=20,
            minimum_top_bottom_thickness_mm=.8,scale_percent=100,embedded_in_3mf=False),
        price_and_print_time='NICHT GEMESSEN; request supplier quote; 0.20 mm availability unconfirmed',
        generator_sha256=sha(Path(__file__)),request_source_sha256=sha(OUT/'README.md'),
        contents={name:hashlib.sha256(data).hexdigest() for name,data in files.items()})
    report_path=OUT/'package-validation.json'
    report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n')
    files['package-validation.json']=report_path.read_bytes()
    package=OUT/'V4-PASSPROBE-SPARPROFIL.zip'
    with ZipFile(package,'w',ZIP_DEFLATED) as archive:
        for name,data in files.items(): archive.writestr(name,data)
    with ZipFile(package) as archive:
        assert archive.testzip() is None
        assert len([name for name in archive.namelist() if name.endswith('.stl')])==4
        assert not any(name.endswith('.png') or 'reference-NOT' in name for name in archive.namelist())
        for name,data in files.items(): assert archive.read(name)==data,name
    print(json.dumps(dict(passed=True,zip=str(package),bytes=package.stat().st_size,
        print_parts=4,unchanged_geometry=True,source_geometry_checks=len(validation['collisions']))))


if __name__=='__main__': main()
