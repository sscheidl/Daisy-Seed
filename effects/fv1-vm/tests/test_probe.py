"""Decoder and generator tests for the fv1-vm host tools (standard library only)."""
import importlib.util
import pathlib
import struct
import tempfile
import unittest

TOOLS = pathlib.Path(__file__).resolve().parents[1] / 'tools'


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f'{name}.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


probe = load('fv1vm_probe')
gen = load('gen_programs')
# Same packet as TestTelemetryPacket in tests/test_firmware.cpp.
PACKET = [2, 8, 39, 0, 106, 48, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 72, 1, 9, 64]


class DecoderTest(unittest.TestCase):
    def feed(self, decoder, values, channel=15):
        result = None
        for i, value in enumerate(values):
            result = decoder.feed(channel, 100 + i, value)
        return result

    def test_packet(self):
        sample = self.feed(probe.SnapshotDecoder(), PACKET)
        self.assertEqual(sample, dict(average_percent=50.0, peak_percent=62.5, blocks=3, overruns=0,
                                      program=201, flags='double+noise'))

    def test_rejects_other_protocol_and_gaps(self):
        decoder = probe.SnapshotDecoder()
        self.assertIsNone(self.feed(decoder, [1] + PACKET[1:]))  # physical-modeling packet id
        self.assertIsNone(self.feed(decoder, PACKET, channel=0))
        broken = list(PACKET)
        broken[19] = 63
        self.assertIsNone(self.feed(decoder, broken))
        self.assertIsNotNone(self.feed(decoder, PACKET))  # recovers on the next frame


class GeneratorTest(unittest.TestCase):
    def test_roundtrip(self):
        words = list(range(0x01000000, 0x01000000 + 128))
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / 'p.bin'
            path.write_bytes(struct.pack('>128I', *words))
            out = pathlib.Path(tmp) / 'out.h'
            self.assertEqual(gen.main(['--out', str(out), '--label', 'test', str(path)]), 0)
            text = out.read_text()
            self.assertIn('kCount = 1;', text)
            self.assertIn('0x01000000u, 0x01000001u', text)
            self.assertIn('LOCAL ONLY', text)
            path.write_bytes(b'\0' * 100)
            with self.assertRaises(ValueError):
                gen.main(['--out', str(out), str(path)])


if __name__ == '__main__':
    unittest.main()
