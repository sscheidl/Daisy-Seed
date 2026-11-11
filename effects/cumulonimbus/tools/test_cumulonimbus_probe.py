"""Probe parser tests against synthetic replies and, when present, the firmware encoder's own bytes
(build/host/telemetry_fixture.bin, written by tests/test_firmware.cpp)."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import cumulonimbus_probe as probe  # noqa: E402

FIXTURE = pathlib.Path(__file__).resolve().parent.parent / 'build' / 'host' / 'telemetry_fixture.bin'


def feed(decoder, data):
    result = None
    for i in range(0, len(data), 3):
        got = decoder.feed(data[i] & 15, data[i + 1], data[i + 2])
        result = got or result
    return result


class ProbeTests(unittest.TestCase):
    def sample(self):
        m = {name: i * 1000 + 7 for i, name in enumerate(probe.METRICS)}
        m.update(budget_cycles=480000, total_max=2 ** 31 - 1)
        return m

    def test_roundtrip(self):
        m = self.sample()
        self.assertEqual(feed(probe.RecordDecoder(), b''.join(probe.encode(m))), m)

    def test_records_in_any_order_and_noise(self):
        m = self.sample()
        packets = probe.encode(m)
        stream = b'\xb0\x01\x40' + b''.join(reversed(packets)) + b'\xbf\x64\x02'
        self.assertEqual(feed(probe.RecordDecoder(), stream), m)

    def test_bad_checksum_and_truncation_are_ignored(self):
        m = self.sample()
        packets = [bytearray(p) for p in probe.encode(m)]
        packets[0][3 * 18 + 2] ^= 1  # checksum
        decoder = probe.RecordDecoder()
        self.assertIsNone(feed(decoder, b''.join(bytes(p) for p in packets)))
        truncated = probe.encode(m)[1][:30]
        self.assertIsNone(feed(probe.RecordDecoder(), truncated))
        other = bytearray(probe.encode(m)[0])
        other[2] = 2  # the fv1-vm protocol id
        self.assertIsNone(feed(probe.RecordDecoder(), bytes(other)))

    def test_derive(self):
        m = self.sample()
        m.update(total_max=300000, total_mean=200000, p99=6100, window_ms=29999, blocks=30000,
                 missed_periods=0, overruns=0, midi_overruns=0, control_drops=0, accounting_max=4800)
        d = probe.derive(m)
        self.assertTrue(d['verdict'].startswith('PASS'))
        self.assertEqual((d['expected_blocks'], d['block_deficit'], d['accounting_pct']), (30000, 0, 1.0))
        m['total_max'] = 360000
        self.assertTrue(probe.derive(m)['verdict'].startswith('MIDDLE'))
        m['total_max'] = 400000
        self.assertTrue(probe.derive(m)['verdict'].startswith('FAIL'))
        m['total_max'] = 100000
        m['missed_periods'] = 1
        self.assertTrue(probe.derive(m)['verdict'].startswith('FAIL'))
        m['missed_periods'] = 0
        m['midi_overruns'] = 1  # a refused note fails the point
        self.assertIn('queue drops', probe.derive(m)['verdict'])
        m['midi_overruns'] = 0
        m['control_drops'] = 2
        self.assertIn('queue drops', probe.derive(m)['verdict'])
        m['control_drops'] = 0
        # Duration-only counters can hide skipped DMA periods: a mismatch is invalid, never a pass,
        # and starts with FAIL so --stop-on-fail ends the sweep (PR #108 review R7).
        m['blocks'] = 29000
        verdict = probe.derive(m)['verdict']
        self.assertTrue(verdict.startswith('FAIL: invalid window'), verdict)
        m['blocks'] = 30003  # within the tolerance of max(5, window/500)
        self.assertTrue(probe.derive(m)['verdict'].startswith('PASS'))

    def test_selftest(self):
        probe.selftest()

    @unittest.skipUnless(FIXTURE.exists(), 'run make test (C++ encoder fixture) first')
    def test_firmware_encoder_bytes(self):
        data = FIXTURE.read_bytes()
        result = feed(probe.RecordDecoder(), data)
        self.assertIsNotNone(result)
        self.assertEqual(result, {name: i * 1000 + 7 for i, name in enumerate(probe.METRICS)})


if __name__ == '__main__':
    unittest.main()
