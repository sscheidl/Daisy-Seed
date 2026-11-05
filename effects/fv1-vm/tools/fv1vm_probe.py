#!/usr/bin/env python3
"""Read fv1-vm firmware CPU telemetry over USB-MIDI, optionally sweeping all programs.

Protocol (src/fv1_firmware.h): requests on MIDI channel 16, CC119 = 1 read, 2 reset and read;
CC22/CC23 program index (high/low 7 bits, 0-based), CC24 input (0 line, 1 internal noise),
CC25..27 POT0..2, CC28 bypass. Reply: channel-16 CC100..119 with protocol id 2.

The decoder uses only the standard library. Device access needs mido and python-rtmidi on the
measurement computer. Hardware runs only with the owner's consent; flashing is never done here.
"""
import argparse
import csv
from datetime import datetime, timezone
import time

FLAGS = {1: 'double', 2: 'fast_memory', 4: 'bypass', 8: 'noise', 16: 'local_programs'}


class SnapshotDecoder:
    """Feeds channel/control/value triples; returns a dict when a complete packet arrived."""

    def __init__(self):
        self.values = []

    def feed(self, channel, control, value):
        if channel != 15 or not 100 <= control <= 119:
            return None
        if not 0 <= value <= 127:
            self.values = []
            return None
        if control == 100:
            self.values = [value] if value == 2 else []
            return None
        if not self.values or control != 100 + len(self.values):
            self.values = []
            return None
        self.values.append(value)
        if control != 119:
            return None
        values, self.values = self.values, []
        if values[19] != 64:
            return None

        def number(start, digits):
            return sum(values[start + i] << (7 * i) for i in range(digits))

        blocks, overruns = number(7, 5), number(12, 4)
        if overruns > blocks:
            return None
        flags = values[18]
        return dict(average_percent=number(1, 3) / 100, peak_percent=number(4, 3) / 100,
                    blocks=blocks, overruns=overruns, program=number(16, 2) + 1,
                    flags='+'.join(name for bit, name in FLAGS.items() if flags & bit) or 'none')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--list', action='store_true', help='list MIDI ports')
    parser.add_argument('--input', help='exact MIDI input port name')
    parser.add_argument('--output', help='exact MIDI output port name')
    parser.add_argument('--seconds', type=float, default=3, help='seconds per program (minimum 1)')
    parser.add_argument('--sweep', type=int, default=0, help='measure programs 1..N (0: current program only)')
    parser.add_argument('--noise', action='store_true', help='use the internal noise input')
    parser.add_argument('--pots', type=int, nargs=3, metavar=('P0', 'P1', 'P2'), help='remote pots 0..127')
    parser.add_argument('--csv', default='fv1vm-cpu.csv', help='output CSV with device readings')
    args = parser.parse_args()
    try:
        import mido
    except ImportError:
        parser.error('install measurement dependencies: python -m pip install mido python-rtmidi')
    if args.list:
        print('Inputs:', *mido.get_input_names(), sep='\n  ')
        print('Outputs:', *mido.get_output_names(), sep='\n  ')
        return
    if not args.input or not args.output or args.seconds < 1:
        parser.error('--input, --output and --seconds >= 1 are required')
    fields = ['utc', 'program', 'flags', 'average_percent', 'peak_percent', 'blocks', 'overruns']
    with mido.open_input(args.input) as incoming, mido.open_output(args.output) as outgoing, \
            open(args.csv, 'w', newline='') as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        decoder = SnapshotDecoder()

        def cc(control, value):
            outgoing.send(mido.Message('control_change', channel=15, control=control, value=value))

        def read(timeout=3.0):
            start = time.monotonic()
            while time.monotonic() - start < timeout:
                for message in incoming.iter_pending():
                    if message.type == 'control_change':
                        sample = decoder.feed(message.channel, message.control, message.value)
                        if sample is not None:
                            return sample
                time.sleep(.001)
            raise RuntimeError('no telemetry reply within 3 s; check firmware, ports and MIDI routing')

        cc(28, 0)
        cc(24, 1 if args.noise else 0)
        if args.pots:
            for i, value in enumerate(args.pots):
                cc(25 + i, max(0, min(127, value)))
        for index in (range(args.sweep) if args.sweep else [None]):
            if index is not None:
                cc(22, index >> 7)
                cc(23, index & 127)
                time.sleep(.3)  # program change, delay-RAM clear, settle
            cc(119, 2)
            read()
            time.sleep(args.seconds)
            cc(119, 1)
            sample = read()
            row = dict(utc=datetime.now(timezone.utc).isoformat(), **sample)
            writer.writerow(row)
            file.flush()
            print(f"program {sample['program']:3d}  avg {sample['average_percent']:6.2f}%  "
                  f"peak {sample['peak_percent']:6.2f}%  overruns {sample['overruns']}  [{sample['flags']}]")


if __name__ == '__main__':
    main()
