#!/usr/bin/env python3
"""Prints the callback load once per second (counters reset each second), no notes sent.

    python tools/live_load.py --seconds 40
"""
import argparse
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / 'modules' / 'mod-on-daisy' / 'tools'))
import mod_probe  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument('--seconds', type=int, default=40)
args = parser.parse_args()
dev = mod_probe.Device()
try:
    for second in range(args.seconds):
        dev.request(mod_probe.CMD['reset_stats'])
        time.sleep(1.0)
        s = mod_probe.stats(dev)
        print(f'{second + 1:3d} s  Mittel {s["load_avg"] / 100:6.2f} %  Spitze {s["load_max"] / 100:6.2f} %  '
              f'zu spät {s["overruns"]}/{s["ticks"]}', flush=True)
finally:
    dev.out.close()
    dev.inp.close()
