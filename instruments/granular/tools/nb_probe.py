#!/usr/bin/env python
"""Nebulae on Daisy: control SysEx probe over USB-MIDI (docs/CONTROL_PROTOCOL.md).

Every probe runs in a child process with a timeout, because a stalled kit makes Windows
MIDI (WinMM) block or throw inside the driver; the parent survives and reports TIMEOUT.

  python nb_probe.py ports
  python nb_probe.py status [--repeat N --interval S]
  python nb_probe.py profile [--reset] [--repeat N --interval S]   (diagnostic build only)
  python nb_probe.py cc <controller> <value>                        (CC20-27 move the primaries)

Exit codes: 0 reply parsed, 2 timeout or send failure, 3 no port, 4 unexpected reply.
Needs python-rtmidi. Hardware statements stay the human's: this tool only reads counters.
"""
from __future__ import annotations

import argparse
import multiprocessing as mp
import sys
import time

PREFIX = [0xF0, 0x7D, 0x4E, 0x42, 0x01]
CMD_STATUS, CMD_PROFILE, CMD_ERROR, REPLY = 0x01, 0x03, 0x7F, 0x40
LOAD_ERRORS = {0: "none", 1: "missing", 2: "invalid WAV", 3: "too large", 4: "I/O", 5: "no card"}
PROFILE_NAMES = [
    "clock_hz", "deadline_cycles", "blocks", "total_peak", "vocoder_peak", "grains_peak",
    "other_peak", "fill_peak", "fft_peak", "phase_peak", "hop_blocks", "hop_vocoder_min",
    "skipped_blocks", "overruns", "bench_sdram_hop", "bench_sram_hop", "bench_sdram_plain",
    "bench_sram_plain", "engine_placement", "last_total",
]


def find_ports(stem: str):
    import rtmidi
    outs, ins = rtmidi.MidiOut().get_ports(), rtmidi.MidiIn().get_ports()
    out = next((i for i, n in enumerate(outs) if stem.lower() in n.lower()), None)
    inp = next((i for i, n in enumerate(ins) if stem.lower() in n.lower()), None)
    return outs, ins, out, inp


def child(stem: str, message: list[int], want_reply: bool, timeout: float, queue) -> None:
    """Send one message, collect one Nebulae SysEx reply. Runs in its own process."""
    try:
        import rtmidi
        outs, ins, out_index, in_index = find_ports(stem)
        if out_index is None or (want_reply and in_index is None):
            queue.put(("NOPORT", f"out={outs} in={ins}"))
            return
        out = rtmidi.MidiOut()
        out.open_port(out_index)
        inp = None
        if want_reply:
            inp = rtmidi.MidiIn()
            inp.open_port(in_index)
            inp.ignore_types(sysex=False, timing=True, active_sense=True)
        t0 = time.monotonic()
        out.send_message(message)
        if not want_reply:
            queue.put(("SENT", ""))
            return
        buf = bytearray()
        while time.monotonic() - t0 < timeout:
            item = inp.get_message()
            if item is None:
                time.sleep(0.002)
                continue
            for b in item[0]:
                if b == 0xF0:
                    buf = bytearray([b])
                elif buf:
                    buf.append(b)
                    if b == 0xF7:
                        # Only the answer to this request: another client on the same port
                        # (panel, editor, a second probe) gets its own NB replies there too.
                        if (list(buf[:5]) == PREFIX and len(buf) > 6
                                and buf[5] in (message[5] | REPLY, CMD_ERROR | REPLY)
                                and (buf[5] != CMD_ERROR | REPLY or buf[6] == message[5])):
                            queue.put(("REPLY", bytes(buf), round(time.monotonic() - t0, 3)))
                            return
                        buf = bytearray()
        queue.put(("TIMEOUT", "no reply"))
    except Exception as exc:  # WinMM errors on a stalled device land here
        queue.put(("ERROR", f"{type(exc).__name__}: {exc}"))


def probe(stem: str, message: list[int], want_reply: bool, timeout: float):
    queue: mp.Queue = mp.Queue()
    proc = mp.Process(target=child, args=(stem, message, want_reply, timeout, queue), daemon=True)
    proc.start()
    proc.join(timeout + 2.0)
    if proc.is_alive():
        proc.terminate()
        proc.join(1.0)
        return ("TIMEOUT", "child process stuck in the MIDI driver (device stalled?)")
    return queue.get() if not queue.empty() else ("ERROR", "child exited without a result")


def mask(b: bytes) -> int:
    return b[0] | b[1] << 7 | (b[2] & 3) << 14


def show_status(d: bytes) -> None:
    if len(d) != 19:
        print(f"UNEXPECTED STATUS length {len(d)}: {d.hex(' ')}")
        return
    flags = d[4]
    names = [n for bit, n in enumerate(["card", "live", "recording", "loading", "OVERRUN", "scanning"]) if flags >> bit & 1]
    print(f"STATUS fw {d[0]}.{d[1]}.{d[2]} personality {d[3]} flags [{' '.join(names)}] "
          f"bank {d[5]}/{d[6]} playing {d[7]}:{d[8]} banks 0x{mask(d[9:12]):04x} slots 0x{mask(d[12:15]):04x} "
          f"load_error {LOAD_ERRORS.get(d[15], d[15])} overruns {d[16] | d[17] << 7} peak {d[18]} %")


def show_profile(d: bytes) -> None:
    if len(d) != 5 * len(PROFILE_NAMES):
        print(f"UNEXPECTED PROFILE length {len(d)}: {d.hex(' ')}")
        return
    v = {}
    for i, name in enumerate(PROFILE_NAMES):
        v[name] = sum(d[5 * i + k] << (7 * k) for k in range(5))
    clock, deadline = v["clock_hz"], v["deadline_cycles"] or 1
    placement = {0xC10: "SDRAM", 0xC00: "SDRAM", 0x240: "AXI SRAM", 0x200: "DTCM", 0x300: "RAM_D2"}
    print(f"PROFILE clock {clock / 1e6:.0f} MHz, block deadline {deadline} cycles = {deadline / clock * 1e3:.3f} ms, "
          f"blocks {v['blocks']}, hop blocks {v['hop_blocks']}, skipped {v['skipped_blocks']}, overruns {v['overruns']}, "
          f"engine in {placement.get(v['engine_placement'], hex(v['engine_placement']))}")
    for name in ["total_peak", "last_total", "vocoder_peak", "hop_vocoder_min", "fill_peak", "fft_peak",
                 "phase_peak", "grains_peak", "other_peak", "bench_sdram_hop", "bench_sram_hop",
                 "bench_sdram_plain", "bench_sram_plain"]:
        c = v[name]
        print(f"  {name:18s} {c:>10d} cycles  {c / clock * 1e3:8.3f} ms  {100 * c / deadline:6.1f} % of deadline")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="Daisy Seed", help="substring of the MIDI port name")
    ap.add_argument("--timeout", type=float, default=1.0, help="seconds to wait for the reply")
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--interval", type=float, default=1.0)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ports")
    sub.add_parser("status")
    p = sub.add_parser("profile")
    p.add_argument("--reset", action="store_true", help="clear the peaks after this reply")
    c = sub.add_parser("cc")
    c.add_argument("controller", type=int)
    c.add_argument("value", type=int)
    a = ap.parse_args()

    if a.cmd == "ports":
        outs, ins, out_index, in_index = find_ports(a.port)
        print("out:", outs)
        print("in: ", ins)
        print(f"match {a.port!r}: out {out_index} in {in_index}")
        return 0 if out_index is not None else 3
    if a.cmd == "cc":
        message, want = [0xB0, a.controller & 0x7F, a.value & 0x7F], False
    elif a.cmd == "status":
        message, want = PREFIX + [CMD_STATUS, 0xF7], True
    else:
        message, want = PREFIX + [CMD_PROFILE] + ([0x01] if a.reset else []) + [0xF7], True

    worst = 0
    for i in range(a.repeat):
        if i:
            time.sleep(a.interval)
        result = probe(a.port, message, want, a.timeout)
        stamp = time.strftime("%H:%M:%S")
        if result[0] == "REPLY":
            frame = result[1]
            cmd, data = frame[5], frame[6:-1]
            print(f"[{stamp}] reply after {result[2]} s:", end=" ")
            if cmd == CMD_STATUS | REPLY:
                show_status(data)
            elif cmd == CMD_PROFILE | REPLY:
                show_profile(data)
            elif cmd == CMD_ERROR | REPLY:
                print(f"ERROR for command 0x{data[0]:02x} code {data[1]} (1 version, 2 unknown, 3 length)")
                worst = max(worst, 4)
            else:
                print(f"unexpected command 0x{cmd:02x}: {frame.hex(' ')}")
                worst = max(worst, 4)
        else:
            print(f"[{stamp}] {result[0]}: {result[1]}")
            worst = max(worst, {"NOPORT": 3}.get(result[0], 2))
    return worst


if __name__ == "__main__":
    mp.freeze_support()
    sys.exit(main())
