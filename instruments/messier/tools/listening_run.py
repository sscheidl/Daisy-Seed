#!/usr/bin/env python
"""Nebulae 0.2.2 listening test: the same 60 s control script for every variant.

Sends CC20-27 (the eight primaries) over USB-MIDI at fixed times, so every recorded
take of every image gets exactly the same settings (docs/LISTENING_TEST_0.2.2.md).
Each send runs in its own process with a timeout (nb_probe.probe), so a stalled kit
cannot hang the script. The owner records; nobody touches the pots during a run.

  python listening_run.py [--port "Daisy Seed"]
"""
from __future__ import annotations

import argparse
import multiprocessing as mp
import sys
import time

import nb_probe

# CC numbers of the primaries: START, SPEED, SIZE, DENSITY, PITCH, OVERLAP, BLEND, WINDOW.
START, SPEED, SIZE, DENSITY, PITCH, OVERLAP, BLEND, WINDOW = range(20, 28)
SEGMENTS = [
    # (seconds from start, label, {cc: value})
    (0, "1 vocoder only, speed 1x",
     {START: 0, SPEED: 64, SIZE: 127, DENSITY: 0, PITCH: 64, OVERLAP: 0, BLEND: 0, WINDOW: 0}),
    (15, "2 vocoder only, near freeze (speed 1.6 %)", {SPEED: 51}),
    (30, "3 vocoder + grains (BLEND centre)", {SPEED: 64, BLEND: 64, DENSITY: 64, OVERLAP: 64}),
    (45, "4 load: PITCH max, grains max", {PITCH: 127, DENSITY: 127, OVERLAP: 127}),
]
LENGTH_S = 60


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="Daisy Seed")
    a = ap.parse_args()
    t0 = time.monotonic()
    failures = 0
    for at, label, values in SEGMENTS:
        while time.monotonic() - t0 < at:
            time.sleep(0.01)
        for cc, value in values.items():
            result = nb_probe.probe(a.port, [0xB0, cc, value], False, 1.0)
            if result[0] != "SENT":
                failures += 1
                print(f"  CC{cc}={value}: {result[0]} {result[1]}")
        print(f"[{time.monotonic() - t0:5.1f} s] segment {label}", flush=True)
    while time.monotonic() - t0 < LENGTH_S:
        time.sleep(0.05)
    print(f"[{time.monotonic() - t0:5.1f} s] end of run{'' if not failures else f', {failures} sends failed'}")
    return 0 if not failures else 2


if __name__ == "__main__":
    mp.freeze_support()
    sys.exit(main())
