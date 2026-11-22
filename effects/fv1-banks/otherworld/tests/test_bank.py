#!/usr/bin/env python3
"""Bank tests: reproducible build, word-for-word match with the upstream release, header layout."""
import hashlib
import json
import pathlib
import re
import struct
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE / 'tools'))
import build_bank  # noqa: E402


class BankTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.entries, cls.groups, cls.digest = build_bank.build()

    def test_committed_outputs_are_fresh(self):
        self.assertEqual(build_bank.main(['--check']), 0, 'run tools/build_bank.py and commit the outputs')

    def test_matches_release_programs(self):
        release = json.loads((HERE / 'tests' / 'release_sha256.json').read_text(encoding='ascii'))
        self.assertEqual(len(release['slots']), 31)
        for slot, ref in release['slots'].items():
            e = self.entries[int(slot) - 1]
            self.assertEqual(e['id'], ref['id'])
            got = hashlib.sha256(struct.pack('>128I', *e['words'])).hexdigest()
            self.assertEqual(got, ref['sha256'], f'slot {slot} {e["id"]} differs from the release')

    def test_layout(self):
        self.assertEqual(len(self.entries), 32)
        self.assertEqual([(g['key'], g['offset'], g['size']) for g in self.groups],
                         [('slicer', 0, 17), ('reverb', 17, 4), ('distortion', 21, 4), ('voice', 25, 7)])
        self.assertEqual([e['slot'] for e in self.entries if e['clocked']], [2, 4, 6, 8, 10, 12, 14, 24, 32])
        for e in self.entries:
            self.assertEqual(len(e['pots']), 3, e['id'])

    def test_header_digest_and_ascii(self):
        text = build_bank.HEADER.read_text(encoding='ascii')
        self.assertIn(self.digest, text)
        self.assertEqual(len(re.findall(r'^    \{ // ', text, re.M)), 32)

    def test_sources_unchanged_from_upstream_layout(self):
        bank = json.loads((HERE / 'upstream' / 'bank.json').read_text(encoding='utf-8'))
        for s in bank['slots']:
            self.assertTrue((HERE / 'upstream' / 'effects' / (s['id'] + '.spn')).exists(), s['id'])
        self.assertIn('MIT License', (HERE / 'upstream' / 'LICENSE').read_text(encoding='utf-8'))

    def test_rounding_helper(self):
        r = build_bank.round_half_away
        self.assertEqual((r(316.5), r(-316.5), r(316.49), r(0.5), r(-0.5)), (317, -317, 316, 1, -1))


if __name__ == '__main__':
    unittest.main()
