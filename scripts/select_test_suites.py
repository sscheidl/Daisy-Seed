#!/usr/bin/env python3
"""Targeted host checks, including consumers of shared DSP/control headers."""
import argparse
import os
import subprocess


def select_suites(changed):
    def touched(*prefixes):
        return any(p.startswith(prefixes) for p in changed)
    # common/audio/ (FX contract) and common/sd/ (not used by any firmware yet) have
    # their own consumer rules instead of selecting every firmware suite.
    shared = any(p in ('Makefile', 'projects.mk', 'libDaisy', 'DaisySP') or
                 (p.startswith('common/') and not p.startswith(('common/audio/', 'common/sd/')))
                 for p in changed)
    mod_api = touched('modules/mod-on-daisy/include/', 'modules/mod-on-daisy/adapters/')
    fx = touched('common/audio/', 'effects/torus-reverb/', 'effects/reverb/src/', 'tests/fx/',
                 'instruments/physical-modeling/torus/dsp/fx/',
                 'instruments/physical-modeling/third_party/stmlib/')
    suites = {
        'bootloader': shared or touched('common/bootloader/'),
        'toy': shared or touched('instruments/toy-piano/') or mod_api or fx,
        'physical': shared or touched('instruments/physical-modeling/') or mod_api or fx,
        'granular': shared or touched('instruments/granular/'),
        'looper': shared or touched('instruments/looper/'),
        'reverb': shared or touched('effects/reverb/'),
        'mod': shared or touched('modules/mod-on-daisy/'),
        'editor': shared or touched('editors/mod-on-daisy/') or mod_api,
        'diagnostics': shared or touched('diagnostics/'),
        'fx': shared or fx,
        'sd': shared or touched('common/sd/', 'scripts/check-sd-symbols.py'),
        'components': True, # tiny registry/selection tests; no compiler or dependencies
    }
    if '.github/workflows/quick.yml' in changed:
        suites = {key: True for key in suites}
    suites['submodules'] = any(suites[k] for k in ('toy', 'physical', 'granular', 'looper', 'reverb'))
    return suites


def changed_files(base, head):
    def exists(sha):
        return bool(sha) and bool(sha.strip('0')) and subprocess.run(
            ['git', 'cat-file', '-e', sha + '^{commit}'], stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL).returncode == 0
    if exists(base):
        return subprocess.check_output(['git', 'diff', '--name-only', base,
                                        head if exists(head) else 'HEAD'], text=True).splitlines()
    return ['Makefile'] # history unavailable: exercise consumers rather than skip


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default=os.environ.get('BASE_SHA', ''))
    parser.add_argument('--head', default=os.environ.get('HEAD_SHA', ''))
    args = parser.parse_args()
    files = changed_files(args.base, args.head)
    suites = select_suites(files)
    print('Changed files:', len(files))
    print('Selected suites:', ', '.join(k for k, selected in suites.items() if selected))
    lines = ''.join(f"{k}={'true' if v else 'false'}\n" for k, v in suites.items())
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
            output.write(lines)
    else:
        print(lines, end='')
