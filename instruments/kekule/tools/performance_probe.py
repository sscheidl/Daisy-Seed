#!/usr/bin/env python3
"""Read PM_PROFILE diagnostics; ordinary firmware never replies to these requests."""
import argparse
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import time

STAGES = ('voices', 'filters', 'reverb', 'output', 'hash', 'clock')

class Decoder:
    def __init__(self):
        self.values = []
    def feed(self, channel, control, value):
        if channel != 15 or not 70 <= control <= 98:
            return None
        if not 0 <= value <= 127:
            self.values = []
            return None
        if control == 70:
            self.values = [value] if value == 1 else []
            return None
        if not self.values or control != 70 + len(self.values):
            self.values = []
            return None
        self.values.append(value)
        if control != 98:
            return None
        values, self.values = self.values, []
        if values[-1] != 64 or values[1] > 5 or values[27]:
            return None
        def number(start, digits):
            return sum(values[start+i] << (7*i) for i in range(digits))
        calls, total = number(2, 5), number(7, 10)
        peak, blocks = number(17, 5), number(22, 5)
        if calls > 0xffffffff or peak > 0xffffffff or blocks > 0xffffffff or total > 0xffffffffffffffff:
            return None
        return dict(stage=STAGES[values[1]], calls=calls, total=total,
                    peak_cycles=peak, blocks=blocks)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--csv', required=True, help='New output file (never overwrites)')
    parser.add_argument('--reset', action='store_true', help='Reset profiler counters before sampling')
    args = parser.parse_args()
    if args.seconds < 4:
        parser.error('--seconds must be at least 4')
    import mido
    decoder = Decoder()
    with mido.open_input(args.input) as incoming, mido.open_output(args.output) as outgoing:
        def query(stage, reset=False):
            for _ in incoming.iter_pending():
                pass
            outgoing.send(mido.Message('control_change', channel=15, control=119,
                                       value=22 if reset else 16+stage))
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                for msg in incoming.iter_pending():
                    if msg.type == 'control_change':
                        result = decoder.feed(msg.channel, msg.control, msg.value)
                        if result and result['stage'] == STAGES[stage]:
                            return result
                time.sleep(.002)
            raise RuntimeError('No profile reply: requires PM_PROFILE=1 firmware and free MIDI ports')
        metadata = dict(clock_hz=query(5)['total'], render=query(4),
                        recorded_utc=datetime.now(timezone.utc).isoformat())
        if args.reset:
            query(0, reset=True)
        print('Device clock:', metadata['clock_hz'], 'Hz')
        if metadata['render']['calls'] == 1:
            print('ARM corpus v1:', f"{metadata['render']['total']:016X}")
        else:
            print('ARM render hash unavailable (build with PM_RENDER_HASH=1)')
        path = Path(args.csv)
        fields = ['utc', 'stage', 'calls', 'total', 'peak_cycles', 'blocks']
        with path.open('x', newline='') as file:
            writer = csv.DictWriter(file, fieldnames=fields)
            writer.writeheader()
            start, count = time.monotonic(), 0
            while time.monotonic() - start < args.seconds:
                row = dict(utc=datetime.now(timezone.utc).isoformat(), **query(count % 4))
                writer.writerow(row); file.flush()
                print(row)
                count += 1
                time.sleep(min(1, max(0, args.seconds - (time.monotonic() - start))))
        path.with_suffix('.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')

if __name__ == '__main__':
    main()
