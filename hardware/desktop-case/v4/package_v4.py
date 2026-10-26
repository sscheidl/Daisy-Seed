"""Package only four print parts; round-trip-check rigid 3MF print placement.

3MF core uses row-vector, row-major 4x3 transformations:
https://github.com/3MFConsortium/spec_core/blob/master/3MF%20Core%20Specification.md
No supports, machine profile or G-code are embedded.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET
from zipfile import ZipFile, ZIP_DEFLATED

import numpy as np
import trimesh

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'full'
NS='http://schemas.microsoft.com/3dmanufacturing/core/2015/02'
ET.register_namespace('',NS)
NAMES=['tray','carrier','rear-panel','seed-service-cover']


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def xml(tag,**attrs): return ET.Element('{'+NS+'}'+tag,attrs)


def write_3mf(path,items):
    model=xml('model',unit='millimeter')
    meta=xml('metadata',name='Title'); meta.text='V4 nominal full-case first print; supports required'; model.append(meta)
    resources=xml('resources'); model.append(resources)
    build=xml('build'); model.append(build)
    for index,(name,mesh,rotation,translation) in enumerate(items,1):
        obj=xml('object',id=str(index),type='model',name=name)
        geometry=xml('mesh'); vertices=xml('vertices'); triangles=xml('triangles')
        for x,y,z in mesh.vertices:
            vertices.append(xml('vertex',x=f'{x:.8f}',y=f'{y:.8f}',z=f'{z:.8f}'))
        for a,b,c in mesh.faces:
            triangles.append(xml('triangle',v1=str(a),v2=str(b),v3=str(c)))
        geometry.extend([vertices,triangles]); obj.append(geometry); resources.append(obj)
        transform=np.r_[rotation.T.ravel(),translation]
        build.append(xml('item',objectid=str(index),transform=' '.join(f'{v:.10f}' for v in transform)))
    with ZipFile(path,'w',ZIP_DEFLATED) as archive:
        archive.writestr('[Content_Types].xml','<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
        archive.writestr('_rels/.rels','<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
        archive.writestr('3D/3dmodel.model',ET.tostring(model,encoding='utf-8',xml_declaration=True))


def read_check_3mf(path,expected):
    # Read the archive independently, apply the standard row-vector transform,
    # then compare every vertex and face with the original STL in its print pose.
    with ZipFile(path) as archive:
        root=ET.fromstring(archive.read('3D/3dmodel.model'))
        assert archive.testzip() is None
    assert root.attrib['unit']=='millimeter'
    ns={'m':NS}; objects={o.attrib['id']:o for o in root.findall('m:resources/m:object',ns)}
    items=root.findall('m:build/m:item',ns); assert len(items)==len(expected)==len(objects)
    checks=[]; bounds=[]
    for item,(name,original,rotation,translation) in zip(items,expected):
        obj=objects[item.attrib['objectid']]; assert obj.attrib['name']==name
        vertices=np.array([[float(v.attrib[k]) for k in ['x','y','z']] for v in obj.findall('m:mesh/m:vertices/m:vertex',ns)])
        faces=np.array([[int(v.attrib[k]) for k in ['v1','v2','v3']] for v in obj.findall('m:mesh/m:triangles/m:triangle',ns)])
        transform=np.array([float(s) for s in item.attrib['transform'].split()]).reshape(4,3)
        determinant=float(np.linalg.det(transform[:3])); assert abs(determinant-1)<1e-8
        actual=vertices@transform[:3]+transform[3]
        wanted=original.vertices@rotation.T+translation
        error=float(np.max(np.abs(actual-wanted)))
        assert error<1e-6 and np.array_equal(faces,original.faces)
        mesh=trimesh.Trimesh(actual,faces,process=False)
        assert mesh.is_watertight and mesh.is_winding_consistent and mesh.volume>0
        assert abs(mesh.bounds[0,2])<1e-6 and np.min(mesh.bounds[:,:2])>=0
        bounds.append(mesh.bounds)
        checks.append(dict(name=name,proper_rotation=True,determinant=determinant,
            maximum_vertex_error_mm=error,bounds_mm=mesh.bounds.round(6).tolist(),closed=True,outward=True))
    for i,a in enumerate(bounds):
        for b in bounds[i+1:]:
            assert np.any(np.minimum(a[1,:2],b[1,:2])-np.maximum(a[0,:2],b[0,:2])<0),'Overlapping print placements'
    return dict(file=path.relative_to(OUT).as_posix(),sha256=sha(path),passed=True,parts=checks)


def main():
    p=json.loads((ROOT/'v4-parameters.json').read_text(encoding='utf8'))
    manifest=json.loads((OUT/'manifest.json').read_text(encoding='utf8'))
    validation=json.loads((OUT/'validation.json').read_text(encoding='utf8'))
    assert validation['passed'] and validation['inputs_match_export']
    assert manifest['parameters_sha256']==validation['parameters_sha256']==sha(ROOT/'v4-parameters.json')
    assert manifest['generator_sha256']==sha(ROOT/'build_v4.py')
    assert manifest['geometry_helper_sha256']==sha(ROOT/'build_v4_coupons.py')
    assert validation['validator_sha256']==sha(ROOT/'validate_v4.py')
    for name,meta in manifest['parts'].items():
        assert sha(OUT/meta['file'])==meta['sha256']==validation['parts'][name]['sha256']
    for name,meta in validation['svg'].items():
        assert sha(OUT/'sheets'/(name+'.svg'))==meta['sha256']
    margin=p['case']['print_bed_margin']['value']; gap=p['case']['print_plate_spacing']['value']
    identity=np.eye(3); rear_rotation=np.array([[1,0,0],[0,0,1],[0,-1,0.]])
    items={}
    cursor=margin
    for name in NAMES:
        mesh=trimesh.load(OUT/manifest['parts'][name]['file'],force='mesh')
        rotation=rear_rotation if name=='rear-panel' else identity
        rotated=mesh.vertices@rotation.T
        translation=-rotated.min(axis=0)+[cursor if name=='seed-service-cover' else margin,margin,0]
        items[name]=(name,mesh,rotation,translation)
        if name=='rear-panel': cursor=margin+np.ptp(rotated[:,0])+gap
    plates=OUT/'plates'; plates.mkdir(exist_ok=True)
    layouts=[('plate-1-tray.3mf',['tray']),('plate-2-carrier.3mf',['carrier']),
        ('plate-3-rear-and-service.3mf',['rear-panel','seed-service-cover'])]
    checks=[]
    for filename,names in layouts:
        expected=[items[name] for name in names]; path=plates/filename
        write_3mf(path,expected); checks.append(read_check_3mf(path,expected))
    files=[OUT/manifest['parts'][name]['file'] for name in NAMES]
    files += sorted((OUT/'sheets').glob('*.svg'))
    assert len(files)==7
    files += [plates/name for name,_ in layouts]
    files += [OUT/'V4.md',OUT/'manifest.json',OUT/'validation.json']
    # Images are explanatory and contain no extra printable geometry.
    files += sorted((OUT/'images').glob('*.png'))
    report=dict(generated_at_utc=datetime.now(timezone.utc).isoformat(),passed=True,
        hardware='NICHT GEMESSEN',print_part_count=4,acrylic_count=2,paper_count=1,
        plates=checks,parameters_sha256=sha(ROOT/'v4-parameters.json'),
        package_generator_sha256=sha(Path(__file__)),
        contents={f.relative_to(OUT).as_posix():sha(f) for f in files})
    report_path=OUT/'print-validation.json'
    report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n')
    files.append(report_path)
    package=OUT/'V4-KOMPLETTDRUCK.zip'
    with ZipFile(package,'w',ZIP_DEFLATED) as archive:
        for f in files: archive.write(f,f.relative_to(OUT).as_posix())
    with ZipFile(package) as archive:
        assert archive.testzip() is None
        assert sorted(n for n in archive.namelist() if n.endswith('.stl'))==sorted('print/'+n+'.stl' for n in NAMES)
        assert len([n for n in archive.namelist() if n.endswith('.svg')])==3
        assert len([n for n in archive.namelist() if n.endswith('.3mf')])==3
        for f in files: assert archive.read(f.relative_to(OUT).as_posix())==f.read_bytes()
    print(json.dumps(dict(passed=True,print_parts=4,plates=3,zip=str(package),bytes=package.stat().st_size)))


if __name__=='__main__': main()
