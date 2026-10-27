import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('performance_probe', Path(__file__).resolve().parents[1] / 'tools/performance_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)

def frame(stage=0, calls=0xffffffff, total=0xffffffffffffffff):
    values = [1, stage] + [(calls >> (7*i)) & 127 for i in range(5)]
    values += [(total >> (7*i)) & 127 for i in range(10)]
    values += [0]*10 + [0, 64]
    return values

class ProtocolTest(unittest.TestCase):
    def decode(self, values):
        decoder = probe.Decoder()
        result = None
        for cc, value in enumerate(values, 70):
            result = decoder.feed(15, cc, value)
        return result
    def test_limits(self):
        self.assertEqual(self.decode(frame())['total'], 0xffffffffffffffff)
        self.assertEqual(self.decode(frame(4, 1, 0xabcdef))['stage'], 'hash')
    def test_invalid(self):
        self.assertIsNone(self.decode(frame(6)))
        values = frame(); values[16] = 2
        self.assertIsNone(self.decode(values))
        values = frame(); values[-1] = 0
        self.assertIsNone(self.decode(values))
    def test_resync(self):
        decoder = probe.Decoder()
        for cc in (70, 71, 73):
            self.assertIsNone(decoder.feed(15, cc, 1))
        for cc, value in enumerate(frame(5, 0, 480000000), 70):
            result = decoder.feed(15, cc, value)
        self.assertEqual(result['total'], 480000000)
        self.assertIsNone(decoder.feed(0, 70, 1))

if __name__ == '__main__':
    unittest.main()
