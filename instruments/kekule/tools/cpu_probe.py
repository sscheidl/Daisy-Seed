#!/usr/bin/env python3
"""Read real firmware CPU telemetry, optionally run a six-model MIDI stress pass.

Requires mido and python-rtmidi only on the measurement computer. This module's
decoder/tests use the Python standard library and never open MIDI devices.
"""
import argparse
import csv
from datetime import datetime, timezone
import time


class SnapshotDecoder:
    def __init__(self):
        self.values = []

    def feed(self, channel, control, value):
        if channel != 15 or not 100 <= control <= 119:
            return None
        if not 0 <= value <= 127:
            self.values = []
            return None
        if control == 100:
            self.values = [value] if value == 1 else []
            return None
        if not self.values or control != 100 + len(self.values):
            self.values = []
            return None
        self.values.append(value)
        if control != 119:
            return None
        values, self.values = self.values, []
        if values[19] != 64 or values[17] > 5 or values[18] != 4:
            return None

        def number(start, digits):
            return sum(values[start + i] << (7 * i) for i in range(digits))

        blocks, overruns = number(7, 5), number(12, 5)
        if blocks > 0xffffffff or overruns > blocks:
            return None
        return dict(average_percent=number(1, 3) / 100,
                    maximum_percent=number(4, 3) / 100,
                    blocks=blocks, overruns=overruns, model=values[17] + 1,
                    voices=values[18])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list', action='store_true', help='List available MIDI ports')
    parser.add_argument('--input', help='Exact MIDI input port name')
    parser.add_argument('--output', help='Exact MIDI output port name')
    parser.add_argument('--seconds', type=float, default=30, help='Seconds per phase (minimum 2)')
    parser.add_argument('--benchmark', action='store_true', help='Play four-note stress chords in each of six models')
    parser.add_argument('--csv', default='cpu-measurements.csv', help='Output CSV with actual device readings')
    args = parser.parse_args()
    try:
        import mido
    except ImportError:
        parser.error('Install measurement dependencies: python -m pip install mido python-rtmidi')
    if args.list:
        print('Inputs:', *mido.get_input_names(), sep='\n  ')
        print('Outputs:', *mido.get_output_names(), sep='\n  ')
        return
    if not args.input or not args.output or args.seconds < 2:
        parser.error('--input, --output and --seconds >= 2 are required')
    fields = ['utc', 'phase', 'model', 'voices', 'average_percent', 'maximum_percent', 'blocks', 'overruns']
    with mido.open_input(args.input) as incoming, mido.open_output(args.output) as outgoing, open(args.csv, 'w', newline='') as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        decoder = SnapshotDecoder()

        def cc(control, value, channel=0):
            outgoing.send(mido.Message('control_change', channel=channel, control=control, value=value))

        try:
            for phase in range(6 if args.benchmark else 1):
                if args.benchmark:
                    for control in (22, 23, 25, 26, 27, 31, 32, 33):
                        cc(control, 127)
                    cc(21, 64); cc(24, 64); cc(28, round(phase * 127 / 5))
                    time.sleep(.05)  # Settle model/parameters before this per-model epoch.
                cc(119, 2, 15)  # Reset and obtain first measured block.
                start = time.monotonic()
                last_reply = start
                next_request, next_chord = start + 1, start
                strikes = 0
                while time.monotonic() - start < args.seconds:
                    now = time.monotonic()
                    if args.benchmark and now >= next_chord:
                        # Four independent events/10 ms, varied pitch and allocation.
                        for offset in (0, 4, 7, 11):
                            note = 36 + (strikes * 7 + offset) % 60
                            outgoing.send(mido.Message('note_on', note=note, velocity=127))
                        strikes += 1
                        next_chord = now + .01
                    if now >= next_request:
                        cc(119, 1, 15)
                        next_request = now + 1
                    for message in incoming.iter_pending():
                        if message.type != 'control_change':
                            continue
                        sample = decoder.feed(message.channel, message.control, message.value)
                        if sample is not None:
                            last_reply = now
                            row = dict(utc=datetime.now(timezone.utc).isoformat(), phase=phase + 1, **sample)
                            writer.writerow(row); file.flush()
                            print(f"model {sample['model']}, avg {sample['average_percent']:.2f}%, peak {sample['maximum_percent']:.2f}%, overruns {sample['overruns']}, blocks {sample['blocks']}")
                    if now - last_reply > 3:
                        raise RuntimeError('No CPU telemetry reply for 3 seconds; check firmware, ports and MIDI routing')
                    time.sleep(.001)
        finally:
            if args.benchmark:
                cc(33, 0)  # FM can sustain indefinitely; silence the completed test.
                print('Benchmark finished: MIDI master volume is 0; restore CC33=127 to play.')


if __name__ == '__main__':
    main()
