"""Acid Seq contracts: SQ vectors, stable parameter IDs, transport vectors, fixtures."""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parent.parent
CONTRACTS = ROOT / 'sequencers/step-seq/contracts'
DOCS = ROOT / 'sequencers/step-seq/docs'
FIXTURES = ROOT / 'tests/fixtures/acid-seq'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


codec = load('sq_codec', ROOT / 'sequencers/step-seq/tools/sq_codec.py')
generator = load('acid_seq_fixtures', FIXTURES / 'generate.py')


def frame_bytes(text):
    return [int(part, 16) for part in text.split()]


class SqVectorTests(unittest.TestCase):
    def setUp(self):
        self.data = json.loads((CONTRACTS / 'sq_vectors.json').read_text(encoding='utf-8'))

    def test_vectors_match_reference_codec(self):
        self.assertEqual(self.data, codec.vectors())
        self.assertEqual(self.data['crc8_check']['crc'], 0xF4)
        self.assertEqual(codec.crc8(b'123456789'), 0xF4)

    def test_every_vector_is_classified_as_documented(self):
        names = set()
        for case in self.data['cases']:
            self.assertNotIn(case['name'], names)
            names.add(case['name'])
            frame = frame_bytes(case['frame'])
            if 'stream' in case:
                assembled, removed = codec.assemble(frame_bytes(case['stream']))
                self.assertEqual((assembled, removed), (frame, case['realtime_bytes']), case['name'])
            verdict, detail = codec.classify(frame)
            self.assertEqual(verdict, case['verdict'], case['name'])
            if verdict == 'nak':
                self.assertEqual(detail, case['error'], case['name'])
            if verdict == 'ok':
                kind, rid, payload = detail
                if 'payload' in case:
                    self.assertEqual((kind, rid, payload), (case['type'], case['rid'], frame_bytes(case['payload'])))
                if kind not in codec.TYPES.values():
                    self.assertEqual(case['error'], codec.ERRORS['UNKNOWN_TYPE'])
            if verdict in ('ok', 'ignore'):
                self.assertLessEqual(len(frame), codec.MAX_FRAME, case['name'])
                self.assertTrue(all(b < 0x80 for b in frame[1:-1]))
        # Reply-class frames are never answered (echo guard); all three classes are covered.
        kinds = {c['type'] for c in self.data['cases'] if c['verdict'] == 'ignore'}
        self.assertTrue({codec.NAK, codec.MONITOR_EVENT} <= kinds)
        self.assertTrue(any(k & codec.RESPONSE and k < codec.MONITOR_EVENT for k in kinds))
        self.assertTrue(any('stream' in c for c in self.data['cases']))

    def test_field_codecs(self):
        for value in (0, 1, 127, 128, 0x3FFF):
            self.assertEqual(codec.Reader(codec.u14(value)).u14(), value)
        for value in (0, 1, 0x7FFFFFFF, 0xFFFFFFFF, 0xCAFEBABE):
            self.assertEqual(codec.Reader(codec.u32(value)).u32(), value)
        for value in (0, -1, -7000, 120000, -0x80000000, 0x7FFFFFFF):
            self.assertEqual(codec.Reader(codec.i32(value)).i32(), value)
        self.assertEqual(codec.Reader(codec.text('tempo', 16)).text(), 'tempo')
        for bad in (lambda: codec.u7(128), lambda: codec.u14(-1), lambda: codec.i32(1 << 31),
                    lambda: codec.text('x' * 17, 16), lambda: codec.text('Länge', 16),
                    lambda: codec.encode(1, 0, [0] * (codec.MAX_PAYLOAD + 1))):
            with self.assertRaises(ValueError):
                bad()
        with self.assertRaises(ValueError):
            codec.Reader([0, 0, 0, 0, 0x10]).u32()
        # A full 16-step pattern fits a single frame.
        pattern = sum((codec.step(59, 100, True, True, True) for _ in range(16)), [])
        self.assertLessEqual(len(codec.encode(codec.TYPES['GET_PATTERN'] | codec.RESPONSE, 0,
                                              codec.u14(0) + codec.u14(0) + pattern)), codec.MAX_FRAME)

    def test_descriptor_vector_matches_parameter_contract(self):
        params = json.loads((CONTRACTS / 'parameters.json').read_text(encoding='utf-8'))
        tempo = params['components']['step-seq']['parameters'][0]
        case = next(c for c in self.data['cases'] if c['name'].startswith('param descriptor'))
        r = codec.Reader(frame_bytes(case['payload']))
        comp, index, count, pid, kind, unit, flags = (r.u7(), r.u7(), r.u7(), r.u14(), r.u7(), r.u7(), r.u7())
        values = (r.i32(), r.i32(), r.i32())
        cc, key = r.u7(), r.text()
        self.assertTrue(r.done())
        self.assertEqual((comp, index, pid, key), (0, 0, tempo['id'], tempo['key']))
        self.assertEqual(count, len(params['components']['step-seq']['parameters']))
        self.assertEqual(params['types'][kind], tempo['type'])
        self.assertEqual(params['units'][unit], tempo['unit'])
        self.assertEqual(flags, 0)
        self.assertEqual(values, (tempo['min'] * 1000, tempo['max'] * 1000, tempo['default'] * 1000))
        self.assertEqual(cc, 127)


class ProbeReplyTests(unittest.TestCase):
    """sq_probe.py accepts only intact replies (F09): a damaged CRC counts as no reply."""

    def link(self, replies):
        import sys
        sys.path.insert(0, str(ROOT / 'sequencers/step-seq/tools'))
        probe = load('sq_probe', ROOT / 'sequencers/step-seq/tools/sq_probe.py')
        probe.TIMEOUT = 0
        link = object.__new__(probe.Link)
        link.rid, link.lost, link.sent = 0, 0, []
        link.send_raw = link.sent.append
        link.frames = lambda seconds: iter(replies.pop(0) if replies else [])
        return link

    def test_good_reply_is_read(self):
        get_state = codec.TYPES['GET_STATE']
        link = self.link([[codec.encode(get_state | codec.RESPONSE, 1, [2])]])
        self.assertEqual(link.request(get_state).u7(), 2)

    def test_bad_crc_is_ignored_and_retried(self):
        get_state = codec.TYPES['GET_STATE']
        bad = codec.encode(get_state | codec.RESPONSE, 1, [2])
        bad[-2] ^= 1
        link = self.link([[bad], [bad], [bad], [bad]])
        with self.assertRaises(SystemExit) as raised:
            link.request(get_state)
        self.assertIn('No reply', str(raised.exception))
        self.assertEqual(len(link.sent), 4)

    def test_decode_reply_rejects_damage(self):
        good = codec.encode(codec.TYPES['GET_STATE'] | codec.RESPONSE, 5, [1, 2])
        self.assertEqual(codec.decode_reply(good), (good[5], 5, [1, 2]))
        for broken in (good[:-1], good[:-4] + good[-3:], [good[0]] + [0x99] + good[2:]):
            self.assertIsNone(codec.decode_reply(broken))
        wrong_version = list(good)
        wrong_version[4] = 2
        self.assertIsNone(codec.decode_reply(wrong_version))


class ParameterContractTests(unittest.TestCase):
    def setUp(self):
        self.data = json.loads((CONTRACTS / 'parameters.json').read_text(encoding='utf-8'))

    def test_parameters_are_well_formed(self):
        ccs = {}
        for component, spec in self.data['components'].items():
            ids, keys = set(), set()
            self.assertGreaterEqual(spec['schema'], 1)
            for p in spec['parameters']:
                label = f"{component}.{p['key']}"
                self.assertTrue(1 <= p['id'] <= 0x3FFF, label)
                self.assertNotIn(p['id'], ids, label)
                self.assertNotIn(p['key'], keys, label)
                ids.add(p['id'])
                keys.add(p['key'])
                self.assertRegex(p['key'], r'^[a-z][a-z0-9_]{0,15}$')
                self.assertIn(p['type'], self.data['types'])
                self.assertIn(p['unit'], self.data['units'])
                for flag in p.get('flags', []):
                    self.assertIn(flag, self.data['flags'])
                self.assertLessEqual(p['min'], p['default'], label)
                self.assertLessEqual(p['default'], p['max'], label)
                for value in (p['min'], p['max'], p['default']):
                    milli = value * 1000
                    self.assertEqual(milli, round(milli), label)
                    self.assertTrue(-2**31 <= milli < 2**31, label)
                if p['type'] in ('int', 'enum', 'bool'):
                    self.assertTrue(all(float(v).is_integer() for v in (p['min'], p['max'], p['default'])), label)
                if p['type'] == 'bool':
                    self.assertEqual((p['min'], p['max']), (0, 1), label)
                if p['type'] == 'enum':
                    self.assertEqual(p['min'], 0, label)
                    self.assertEqual(len(p['enum']), p['max'] + 1, label)
                    self.assertEqual(len(p['enum_labels']), len(p['enum']), label)
                    for value in p['enum']:
                        codec.text(value, 16)
                if 'log' in p.get('flags', []):
                    self.assertGreater(p['min'], 0, label)
                if p['cc'] is not None:
                    # 120-127 are channel mode messages; CC 0/32 select banks.
                    self.assertTrue(1 <= p['cc'] <= 119 and p['cc'] != 32, label)
                    self.assertNotIn(p['cc'], ccs, f"{label} reuses CC of {ccs.get(p['cc'])}")
                    ccs[p['cc']] = label
        # The sequencer's controls belong to the hardware; voice and effects have CCs.
        self.assertTrue(all(p['cc'] is None for p in self.data['components']['step-seq']['parameters']))
        for component in ('acid-303', 'drive', 'stereo-delay'):
            self.assertTrue(any(p['cc'] is not None for p in self.data['components'][component]['parameters']))

    def test_documented_tables_match(self):
        text = (DOCS / 'PARAMETERS.md').read_text(encoding='utf-8')
        sections = re.split(r'^### `([a-z0-9-]+)`', text, flags=re.MULTILINE)
        documented = {sections[i]: sections[i + 1] for i in range(1, len(sections), 2)}
        self.assertEqual(set(documented), set(self.data['components']))
        for component, spec in self.data['components'].items():
            rows = re.findall(r'^\| (\d+) \| `([a-z0-9_]+)` \|[^|]*\|[^|]*\|[^|]*\|[^|]*\|[^|]*\| ([0-9—]+) \|',
                              documented[component], flags=re.MULTILINE)
            expected = [(str(p['id']), p['key'], str(p['cc']) if p['cc'] is not None else '—')
                        for p in spec['parameters']]
            self.assertEqual(rows, expected, component)

    def test_spec_ranges_from_work_order(self):
        def param(component, key):
            return next(p for p in self.data['components'][component]['parameters'] if p['key'] == key)
        self.assertEqual((param('step-seq', 'tempo')['min'], param('step-seq', 'tempo')['max']), (30, 300))
        self.assertEqual((param('step-seq', 'swing')['min'], param('step-seq', 'swing')['max']), (50, 75))
        self.assertEqual(param('step-seq', 'clock_source')['enum'][param('step-seq', 'clock_source')['default']],
                         'internal')  # D10
        self.assertEqual(param('step-seq', 'voice_channel')['default'], 2)
        self.assertEqual(param('stereo-delay', 'feedback')['max'], 95)
        self.assertEqual(param('stereo-delay', 'mode')['enum'][0], 'ping_pong')  # D7 default
        self.assertEqual((param('stereo-delay', 'time')['min'], param('stereo-delay', 'time')['max']), (10, 2000))
        self.assertEqual(len(param('stereo-delay', 'note_value')['enum']), 18)
        self.assertEqual(param('drive', 'target_offset')['default'], -1.5)
        self.assertEqual(param('drive', 'compensation')['default'], 100)
        self.assertEqual(param('drive', 'curve')['enum'], ['soft', 'hard', 'tape', 'fold'])


def reference_next(position, mode, length):
    """Next step after `position` (1-based, None = start), ARBEITSAUFTRAG 4.2."""
    if mode == 'forward':
        return 1 if position is None or position >= length else position + 1
    if position is None or position > length:
        return length
    return length if position <= 1 else position - 1


class TransportVectorTests(unittest.TestCase):
    def setUp(self):
        self.data = json.loads((CONTRACTS / 'transport_vectors.json').read_text(encoding='utf-8'))

    def test_tick_order(self):
        self.assertEqual(self.data['tick_order'], ['gate_off', 'reset', 'step', 'gate_on'])

    def test_sequences(self):
        for case in self.data['sequences']:
            mode, length, position = case['mode'], case['length'], None
            changes = {c['after']: c for c in case.get('changes', [])}
            steps = []
            for played in range(len(case['steps'])):
                if played in changes:
                    mode = changes[played].get('mode', mode)
                    length = changes[played].get('length', length)
                position = reference_next(position, mode, length)
                steps.append(position)
            self.assertEqual(steps, case['steps'], case['name'])

    def test_random_draws(self):
        spec = self.data['random']
        for case in spec['draws']:
            x = case['seed'] or spec['default_seed']
            steps = []
            for _ in case['steps']:
                x ^= (x << 13) & 0xFFFFFFFF
                x ^= x >> 17
                x ^= (x << 5) & 0xFFFFFFFF
                steps.append(1 + x % case['length'])
            self.assertEqual(steps, case['steps'])
            self.assertTrue(all(1 <= s <= case['length'] for s in steps))
        zero = next(c for c in spec['draws'] if c['seed'] == 0)
        default = next(c for c in spec['draws'] if c['seed'] == spec['default_seed'])
        self.assertEqual(zero['steps'], default['steps'])


class FixtureTests(unittest.TestCase):
    def test_manifest_and_files(self):
        manifest = json.loads((FIXTURES / 'manifest.json').read_text(encoding='utf-8'))
        self.assertEqual(manifest['sample_rate'], 48000)
        expected = {'saw_lp1', 'saw_lp2', 'saw_lp3', 'square_lp1', 'square_lp2', 'square_lp3',
                    'saw_resonance', 'impulse', 'silence'}
        self.assertEqual(set(manifest['files']), expected)
        for name, entry in manifest['files'].items():
            data = (FIXTURES / entry['file']).read_bytes()
            self.assertEqual(len(data), manifest['frames'] * 4, name)
            self.assertEqual(hashlib.sha256(data).hexdigest(), entry['sha256'], name)
            values = struct.unpack(f"<{manifest['frames']}f", data)
            self.assertAlmostEqual(max(abs(v) for v in values), entry['peak'], places=5)
        self.assertEqual(manifest['fixture_version'], generator.FIXTURE_VERSION)

    def test_generator_reproduces_files(self):
        manifest = json.loads((FIXTURES / 'manifest.json').read_text(encoding='utf-8'))
        for name, (signal, _) in generator.fixtures().items():
            stored = struct.unpack(f"<{manifest['frames']}f", (FIXTURES / f'{name}.f32').read_bytes())
            packed = struct.unpack(f'<{len(signal)}f', generator.pack(signal))
            self.assertLess(max(abs(a - b) for a, b in zip(stored, packed)), 1e-6, name)


if __name__ == '__main__':
    unittest.main()
