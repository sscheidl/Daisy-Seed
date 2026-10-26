#!/usr/bin/env python3
"""Targeted host checks, including consumers of shared DSP/control headers."""
import argparse
import os
import subprocess


def select_suites(changed):
    def touched(*prefixes):
        return any(p.startswith(prefixes) for p in changed)
    # common/audio/ (FX contract), common/dsp/ (shared saturation) and common/sd/ (not
    # used by any firmware yet) have their own consumer rules instead of selecting
    # every firmware suite.
    shared = any(p in ('Makefile', 'projects.mk', 'libDaisy', 'DaisySP') or
                 (p.startswith('common/') and
                  not p.startswith(('common/audio/', 'common/dsp/', 'common/sd/')))
                 for p in changed)
    mod_api = touched('modules/mod-on-daisy/include/', 'modules/mod-on-daisy/adapters/')
    fx = touched('common/audio/', 'effects/torus-reverb/', 'effects/reverb/src/', 'effects/tape-fx/src/', 'tests/fx/',
                 'instruments/physical-modeling/torus/dsp/fx/',
                 'instruments/physical-modeling/third_party/stmlib/')
    # Shared saturation curves (common/dsp/): Tape FX, its Toy Piano build and Drive.
    dsp = touched('common/dsp/')
    # Acid Seq effects: their wet-only cores and adapters run in the shared FX test.
    acid_fx = touched('effects/drive/', 'effects/stereo-delay/')
    fx_contract = touched('common/audio/', 'tests/fx/')
    # The SQ protocol contract (vectors, parameter IDs) is shared by the firmware cores,
    # the product and the editor; the reference fixtures by Drive, Delay and the product.
    sq_contract = touched('sequencers/step-seq/contracts/', 'sequencers/step-seq/docs/PROTOCOL.md')
    fixtures = touched('tests/fixtures/acid-seq/', 'tests/test_acid_seq_contracts.py')
    diagnostics_adapter = touched('modules/mod-on-daisy/adapters/')
    acid_common = shared or diagnostics_adapter or sq_contract
    stepseq = acid_common or touched('sequencers/step-seq/')
    acid303 = acid_common or touched('instruments/acid-303/')
    drive = acid_common or dsp or fx_contract or fixtures or touched('effects/drive/')
    delay = acid_common or fx_contract or fixtures or touched('effects/stereo-delay/')
    suites = {
        'bootloader': shared or touched('common/bootloader/'),
        'toy': shared or touched('instruments/toy-piano/') or mod_api or fx or dsp,
        'physical': shared or touched('instruments/physical-modeling/') or mod_api or fx,
        'granular': shared or touched('instruments/granular/'),
        'looper': shared or touched('instruments/looper/') or mod_api,
        'reverb': shared or touched('effects/reverb/'),
        'tape': shared or fx or dsp or touched('effects/tape-fx/'),
        'mod': shared or touched('modules/mod-on-daisy/'),
        'editor': shared or touched('editors/mod-on-daisy/') or mod_api or sq_contract,
        'diagnostics': shared or touched('diagnostics/'),
        'fx': shared or fx or dsp or acid_fx,
        'sd': shared or touched('common/sd/', 'scripts/check-sd-symbols.py'),
        'stepseq': stepseq,
        'acid303': acid303,
        'drive': drive,
        'delay': delay,
        # Any change to one of its cores also tests the composed product.
        'acidseq': stepseq or acid303 or drive or delay or touched('sequencers/acid-seq/'),
        'components': True, # tiny registry/selection tests; no compiler or dependencies
    }
    if '.github/workflows/quick.yml' in changed:
        suites = {key: True for key in suites}
    suites['submodules'] = any(suites[k] for k in ('toy', 'physical', 'granular', 'looper', 'reverb', 'tape',
                                                    'stepseq', 'acid303', 'drive', 'delay', 'acidseq'))
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
