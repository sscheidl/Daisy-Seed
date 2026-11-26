"""Offline telemetry protocol tests: no MIDI package/device is opened."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('cpu_probe', Path(__file__).resolve().parents[1] / 'tools/cpu_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def frame():
    values = [0] * 20
    values[0], values[17], values[18], values[19] = 1, 5, 4, 64
    for start, number, digits in [(1, 2500, 3), (4, 15000, 3), (7, 0xffffffff, 5), (12, 17, 5)]:
        for i in range(digits):
            values[start + i] = (number >> (7 * i)) & 127
    return values


class TelemetryTest(unittest.TestCase):
    def test_complete_frame_and_over_100_percent(self):
        decoder = probe.SnapshotDecoder()
        for i, value in enumerate(frame()):
            result = decoder.feed(15, 100 + i, value)
            if i < 19:
                self.assertIsNone(result)
        self.assertEqual(result, dict(average_percent=25, maximum_percent=150, blocks=0xffffffff, overruns=17, model=6, voices=4))

    def test_incomplete_foreign_out_of_order_and_malformed(self):
        decoder = probe.SnapshotDecoder()
        self.assertIsNone(decoder.feed(0, 100, 1))
        self.assertIsNone(decoder.feed(15, 100, 1))
        self.assertIsNone(decoder.feed(15, 103, 2))
        self.assertIsNone(decoder.feed(15, 119, 64))
        for broken in (19, 18, 17, 0):
            values = frame()
            values[broken] = 127
            for i, value in enumerate(values):
                self.assertIsNone(decoder.feed(15, 100 + i, value))
        self.assertIsNone(decoder.feed(15, 100, 1))
        self.assertIsNone(decoder.feed(15, 101, 128))

    def test_new_header_resynchronizes(self):
        decoder = probe.SnapshotDecoder()
        decoder.feed(15, 100, 1); decoder.feed(15, 101, 1)
        for i, value in enumerate(frame()):
            result = decoder.feed(15, 100 + i, value)
        self.assertEqual(result['model'], 6)


if __name__ == '__main__':
    unittest.main()
