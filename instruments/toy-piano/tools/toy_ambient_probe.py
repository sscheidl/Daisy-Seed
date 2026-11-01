#!/usr/bin/env python3
"""CPU load of Toy Piano + Ambient at one VR8 (Stretch) position, over USB MIDI.

Plays notes into the kit and reads the Mod on Daisy callback statistics
(GET_STATS/RESET_STATS, modules/mod-on-daisy/tools/mod_probe.py). VR8 is a
physical pot: set it by hand before each run and pass its position as --label.

    python tools/toy_ambient_probe.py --label vr8-links
"""
import argparse
import json
import pathlib
import sys
import time
from datetime import datetime, timezone

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'modules' / 'mod-on-daisy' / 'tools'))
import mod_probe  # noqa: E402

PHASES = [  # name, seconds, notes per strike, seconds between strikes
    ('ruhig', 15, 4, 2.0),
    ('akkorde', 15, 8, 0.5),
    ('dicht', 15, 8, 0.1),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--label', required=True, help='VR8 position, e.g. vr8-links or vr8-rechts')
    parser.add_argument('--port', help='MIDI output port (default: first containing "Daisy")')
    args = parser.parse_args()
    dev = mod_probe.Device(args.port)
    info = mod_probe.identify(dev)
    print(f'{info["instrument"]} v{info["version"]}')
    if 'ambient' not in info['instrument']:
        sys.exit('Firmware is not toy-piano-ambient')
    mido = dev.mido
    results, strikes = [], 0
    try:
        for name, seconds, count, interval in PHASES:
            dev.request(mod_probe.CMD['reset_stats'])
            send_errors = 0
            start = time.time()
            next_strike = start
            while time.time() - start < seconds:
                if time.time() >= next_strike:
                    root = 48 + (strikes * 5) % 24
                    for offset in (0, 4, 7, 11, 12, 16, 19, 23)[:count]:
                        try:
                            dev.out.send(mido.Message('note_on', note=root + offset, velocity=110))
                        except Exception:  # WinMM rejects sends while the kit's USB stalls.
                            send_errors += 1
                    strikes += 1
                    next_strike += interval
                time.sleep(0.002)
            time.sleep(1.2)  # load_avg is smoothed at 1 Hz; let the snapshot settle.
            s = mod_probe.stats(dev)
            row = dict(phase=name, load_avg=s['load_avg'] / 100, load_max=s['load_max'] / 100,
                       overruns=s['overruns'], blocks=s['ticks'], midi_send_errors=send_errors)
            results.append(row)
            print(f'{name:8s} Mittel {row["load_avg"]:6.2f} %  Spitze {row["load_max"]:6.2f} %  '
                  f'Fristüberschreitungen {row["overruns"]}/{row["blocks"]}  MIDI-Sendefehler {send_errors}')
    finally:
        for note in range(128):
            try:
                dev.out.send(mido.Message('note_off', note=note))
            except Exception:
                pass
        # Open rtmidi ports keep the interpreter alive on Windows.
        dev.out.close()
        dev.inp.close()
    out = pathlib.Path(__file__).resolve().parent.parent / 'build' / 'hardware-results'
    out.mkdir(parents=True, exist_ok=True)
    path = out / f'{args.label}-{datetime.now().strftime("%Y%m%d-%H%M%S")}.json'
    path.write_text(json.dumps(dict(label=args.label, firmware=info, utc=datetime.now(timezone.utc).isoformat(),
                                    phases=results), indent=1), encoding='utf-8')
    print(f'Ergebnis: {path}')


if __name__ == '__main__':
    main()
