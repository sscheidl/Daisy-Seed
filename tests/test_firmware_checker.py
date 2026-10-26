"""scripts/check-firmware.py for the Acid Seq images: positive and negative cases (R12)."""
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
CHECKER = ROOT / 'scripts' / 'check-firmware.py'
# prefix, version source, macro, marker (as in check-firmware.py ACID_SEQ)
IMAGES = [
    ('StepSeq-v', 'sequencers/step-seq/src/version.h', 'STEP_SEQ_FIRMWARE_VERSION', 'STEP-SEQ-FW-VERSION='),
    ('Acid303-v', 'instruments/acid-303/src/version.h', 'ACID_303_FIRMWARE_VERSION', 'ACID-303-FW-VERSION='),
    ('DriveFx-v', 'effects/drive/src/version.h', 'DRIVE_FIRMWARE_VERSION', 'DRIVE-FX-VERSION='),
    ('DelayFx-v', 'effects/stereo-delay/src/version.h', 'STEREO_DELAY_FIRMWARE_VERSION', 'DELAY-FX-VERSION='),
]


def version_of(source, macro):
    return re.search(macro + r'\s+"([^"]+)"', (ROOT / source).read_text()).group(1)


def image(marker, version, stack=0x20020000, reset=0x240003D1):
    return struct.pack('<II', stack, reset) + b'\0' * 1024 + (marker + version + '\0').encode() + b'\0' * 64


def check(path):
    return subprocess.run([sys.executable, str(CHECKER), str(path)], capture_output=True, text=True)


class FirmwareCheckerTests(unittest.TestCase):
    def test_acid_seq_images(self):
        for prefix, source, macro, marker in IMAGES:
            version = version_of(source, macro)
            with tempfile.TemporaryDirectory() as folder:
                good = Path(folder) / f'{prefix}{version}.bin'
                good.write_bytes(image(marker, version))
                result = check(good)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f'PASS: v{version}', result.stdout)
                # Wrong name, wrong or missing marker, foreign marker, bad vectors.
                wrong = Path(folder) / f'{prefix}9.9.9.bin'
                wrong.write_bytes(image(marker, version))
                self.assertNotEqual(check(wrong).returncode, 0)
                good.write_bytes(image(marker, '9.9.9'))
                self.assertNotEqual(check(good).returncode, 0)
                good.write_bytes(image('TOYPIANO-FW-VERSION=', version))
                self.assertNotEqual(check(good).returncode, 0)
                good.write_bytes(image(marker, version) + image(marker, version))
                self.assertNotEqual(check(good).returncode, 0)  # the marker exactly once
                good.write_bytes(image(marker, version, stack=0x30000000))
                self.assertNotEqual(check(good).returncode, 0)
                good.write_bytes(image(marker, version, reset=0x24000000))
                self.assertNotEqual(check(good).returncode, 0)  # Thumb bit missing

    def test_unknown_prefix_still_falls_back_to_toy_piano(self):
        with tempfile.TemporaryDirectory() as folder:
            other = Path(folder) / 'StepSequencer-v0.1.0.bin'
            other.write_bytes(image('STEP-SEQ-FW-VERSION=', '0.1.0'))
            self.assertNotEqual(check(other).returncode, 0)


if __name__ == '__main__':
    unittest.main()
