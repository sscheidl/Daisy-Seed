#!/usr/bin/env python3
"""Profile the Cumulonimbus diagnostic images over USB-MIDI and write CSV rows.

Images: CumulonimbusFx48/32 (standalone) and ToyPianoCumulonimbus48/32Profile. Protocol
(effects/cumulonimbus/src/stage_meter.h): requests on MIDI channel 16, CC20 scenario, CC21 grain cap,
CC22 stimulus (standalone: 0 input, 1 impulses, 2 bursts, 3 noise; Toy Piano: 0 none, 1/8/16 MIDI
voices, 4 = 16 MIDI + 16 key voices), CC119 = 1 snapshot, 2 reset. A snapshot is 11 records of
channel-16 CC100..119 with protocol id 3, three u32 metrics each (five 7-bit digits, little endian),
CC117 metric count, CC118 checksum (sum of CC101..117 & 127), CC119 = 64.

The decoder uses only the standard library. Device access needs mido and python-rtmidi on the
measurement computer. Hardware runs only with the owner's consent; this tool never flashes.

  cumulonimbus_probe.py --list
  cumulonimbus_probe.py snapshot [--in NAME --out NAME]
  cumulonimbus_probe.py set --scenario 0 --cap 16 --stimulus 4
  cumulonimbus_probe.py sweep --scenarios 1,2,3,0 --caps 8,16,24,32 --stimulus 4 \
      --warmup 5 --seconds 30 --csv results.csv --label B3 --image ToyPianoCumulonimbus48Profile-v0.1.0
  cumulonimbus_probe.py play --seconds 1800 --voices 16 --cc-traffic   # B4 soak on a playable image
  cumulonimbus_probe.py selftest
"""
import argparse
import csv
from datetime import datetime, timezone
import sys
import time

PROTOCOL_ID = 3
METRICS = [
    'scenario', 'cap', 'stimulus',
    'internal_rate', 'block_frames', 'budget_cycles',
    'blocks', 'window_ms', 'missed_periods',
    'overruns', 'total_max', 'total_mean',
    'p50', 'p90', 'p99',
    'piano_max', 'write_max', 'render_max',
    'convert_max', 'reverb_max', 'render_mean',
    'grains_active', 'grains_peak', 'rejected',
    'started_high', 'started_medium', 'midi_overruns',
    'voices_active', 'voices_peak', 'flags',
    'piano_mean', 'reverb_mean', 'write_mean',
    # metric set 2 (meter 0.2.0)
    'control_drops', 'stale_rates', 'accounting_max',
    'stack_free', 'stack_size', 'metric_set',
]
RECORDS = len(METRICS) // 3
SCENARIOS = ['full', 'dry', 'grains_only', 'reverb_only', 'write_only', 'render_only', 'muted']
FLAGS = {1: 'profiling', 2: 'bypass', 4: 'freeze', 8: 'suspended', 16: 'fx_page', 32: 'rate32', 64: 'mono'}
DERIVED = ['peak_load_pct', 'mean_load_pct', 'p99_load_pct', 'accounting_pct', 'expected_blocks', 'block_deficit',
           'verdict']


class RecordDecoder:
    """Feeds (channel, control, value) triples; returns a metrics dict once all records arrived."""

    def __init__(self):
        self.values = []
        self.records = {}

    def feed(self, channel, control, value):
        if channel != 15 or not 100 <= control <= 119 or not 0 <= value <= 127:
            return None
        if control == 100:
            self.values = [value] if value == PROTOCOL_ID else []
            return None
        if not self.values or control != 100 + len(self.values):
            self.values = []
            return None
        self.values.append(value)
        if control != 119:
            return None
        v, self.values = self.values, []
        if v[19] != 64 or v[17] != len(METRICS) or (sum(v[1:18]) & 127) != v[18]:
            return None
        record = v[1]
        if record >= RECORDS:
            return None
        for i in range(3):
            digits = v[2 + 5 * i: 7 + 5 * i]
            self.records[record * 3 + i] = sum(d << (7 * n) for n, d in enumerate(digits))
        if len(self.records) == len(METRICS):
            result = {name: self.records[i] for i, name in enumerate(METRICS)}
            self.records = {}
            return result
        return None


def encode(metrics):
    """Python twin of EncodeRecord(): list of 60-byte records (for tests and the self-test)."""
    values = [metrics.get(name, 0) for name in METRICS]
    out = []
    for record in range(RECORDS):
        data = [PROTOCOL_ID, record]
        for value in values[record * 3: record * 3 + 3]:
            data += [(value >> (7 * d)) & 127 for d in range(5)]
        data.append(len(METRICS))
        data.append(sum(data[1:]) & 127)
        data.append(64)
        packet = []
        for i, value in enumerate(data):
            packet += [0xBF, 100 + i, value]
        out.append(bytes(packet))
    return out


def derive(m):
    """Load in % of the budget, block accounting and the Stage 1 verdict.

    PASS <= 70 % peak; MIDDLE 70-80 %; FAIL above 80 %, on any missed period or overrun, on any MIDI
    note or control-queue drop, and when the counted blocks do not match the measured window (an
    invalid measurement, never a pass)."""
    budget = max(1, m['budget_cycles'])
    # The window spans the first to the last counted block: blocks = window_ms + 1 at 1 kHz.
    expected = m['window_ms'] + 1 if m['blocks'] else 0
    d = {
        'peak_load_pct': round(100.0 * m['total_max'] / budget, 2),
        'mean_load_pct': round(100.0 * m['total_mean'] / budget, 2),
        'p99_load_pct': round(m['p99'] / 100.0, 2),
        'accounting_pct': round(100.0 * m.get('accounting_max', 0) / budget, 2),
        'expected_blocks': expected,
        'block_deficit': expected - m['blocks'],
    }
    if abs(d['block_deficit']) > max(5, expected // 500):
        verdict = 'FAIL: invalid window (block count does not match the measured time)'
    elif m['missed_periods'] or m['overruns']:
        verdict = 'FAIL: missed periods or overruns'
    elif m['midi_overruns'] or m.get('control_drops', 0):
        verdict = 'FAIL: MIDI note or control queue drops'
    elif d['peak_load_pct'] > 80.0:
        verdict = 'FAIL: peak above 80 %'
    elif d['peak_load_pct'] > 70.0:
        verdict = 'MIDDLE: 70-80 % peak'
    else:
        verdict = 'PASS: <= 70 % peak'
    d['verdict'] = verdict
    return d


def flags_text(flags):
    return '+'.join(name for bit, name in FLAGS.items() if flags & bit) or '-'


def print_metrics(m):
    d = derive(m)
    scenario = SCENARIOS[m['scenario']] if m['scenario'] < len(SCENARIOS) else str(m['scenario'])
    print(f"scenario {scenario}, cap {m['cap']}, stimulus {m['stimulus']}, rate {m['internal_rate']} Hz, "
          f"flags {flags_text(m['flags'])}")
    print(f"blocks {m['blocks']} in {m['window_ms']} ms, missed {m['missed_periods']}, overruns {m['overruns']}")
    print(f"load peak {d['peak_load_pct']} %, mean {d['mean_load_pct']} %, p50 {m['p50'] / 100} %, "
          f"p90 {m['p90'] / 100} %, p99 {m['p99'] / 100} % of {m['budget_cycles']} cycles")
    print(f"stage max cycles: piano {m['piano_max']}, write {m['write_max']}, render {m['render_max']}, "
          f"convert {m['convert_max']}, reverb {m['reverb_max']}")
    print(f"grains active {m['grains_active']}, peak {m['grains_peak']}, rejected {m['rejected']}, "
          f"started high/medium {m['started_high']}/{m['started_medium']}; voices {m['voices_active']} "
          f"(peak {m['voices_peak']}), MIDI overruns {m['midi_overruns']}, control drops {m['control_drops']}, "
          f"stale rate reports {m['stale_rates']}")
    stack = f"{m['stack_free']} of {m['stack_size']} bytes never touched" if m['stack_size'] else 'not measured'
    print(f"meter accounting max {m['accounting_max']} cycles ({d['accounting_pct']} %, outside the peak); "
          f"stack {stack}")
    print(d['verdict'])


def selftest():
    sample = {name: (i * 7919) % 100000 for i, name in enumerate(METRICS)}
    sample.update(budget_cycles=480000, total_max=300000, window_ms=29999, blocks=30000,
                  missed_periods=0, overruns=0, midi_overruns=0, control_drops=0, flags=1 | 32, metric_set=2)
    decoder = RecordDecoder()
    result = None
    for packet in encode(sample):
        for i in range(0, len(packet), 3):
            result = decoder.feed(packet[i] & 15, packet[i + 1], packet[i + 2]) or result
    assert result == sample, 'decode mismatch'
    assert derive(result)['verdict'].startswith('PASS')
    print('selftest: PASS')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--list', action='store_true', help='list MIDI ports')
    p.add_argument('--in', dest='input', help='MIDI input port (default: first with "Daisy")')
    p.add_argument('--out', dest='output', help='MIDI output port (default: first with "Daisy")')
    sub = p.add_subparsers(dest='command')
    sub.add_parser('snapshot')
    sub.add_parser('reset')
    s = sub.add_parser('set')
    s.add_argument('--scenario', type=int)
    s.add_argument('--cap', type=int)
    s.add_argument('--stimulus', type=int)
    w = sub.add_parser('sweep')
    w.add_argument('--scenarios', default='1,2,3,0')
    w.add_argument('--caps', default='8,16,24,32')
    w.add_argument('--stimulus', type=int, default=0)
    w.add_argument('--warmup', type=float, default=5.0)
    w.add_argument('--seconds', type=float, default=30.0)
    w.add_argument('--csv', required=True)
    w.add_argument('--label', default='')
    w.add_argument('--image', default='')
    w.add_argument('--stop-on-fail', action='store_true')
    y = sub.add_parser('play', help='deterministic MIDI load: chord attacks every 500 ms, optional CC sweeps')
    y.add_argument('--seconds', type=float, default=60.0)
    y.add_argument('--voices', type=int, default=16, help='notes per chord (MIDI pool: 16)')
    y.add_argument('--cc-traffic', action='store_true', help='sweep CC70-77 (FX page) slowly on channel 1')
    sub.add_parser('selftest')
    args = p.parse_args(argv)
    if args.command == 'selftest':
        return selftest()

    import mido  # measurement computer only
    if args.list or not args.command:
        print('Inputs:', *mido.get_input_names(), sep='\n  ')
        print('Outputs:', *mido.get_output_names(), sep='\n  ')
        return 0

    def pick(names, wanted):
        if wanted:
            return wanted
        for name in names:
            if 'daisy' in name.lower():
                return name
        raise SystemExit('No Daisy port found; pass --in/--out')

    with mido.open_input(pick(mido.get_input_names(), args.input)) as incoming, \
            mido.open_output(pick(mido.get_output_names(), args.output)) as outgoing:
        def cc(control, value):
            outgoing.send(mido.Message('control_change', channel=15, control=control, value=value))

        def snapshot(timeout=3.0):
            for _ in incoming.iter_pending():
                pass
            decoder = RecordDecoder()
            cc(119, 1)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                for message in incoming.iter_pending():
                    if message.type == 'control_change':
                        result = decoder.feed(message.channel, message.control, message.value)
                        if result:
                            return result
                time.sleep(0.002)
            raise SystemExit('No complete telemetry snapshot (is a diagnostic image running?)')

        if args.command == 'snapshot':
            print_metrics(snapshot())
        elif args.command == 'reset':
            cc(119, 2)
        elif args.command == 'set':
            if args.scenario is not None: cc(20, args.scenario)
            if args.cap is not None: cc(21, args.cap)
            if args.stimulus is not None: cc(22, args.stimulus)
        elif args.command == 'play':
            notes = [48 + i for i in range(max(1, min(16, args.voices)))]
            start = time.monotonic()
            step = 0
            while time.monotonic() - start < args.seconds:
                for note in notes:  # one-shot voices: note-offs are ignored by Toy Piano
                    outgoing.send(mido.Message('note_on', channel=0, note=note, velocity=100))
                if args.cc_traffic:
                    for i in range(8):  # slow full-range triangle per destination, phase-shifted
                        phase = (step + 16 * i) % 64
                        value = phase * 2 if phase < 32 else (63 - phase) * 2
                        outgoing.send(mido.Message('control_change', channel=0, control=70 + i, value=value))
                step += 1
                time.sleep(0.5)
            print(f'played {step} chords of {len(notes)} notes')
        elif args.command == 'sweep':
            scenarios = [int(x) for x in args.scenarios.split(',') if x]
            caps = [int(x) for x in args.caps.split(',') if x]
            with open(args.csv, 'a', newline='') as handle:
                writer = csv.writer(handle)
                if handle.tell() == 0:
                    writer.writerow(['utc', 'label', 'image'] + METRICS + DERIVED)
                for scenario in scenarios:
                    for cap in caps:
                        cc(20, scenario)
                        cc(21, cap)
                        cc(22, args.stimulus)
                        time.sleep(args.warmup)
                        cc(119, 2)  # measured window starts after warm-up
                        time.sleep(args.seconds)
                        m = snapshot()
                        d = derive(m)
                        writer.writerow([datetime.now(timezone.utc).isoformat(timespec='seconds'), args.label,
                                         args.image] + [m[k] for k in METRICS] + [d[k] for k in DERIVED])
                        handle.flush()
                        print(f'--- scenario {scenario}, cap {cap}')
                        print_metrics(m)
                        if args.stop_on_fail and d['verdict'].startswith('FAIL'):
                            print('Stopping the sweep at the first failing point (statistics saved).')
                            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
