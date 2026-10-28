#!/usr/bin/env python3
"""A/B CPU and stability run for Physical Modeling MOD over USB MIDI.

Plays the kit itself (notes, CC20..33, pitch bend) and reads the firmware's CPU
telemetry (channel 16, CC119) at the same time. Nothing is flashed.

    python tools/pm_ab_test.py --label A          # full run, about 9 minutes
    python tools/pm_ab_test.py --label B
    python tools/pm_ab_test.py --compare results/run-A-*.json results/run-B-*.json
    python tools/pm_ab_test.py --selftest         # offline, no MIDI device

Per model: model change (measured separately, not a target), settled chords,
two moving knobs, eight automated knobs plus pitch bend, and the 22.10.2026
stress benchmark (four notes every 10 ms, all CCs at 127) for comparability.
Each phase resets the counters after a settle time, so model-change spikes do
not leak into the other phases. Results: results/run-<label>-<time>.csv/.json.
"""
import argparse
import csv
import json
import math
import pathlib
import sys
import time
from datetime import datetime, timezone

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from cpu_probe import SnapshotDecoder  # noqa: E402

MODELS = ['Modal Resonator', 'Sympathetic Strings', 'Inharmonic Strings',
          'FM Voice', 'Western Chords', 'String & Reverb']
# CC = 20 + parameter index (src/parameter_manager.h); 29 (Polyphony) is ignored.
FREQUENCY, STRUCTURE, BRIGHTNESS, DAMPING, POSITION, EXCITATION, DECAY, WET = range(20, 28)
MODEL, REVERB_DAMPING, DIFFUSION, INPUT_GAIN, MASTER = 28, 30, 31, 32, 33
BASE = {FREQUENCY: 64, STRUCTURE: 64, BRIGHTNESS: 80, DAMPING: 90, POSITION: 64,
        EXCITATION: 100, DECAY: 90, WET: 40, REVERB_DAMPING: 64, DIFFUSION: 80,
        INPUT_GAIN: 51, MASTER: 127}
PHASES = ['wechsel', 'ruhig', 'regler2', 'regler8', 'stress']


class Kit:
    """Thin MIDI wrapper; the self-test substitutes a fake with the same methods."""

    def __init__(self, input_name, output_name):
        import mido
        self.mido = mido
        self.incoming = mido.open_input(input_name)
        self.outgoing = mido.open_output(output_name)

    def cc(self, control, value, channel=0):
        self.outgoing.send(self.mido.Message('control_change', channel=channel,
                                             control=control, value=int(value)))

    def note(self, note, velocity):
        self.outgoing.send(self.mido.Message('note_on', note=note, velocity=velocity))

    def bend(self, value):
        self.outgoing.send(self.mido.Message('pitchwheel', pitch=int(value)))

    def pending(self):
        for message in self.incoming.iter_pending():
            if message.type == 'control_change':
                yield message.channel, message.control, message.value

    def close(self):
        self.incoming.close()
        self.outgoing.close()


def find_ports():
    import mido
    inputs = [n for n in mido.get_input_names() if 'daisy' in n.lower()]
    outputs = [n for n in mido.get_output_names() if 'daisy' in n.lower()]
    if not inputs or not outputs:
        raise SystemExit('Kein Daisy-MIDI-Port gefunden. Ist das Kit an J7 angeschlossen und gestartet?\n'
                         f'Eingänge: {mido.get_input_names()}\nAusgänge: {mido.get_output_names()}')
    return inputs[0], outputs[0]


class Runner:
    def __init__(self, kit, seconds, stress_seconds, settle, writer, clock=time.monotonic, sleep=time.sleep):
        self.kit, self.seconds, self.stress_seconds, self.settle = kit, seconds, stress_seconds, settle
        self.writer, self.clock, self.sleep = writer, clock, sleep
        self.decoder = SnapshotDecoder()
        self.strikes = 0

    def drain(self):
        """Returns the newest complete snapshot among pending messages, or None."""
        latest = None
        for channel, control, value in self.kit.pending():
            sample = self.decoder.feed(channel, control, value)
            if sample is not None:
                latest = sample
        return latest

    def snapshot(self, min_blocks=0, max_blocks=None, request=1, timeout=3.0):
        """Requests until a reply satisfies the block bounds. The firmware ignores a
        request while one reply is outstanding, so a stale reply is skipped and the
        request repeated."""
        start = self.clock()
        next_request = start
        while self.clock() - start < timeout:
            if self.clock() >= next_request:
                self.kit.cc(119, request, 15)
                next_request = self.clock() + 0.25
            sample = self.drain()
            if sample is not None and sample['blocks'] >= min_blocks and (
                    max_blocks is None or sample['blocks'] <= max_blocks):
                return sample
            self.sleep(0.001)
        raise RuntimeError('Keine passende CPU-Antwort vom Kit (3 s). Anderes MIDI-Programm offen?')

    def reset(self):
        # A reset reply carries only a few blocks; anything older means it was ignored.
        self.snapshot(max_blocks=200, request=2)

    def base(self):
        for control, value in BASE.items():
            self.kit.cc(control, value)
        self.kit.bend(0)

    def chord(self, root, velocity=100):
        for offset in (0, 4, 7, 11):
            self.kit.note(root + offset, velocity)

    def play(self, phase, model, duration):
        start = self.clock()
        next_chord = next_control = next_request = start
        rows = []
        while (now := self.clock()) - start < duration:
            t = now - start
            if phase == 'stress':
                if now >= next_chord:
                    for offset in (0, 4, 7, 11):
                        self.kit.note(36 + (self.strikes * 7 + offset) % 60, 127)
                    self.strikes += 1
                    next_chord = now + 0.01
            else:
                if now >= next_chord:
                    self.chord(43 + model * 2 + (self.strikes % 3) * 5)
                    self.strikes += 1
                    next_chord = now + 2.0
                if phase in ('regler2', 'regler8') and now >= next_control:
                    tri = lambda rate: 1 - abs(((t * rate) % 1) * 2 - 1)
                    self.kit.cc(STRUCTURE, 20 + 87 * tri(0.25))
                    self.kit.cc(DAMPING, 30 + 90 * tri(0.17))
                    if phase == 'regler8':
                        self.kit.cc(FREQUENCY, 56 + 16 * tri(0.11))
                        self.kit.cc(BRIGHTNESS, 30 + 90 * tri(0.21))
                        self.kit.cc(POSITION, 10 + 107 * tri(0.13))
                        self.kit.cc(EXCITATION, 60 + 60 * tri(0.19))
                        self.kit.cc(DECAY, 40 + 80 * tri(0.07))
                        self.kit.cc(WET, 20 + 60 * tri(0.09))
                        self.kit.bend(2000 * math.sin(2 * math.pi * 0.5 * t))
                    next_control = now + 0.04  # 25 Hz per track, like dense DAW automation.
            if now >= next_request:
                self.kit.cc(119, 1, 15)
                next_request = now + 1.0
            sample = self.drain()
            if sample is not None:
                rows.append(self.record(phase, model, sample))
            self.sleep(0.001)
        # 1000 blocks/s (48 samples at 48 kHz); require the whole phase in the final reply.
        rows.append(self.record(phase, model, self.snapshot(min_blocks=int(duration * 900))))
        return rows[-1]

    def record(self, phase, model, sample):
        row = dict(utc=datetime.now(timezone.utc).isoformat(), phase=phase, requested_model=model + 1, **sample)
        self.writer.writerow(row)
        return row

    def run(self):
        summary = []
        for model in range(6):
            # Model change: counters reset just before the switch, read after 1 s.
            self.kit.cc(DAMPING, 0)
            self.reset()
            self.kit.cc(MODEL, round(model * 127 / 5))
            self.sleep(1.0)
            results = {'wechsel': self.record('wechsel', model, self.snapshot(min_blocks=900))}
            for phase in PHASES[1:]:
                self.base()
                if phase == 'stress':
                    for control in (BRIGHTNESS, DAMPING, EXCITATION, DECAY, WET, DIFFUSION, INPUT_GAIN, MASTER):
                        self.kit.cc(control, 127)
                    self.kit.cc(STRUCTURE, 64); self.kit.cc(POSITION, 64)
                self.sleep(self.settle)
                self.reset()
                results[phase] = self.play(phase, model, self.stress_seconds if phase == 'stress' else self.seconds)
            for phase in PHASES:
                r = results[phase]
                if r['model'] != model + 1:
                    print(f'WARNUNG: Kit meldet Modell {r["model"]}, erwartet {model + 1} '
                          '(Modellschalter bewegt?)')
                print(f'{MODELS[model]:20s} {phase:8s} Mittel {r["average_percent"]:6.2f} %  '
                      f'Spitze {r["maximum_percent"]:6.2f} %  Fristüberschreitungen {r["overruns"]}/{r["blocks"]}')
                summary.append(dict(model=model + 1, name=MODELS[model], phase=phase,
                                    average_percent=r['average_percent'], maximum_percent=r['maximum_percent'],
                                    overruns=r['overruns'], blocks=r['blocks'], reported_model=r['model']))
        return summary

    def finish(self):
        # Let the resonators decay, then restore a playable state (Master back to 127).
        self.kit.cc(DAMPING, 0)
        self.sleep(3.0)
        self.base()


def compare(paths):
    runs = [json.loads(pathlib.Path(p).read_text(encoding='utf-8')) for p in paths]
    a, b = runs
    print(f'A = {a["label"]} ({a["started"]}), B = {b["label"]} ({b["started"]})')
    print(f'{"Modell":20s} {"Phase":8s} {"Mittel A":>9s} {"Mittel B":>9s} {"Δ":>7s} '
          f'{"Spitze A":>9s} {"Spitze B":>9s} {"Δ":>7s} {"Über A":>7s} {"Über B":>7s}')
    index = {(r['model'], r['phase']): r for r in b['summary']}
    for ra in a['summary']:
        rb = index[(ra['model'], ra['phase'])]
        print(f'{ra["name"]:20s} {ra["phase"]:8s} {ra["average_percent"]:9.2f} {rb["average_percent"]:9.2f} '
              f'{rb["average_percent"] - ra["average_percent"]:+7.2f} {ra["maximum_percent"]:9.2f} '
              f'{rb["maximum_percent"]:9.2f} {rb["maximum_percent"] - ra["maximum_percent"]:+7.2f} '
              f'{ra["overruns"]:7d} {rb["overruns"]:7d}')


class FakeKit:
    """Offline stand-in that answers CC119 like EncodeCpuSnapshot in cpu_telemetry.h."""

    def __init__(self):
        self.model, self.blocks, self.replies, self.sent = 0, 0, [], 0

    def cc(self, control, value, channel=0):
        self.sent += 1
        assert 0 <= int(value) <= 127, (control, value)
        if channel == 0 and control == MODEL:
            self.model = min(5, int(int(value) / 127 * 6))
        if channel == 15 and control == 119:
            self.blocks = 0 if value == 2 else self.blocks + 1000
            load = lambda percent: round(percent * 100)
            values = [1] + digits(load(30 + self.model), 3) + digits(load(40 + self.model), 3) \
                + digits(self.blocks, 5) + digits(0, 5) + [self.model, 4, 64]
            self.replies += [(15, 100 + i, v) for i, v in enumerate(values)]

    def note(self, note, velocity):
        assert 0 <= note <= 127 and 1 <= velocity <= 127
        self.sent += 1

    def bend(self, value):
        assert -8192 <= int(value) <= 8191
        self.sent += 1

    def pending(self):
        replies, self.replies = self.replies, []
        yield from replies


def digits(number, count):
    return [(number >> (7 * i)) & 127 for i in range(count)]


def selftest(out):
    kit = FakeKit()
    path = out / 'selftest.csv'
    with open(path, 'w', newline='', encoding='utf-8') as file:
        writer = csv.DictWriter(file, fieldnames=FIELDS)
        writer.writeheader()
        runner = Runner(kit, 0.2, 0.2, 0.0, writer, sleep=lambda s: time.sleep(min(s, 0.002)))
        summary = runner.run()
        runner.finish()
    assert len(summary) == 6 * len(PHASES)
    assert all(r['reported_model'] == r['model'] for r in summary)
    assert summary[0]['average_percent'] == 30.0 and summary[-1]['maximum_percent'] == 45.0
    print(f'PASS selftest: {len(summary)} phases, {kit.sent} MIDI messages, CSV {path}')


FIELDS = ['utc', 'phase', 'requested_model', 'model', 'voices', 'average_percent',
          'maximum_percent', 'blocks', 'overruns']


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--label', help='Name of the flashed image, e.g. A or B')
    parser.add_argument('--input', help='MIDI input (default: first port containing "Daisy")')
    parser.add_argument('--output', help='MIDI output (default: first port containing "Daisy")')
    parser.add_argument('--seconds', type=float, default=15, help='Seconds per playing phase')
    parser.add_argument('--stress-seconds', type=float, default=30, help='Seconds for the stress phase')
    parser.add_argument('--settle', type=float, default=1.0, help='Seconds before each counter reset')
    parser.add_argument('--compare', nargs=2, metavar='JSON', help='Compare two summaries (A then B)')
    parser.add_argument('--selftest', action='store_true', help='Offline test with a fake kit')
    args = parser.parse_args()
    out = pathlib.Path(__file__).resolve().parent.parent / 'build' / 'hardware-results'
    out.mkdir(parents=True, exist_ok=True)
    if args.selftest:
        return selftest(out)
    if args.compare:
        return compare(args.compare)
    if not args.label:
        parser.error('--label is required (e.g. A or B)')
    input_name, output_name = (args.input, args.output) if args.input and args.output else find_ports()
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S')
    base = out / f'run-{args.label}-{stamp}'
    print(f'Ports: {input_name} / {output_name}; Ergebnis: {base}.csv/.json')
    kit = Kit(input_name, output_name)
    started = datetime.now(timezone.utc).isoformat()
    try:
        with open(f'{base}.csv', 'w', newline='', encoding='utf-8') as file:
            writer = csv.DictWriter(file, fieldnames=FIELDS)
            writer.writeheader()
            runner = Runner(kit, args.seconds, args.stress_seconds, args.settle, writer)
            runner.snapshot()  # Fails early when the telemetry does not answer.
            try:
                summary = runner.run()
            finally:
                runner.finish()
        pathlib.Path(f'{base}.json').write_text(json.dumps(dict(
            label=args.label, started=started, finished=datetime.now(timezone.utc).isoformat(),
            ports=[input_name, output_name], seconds=args.seconds, stress_seconds=args.stress_seconds,
            summary=summary), indent=1), encoding='utf-8')
        print(f'Fertig: {base}.json')
    finally:
        kit.close()


if __name__ == '__main__':
    main()
