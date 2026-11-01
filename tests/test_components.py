import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


manifest = load('component_manifest')
selection = load('select_test_suites')


def copy_version_sources(data, root):
    # Minimal checkout for registry validation: sources plus every version source.
    for item in data['components'].values():
        version = Path(item['version']['path'])
        if Path(item['source']) != version:
            (root / item['source']).mkdir(parents=True, exist_ok=True)
        (root / version).parent.mkdir(parents=True, exist_ok=True)
        (root / version).write_bytes((ROOT / version).read_bytes())
    for item in data['products'].values():
        if 'version' in item:
            source = Path(item['version']['path'])
            (root / source).parent.mkdir(parents=True, exist_ok=True)
            (root / source).write_bytes((ROOT / source).read_bytes())
    registry = root / 'components/registry.json'
    registry.parent.mkdir(exist_ok=True)
    return registry


ACID_SEQ_SUITES = ('stepseq', 'acid303', 'drive', 'delay', 'acidseq')


class ComponentTests(unittest.TestCase):
    def test_versions_and_project_coverage(self):
        data = manifest.load_registry()
        mk = (ROOT / 'projects.mk').read_text()
        buildable = []
        for line in mk.splitlines():
            match = re.match(r'BUILDABLE_PROJECTS\s*(?::=|\+=)\s*(.*)', line)
            if match:
                buildable.extend(match.group(1).split())
        self.assertEqual(set(buildable), set(data['products']))
        self.assertEqual(manifest.read_version(data['components']['mod-on-daisy']['version']), '0.1.0')
        self.assertEqual(manifest.read_version(data['components']['mod-editor-web']['version']), '0.3.0')

    def test_composition_and_image_identity(self):
        for effect in ('torus', 'ambient'):
            project = 'physical-modeling-fx-' + effect
            data = manifest.load_registry()['products'][project]
            version = manifest.read_version(data['version'])
            with tempfile.TemporaryDirectory() as folder:
                image = Path(folder) / f"{data['image']}-v{version}.bin"
                image.write_bytes(b'fixture\0' + (data['marker'] + version + '\0').encode())
                result = manifest.describe_product(project, image)
                ids = [item['id'] for item in result['components']]
                self.assertIn('torus-reverb' if effect == 'torus' else 'ambient-reverb', ids)
                self.assertIn('mod-on-daisy', ids)
                self.assertFalse(result['product']['legacy_device_version'])
                self.assertTrue(all(item['kind'] != 'panel' for item in result['components']))
                self.assertEqual(len(result['compatible_control_panels']), 3)
                image.write_bytes(b'incorrect firmware identity')
                with self.assertRaisesRegex(ValueError, 'identity'):
                    manifest.describe_product(project, image)
        with self.assertRaisesRegex(ValueError, 'Unknown product'):
            manifest.describe_product('does-not-exist')

    def test_unknown_dependency_and_cycle(self):
        data = json.loads((ROOT / 'components/registry.json').read_text())
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            registry = copy_version_sources(data, root)
            data['components']['mod-on-daisy']['depends'] = ['missing']
            registry.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, 'Unknown component'):
                manifest.load_registry(root)
            data['components']['mod-on-daisy']['depends'] = ['mod-editor-juce']
            registry.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, 'cycle'):
                manifest.load_registry(root)

    def test_shared_control_consumers(self):
        suites = selection.select_suites(['modules/mod-on-daisy/include/mod_on_daisy/engine.h'])
        for key in ('toy', 'physical', 'looper', 'mod', 'editor', 'submodules'):
            self.assertTrue(suites[key], key)
        self.assertFalse(suites['granular'])

    def test_fx_consumers(self):
        suites = selection.select_suites(['common/audio/fx_contract.h'])
        self.assertTrue(suites['fx'] and suites['physical'])
        self.assertTrue(suites['toy'])
        self.assertFalse(suites['looper'])
        suites = selection.select_suites(['effects/reverb/src/wet_path.cpp'])
        self.assertTrue(suites['fx'] and suites['physical'] and suites['reverb'])
        suites = selection.select_suites(['instruments/physical-modeling/torus/dsp/fx/reverb.h'])
        self.assertTrue(suites['fx'] and suites['physical'])
        # Tape FX DSP: its own suite, the shared FX contract test and its Toy Piano consumer.
        suites = selection.select_suites(['effects/tape-fx/src/tape_fx.h'])
        self.assertTrue(suites['tape'] and suites['fx'] and suites['toy'] and suites['submodules'])
        self.assertFalse(suites['looper'])
        suites = selection.select_suites(['effects/tape-fx/tests/test_tape_fx.cpp'])
        self.assertTrue(suites['tape'] and suites['submodules'])
        self.assertFalse(suites['toy'] or suites['physical'] or suites['looper'])

    def test_bootloader_entry(self):
        # Every firmware product embeds the shared bootloader entry with its own version;
        # the panels that send its request depend on it.
        data = manifest.load_registry()
        self.assertEqual(manifest.read_version(data['components']['bootloader-entry']['version']), '1.0.0')
        for name in data['products']:
            ids = [item['id'] for item in manifest.describe_product(name)['components']]
            self.assertIn('bootloader-entry', ids, name)
        for panel in ('mod-editor-web', 'mod-editor-juce', 'mod-editor-vst3'):
            self.assertIn('bootloader-entry', data['components'][panel]['depends'])
        suites = selection.select_suites(['common/bootloader/bootloader_entry.h'])
        for key in ('bootloader', 'toy', 'physical', 'granular', 'looper', 'reverb', 'tape', 'mod',
                    'diagnostics', 'submodules'):
            self.assertTrue(suites[key], key)

    def test_sd_driver_consumers(self):
        # Nebulae 0.2 links the shared read-only driver and the card reader; the
        # Diagnostics card-reader image links only the reader. Nothing else runs.
        suites = selection.select_suites(['common/sd/ro_sd.c'])
        self.assertTrue(suites['sd'] and suites['granular'] and suites['submodules'])
        for key in ('toy', 'physical', 'looper', 'reverb', 'tape', 'bootloader', 'diagnostics'):
            self.assertFalse(suites[key], key)
        reader = selection.select_suites(['common/sd/reader/msc_bot.cpp'])
        self.assertTrue(reader['sd'] and reader['granular'] and reader['diagnostics'])
        self.assertFalse(reader['toy'])
        self.assertTrue(selection.select_suites(['diagnostics/protocol/ddp.cpp'])['granular'])
        self.assertTrue(selection.select_suites(['Makefile'])['sd'])

    def test_acid_seq_components(self):
        # Components are registered with their version sources; products follow only
        # with their first validated build (BUILDABLE_PROJECTS == products above).
        data = manifest.load_registry()
        expected = {'step-seq': 'sequencer', 'acid-303': 'instrument', 'saturation': 'dsp', 'fast-math': 'dsp',
                    'drive': 'effect', 'stereo-delay': 'effect'}
        for key, kind in expected.items():
            self.assertEqual(data['components'][key]['kind'], kind)
            self.assertRegex(manifest.read_version(data['components'][key]['version']), r'^\d+\.\d+\.\d+$')
        self.assertIn('saturation', data['components']['drive']['depends'])
        self.assertIn('fx-contract', data['components']['stereo-delay']['depends'])
        self.assertEqual(data['components']['saturation']['version']['path'], 'common/dsp/saturation.h')
        mk = (ROOT / 'projects.mk').read_text()
        for project in ('step-seq', 'acid-303', 'drive-fx', 'delay-fx', 'acid-seq'):
            self.assertRegex(mk, r'PROJECT_STATUS_' + re.escape(project) + r'\s*:=\s*(planned|buildable)')
        # Registered products name the device ID their firmware reports in IDENTIFY (D9).
        for name, product in data['products'].items():
            if 'device_id' not in product:
                continue
            source = ROOT / product['directory'].replace('/build', '') / 'src' / 'main.cpp'
            self.assertIn(f'"{product["device_id"]}"', source.read_text(encoding='utf-8'), name)

    def test_component_kinds(self):
        data = json.loads((ROOT / 'components/registry.json').read_text())
        self.assertIn('sequencer', manifest.KINDS)
        self.assertIn('dsp', manifest.KINDS)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            registry = copy_version_sources(data, root)
            registry.write_text(json.dumps(data))
            self.assertIn('step-seq', manifest.load_registry(root)['components'])
            data['components']['saturation']['kind'] = 'library'
            registry.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, 'Invalid kind'):
                manifest.load_registry(root)

    def assertOnly(self, suites, selected, label):
        selected = set(selected) | {'components'}
        for key, value in suites.items():
            if key == 'submodules':
                continue
            self.assertEqual(value, key in selected, f'{label}: {key}')

    def test_acid_seq_consumers(self):
        cases = {
            # Shared Step Seq base: every standalone firmware built on it (Codex F14).
            'sequencers/step-seq/include/step_seq/engine.h': ACID_SEQ_SUITES,
            'sequencers/step-seq/include/step_seq/spsc.h': ACID_SEQ_SUITES + ('streichwurst',),
            'sequencers/step-seq/include/step_seq/controls.h': ACID_SEQ_SUITES,
            'sequencers/step-seq/include/step_seq/diagnostics.h': ACID_SEQ_SUITES + ('streichwurst',),
            'sequencers/step-seq/tests/fakes/desktop_devkit.h': ACID_SEQ_SUITES + ('streichwurst',),
            'sequencers/step-seq/include/step_seq/device.h': ACID_SEQ_SUITES + ('editor', 'streichwurst'),
            'sequencers/step-seq/include/step_seq/sq_protocol.h': ACID_SEQ_SUITES + ('editor', 'streichwurst'),
            # Step Seq's own parts.
            'sequencers/step-seq/include/step_seq/midi_out.h': ('stepseq', 'acidseq'),
            'sequencers/step-seq/include/step_seq/params.h': ('stepseq', 'acidseq'),
            'sequencers/step-seq/src/main.cpp': ('stepseq', 'acidseq'),
            'instruments/acid-303/include/acid303/voice.h': ('acid303', 'acidseq'),
            # New effect cores and adapters also run the shared FX contract test.
            'effects/drive/include/drive/drive.h': ('drive', 'fx', 'acidseq'),
            'effects/stereo-delay/include/stereo_delay/delay.h': ('delay', 'fx', 'acidseq', 'streichwurst'),
            'sequencers/acid-seq/src/main.cpp': ('acidseq',),
            # Shared saturation: its consumers only, no longer every suite.
            'common/dsp/saturation.h': ('tape', 'toy', 'drive', 'delay', 'fx', 'acidseq'),
            'common/dsp/fastmath.h': ('tape', 'toy', 'drive', 'delay', 'fx', 'acidseq'),
            'common/audio/fx_contract.h': ('toy', 'physical', 'tape', 'fx', 'drive', 'delay', 'acidseq', 'streichwurst'),
            'sequencers/step-seq/contracts/parameters.json': ACID_SEQ_SUITES + ('editor', 'streichwurst'),
            'sequencers/step-seq/docs/PROTOCOL.md': ACID_SEQ_SUITES + ('editor', 'streichwurst'),
            'tests/fixtures/acid-seq/manifest.json': ('acid303', 'drive', 'delay', 'acidseq'),
            'sequencers/README.md': (),
            'sequencers/step-seq/README.md': ('stepseq', 'acidseq'),
            # Streichwurst: its own suite; the contract also regenerates the editor labels.
            'instruments/streichwurst/include/streichwurst/instrument.h': ('streichwurst',),
            'effects/streichwurst-fx/include/streichwurst_fx/ensemble.h': ('streichwurst',),
            'instruments/streichwurst/contracts/parameters.json': ('streichwurst', 'editor'),
            'instruments/streichwurst/src/register.h': ('streichwurst', 'editor'),
            'sequencers/step-seq/tools/gen_params.py': ('stepseq', 'acidseq', 'streichwurst', 'editor'),
        }
        for path, selected in cases.items():
            suites = selection.select_suites([path])
            self.assertOnly(suites, selected, path)
            self.assertEqual(suites['submodules'], bool(set(selected) - {'fx', 'editor'}), path)
        diagnostics = selection.select_suites(['modules/mod-on-daisy/adapters/callback_diagnostics.h'])
        for key in ACID_SEQ_SUITES:
            self.assertTrue(diagnostics[key], key)
        for path in ('Makefile', 'projects.mk', 'common/bootloader/bootloader_entry.h', '.github/workflows/quick.yml'):
            suites = selection.select_suites([path])
            for key in ACID_SEQ_SUITES:
                self.assertTrue(suites[key], f'{path}: {key}')
        # Changes elsewhere leave the Acid Seq suites alone.
        for path in ('instruments/toy-piano/src/toy_piano.cpp', 'effects/tape-fx/src/tape_fx.h',
                     'editors/mod-on-daisy/web/mod_protocol.js', 'common/sd/ro_sd.c'):
            suites = selection.select_suites([path])
            for key in ACID_SEQ_SUITES + ('streichwurst',):
                self.assertFalse(suites[key], f'{path}: {key}')
        # Streichwurst sources leave the other instruments alone.
        suites = selection.select_suites(['instruments/streichwurst/src/main.cpp'])
        self.assertOnly(suites, ('streichwurst',), 'streichwurst main')
        self.assertTrue(suites['submodules'])


if __name__ == '__main__':
    unittest.main()
