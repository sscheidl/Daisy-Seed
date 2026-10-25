#!/usr/bin/env python3
"""Resolve owned version sources and describe a reproducible firmware composition."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def read_version(spec, root=ROOT):
    text = (root / spec['path']).read_text(encoding='utf-8')
    kind = spec['type']
    if kind == 'constants':
        parts = []
        for name in spec['names']:
            match = re.search(r'\b' + re.escape(name) + r'\s*=\s*(\d+)', text)
            if not match:
                raise ValueError(f"Missing version constant {name}")
            parts.append(match.group(1))
        value = '.'.join(parts)
    else:
        if kind == 'macro':
            pattern = r'^#define\s+' + re.escape(spec['name']) + r'\s+"([^"]+)"'
        elif kind == 'cmake':
            pattern = r'project\([^\s]+\s+VERSION\s+([^\s)]+)'
        elif kind == 'javascript':
            pattern = r'\b' + re.escape(spec['name']) + r"\s*=\s*['\"]([^'\"]+)['\"]"
        else:
            raise ValueError(f'Unknown version source {kind}')
        match = re.search(pattern, text, re.MULTILINE)
        if not match:
            raise ValueError(f"Missing version in {spec['path']}")
        value = match.group(1)
    # Legacy 0.3/0.2 values are retained; new components use three-part SemVer.
    if not re.fullmatch(r'\d+\.\d+(?:\.\d+)?(?:-[0-9A-Za-z.-]+)?', value):
        raise ValueError(f'Invalid version {value}')
    return value


def load_registry(root=ROOT):
    data = json.loads((root / 'components/registry.json').read_text())
    if data.get('format_version') != 1:
        raise ValueError('Unsupported component registry version')
    components = data['components']
    for key, item in components.items():
        if not re.fullmatch(r'[a-z][a-z0-9-]*', key):
            raise ValueError(f'Invalid component ID {key}')
        if item['kind'] not in ('instrument', 'effect', 'control', 'panel', 'contract'):
            raise ValueError(f'Invalid kind for {key}')
        if not (root / item['source']).exists():
            raise ValueError(f'Missing component source {key}')
        read_version(item['version'], root)
    def visit(key, chain):
        if key not in components:
            raise ValueError(f'Unknown component {key}')
        if key in chain:
            raise ValueError(f'Dependency cycle at {key}')
        for dep in components[key].get('depends', []):
            visit(dep, chain + (key,))
    for key in components:
        visit(key, ())
    for key, item in data['products'].items():
        source = item.get('version') or components[item['version_component']]['version']
        read_version(source, root)
        for component in item['components']:
            visit(component, ())
            if components[component]['kind'] == 'panel':
                raise ValueError(f'Panel is not firmware DSP: {key}')
        for panel in item.get('panels', []):
            visit(panel, ())
            if components[panel]['kind'] != 'panel':
                raise ValueError(f'Not a panel: {panel}')
    return data


def describe_product(project, image=None, root=ROOT):
    data = load_registry(root)
    if project not in data['products']:
        raise ValueError(f'Unknown product {project}')
    product = data['products'][project]
    source = product.get('version') or data['components'][product['version_component']]['version']
    version = read_version(source, root)
    def component(key):
        item = data['components'][key]
        return {'id': key, 'kind': item['kind'], 'version': read_version(item['version'], root),
                'version_source': item['version']['path'], 'depends': item.get('depends', [])}
    ordered = []
    def include(key):
        for dependency in data['components'][key].get('depends', []):
            include(dependency)
        if key not in ordered:
            ordered.append(key)
    for key in product['components']:
        include(key)
    result = {
        'manifest_version': 1,
        'product': {'id': project, 'version': version,
                    'legacy_device_version': product.get('legacy_device_version', False)},
        'components': [component(key) for key in ordered],
        'compatible_control_panels': [component(key) for key in product.get('panels', [])],
    }
    result['repository_commit'] = subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    result['working_tree_dirty'] = bool(subprocess.check_output(
        ['git', 'status', '--porcelain', '--untracked-files=normal'], cwd=root, text=True).strip())
    if image is not None:
        image = Path(image)
        expected = f"{product['image']}-v{version}.bin"
        if image.name != expected:
            raise ValueError(f'Expected image {expected}, got {image.name}')
        content = image.read_bytes()
        if content.count((product['marker'] + version + '\0').encode('ascii')) != 1:
            raise ValueError('Firmware identity does not match the product version')
        result['image'] = {'name': image.name, 'bytes': len(content),
                           'sha256': hashlib.sha256(content).hexdigest()}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validate', action='store_true')
    parser.add_argument('--project')
    parser.add_argument('--image', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--built', action='store_true', help='check the built BIN and write COMPONENTS.json next to it')
    args = parser.parse_args()
    try:
        data = load_registry()
        if args.project:
            if args.built:
                product = data['products'][args.project]
                source = product.get('version') or data['components'][product['version_component']]['version']
                args.image = ROOT / product['directory'] / f"{product['image']}-v{read_version(source)}.bin"
                if args.output is None:
                    args.output = args.image.parent / 'COMPONENTS.json'
            result = describe_product(args.project, args.image)
        elif args.validate:
            print(f"PASS: {len(data['components'])} components, {len(data['products'])} firmware products")
            return
        else:
            result = {key: read_version(item['version']) for key, item in data['components'].items()}
        text = json.dumps(result, indent=2, ensure_ascii=False) + '\n'
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(text, encoding='utf-8')
        else:
            print(text, end='')
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f'Component manifest: {error}\n')


if __name__ == '__main__':
    main()
