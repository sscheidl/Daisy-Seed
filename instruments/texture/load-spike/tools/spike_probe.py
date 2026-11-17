#!/usr/bin/env python3
"""Readout of the texture load spike over USB-MIDI (../README.md).

    pip install mido python-rtmidi
    python spike_probe.py identify               # variant, clock, block, budget, product load
    python spike_probe.py config --block 48 --product on
    python spike_probe.py run-all --md out.md    # start all four cases, wait, print and save the table
    python spike_probe.py run-all --midi-load    # ... while sending CC1 and channel pressure (product load)
    python spike_probe.py series --md out.md     # blocks 12 and 48, each without and with product load
    python spike_probe.py results [--md out.md]  # results of the cases that are done
    python spike_probe.py profile 1              # per-point peaks of case a (1..4)

Everything is plain control change on channel 16 (firmware src/main.cpp HandleCc):
CC119 = 1..4 result of case a..d, 5 identity, 6..9 product load of case a..d; CC118 = 1..4 start a
case, 5 run all; CC117 = 1..4 per-point profile; CC116 = 1/2 block 12/48, 3/4 product load off/on.
The port defaults to the first one containing "Daisy".
"""
import argparse
import sys
import time

CASES = ['a granular 16 bit', 'b delay 16 bit', 'c dirty tape 48k 2:1', 'd synth']
STATES = {0: 'not run', 1: 'running', 2: 'done', 3: 'failed to start'}
# Must match src/spike.h (kWarmupBlocks, kPointBlocks, PointCount).
WARMUP_S, POINT_S, POINTS = 2.0, 1.0, [72, 48, 72, 72]
DENSITY = [0.0, 0.2, 0.4, 0.6, 0.8, 1.0]


def point(case, i):
    """Sweep point i of a case (0-based case), as src/spike.h GetPoint."""
    amount, pitch = [0, -1][i % 2], [0, 24][(i // 2) % 2]
    if case == 1:
        return dict(density=DENSITY[(i // 8) % 6], size=1.0, pitch=pitch, time=[0.2, 0.9][(i // 4) % 2], amount=amount)
    return dict(density=DENSITY[(i // 12) % 6], size=[0.0, 0.5, 0.97][(i // 4) % 3], pitch=pitch, time=0.5, amount=amount)


def describe(case, i):
    p = point(case, i)
    return (f"density {p['density']:.1f}, size {p['size']:.2f}, pitch {p['pitch']:+d}, "
            f"time {p['time']:.1f}, time random {'on' if p['amount'] else 'off'}")


class Device:
    def __init__(self, port_name=None):
        import mido
        self.mido = mido
        outs, ins = mido.get_output_names(), mido.get_input_names()
        port_name = port_name or next((n for n in outs if 'Daisy' in n), None)
        if port_name is None:
            sys.exit('no MIDI port with "Daisy" in its name; use --port (outputs: %s)' % outs)
        in_name = next((n for n in ins if n.split(' ')[0] == port_name.split(' ')[0] and 'Daisy' in n), port_name)
        try:
            self.out = mido.open_output(port_name)
            self.inp = mido.open_input(in_name)
        except OSError as e:
            sys.exit(f'cannot open {port_name}: {e} (is the port open in a DAW?)')

    def cc(self, control, value):
        self.out.send(self.mido.Message('control_change', channel=15, control=control, value=value))

    def replies(self, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for msg in self.inp.iter_pending():
                if msg.type == 'control_change' and msg.channel == 15:
                    yield msg
            time.sleep(0.002)

    def block(self, value):
        """One 20-CC reply (CC100..119) to CC119 = value."""
        for _ in self.inp.iter_pending():
            pass
        self.cc(119, value)
        values = {}
        for msg in self.replies(1.0):
            if 100 <= msg.control <= 119:
                values[msg.control] = msg.value
                if msg.control == 119:
                    break
        if values.get(119) != 64:
            sys.exit('no reply from the spike firmware (is TextureSpikeA/B flashed and the port free?)')
        return lambda cc, n=1: sum(values.get(cc + i, 0) << (7 * i) for i in range(n))


def identify(dev):
    v = dev.block(5)
    if v(100) != 16:
        sys.exit('unexpected identity format %d' % v(100))
    return dict(variant=chr(v(101)) + ('I' if v(115) else ''), clock_mhz=v(102, 2), block=v(104),
                budget_cycles=v(105, 3), version=f'{v(108)}.{v(109)}.{v(110)}', wrong_block_size=v(111, 3),
                product=bool(v(114)), patch_status=v(116))


def result(dev, case):
    v = dev.block(case + 1)
    if v(100) != 17 or v(113) != case + 1:
        sys.exit('unexpected result format')
    flags = v(118)
    return dict(state=STATES.get(flags & 7, '?'), peak_cycles=v(101, 3), avg_cycles=v(104, 3), peak=v(107, 3) / 100,
                avg=v(114, 3) / 100, overruns=v(110, 3), worst=v(117), product=bool(flags & 8),
                block=48 if flags & 16 else 12)


def product_result(dev, case):
    v = dev.block(case + 6)
    if v(100) != 18 or v(113) != case + 1:
        sys.exit('unexpected product result format')
    return dict(control_peak=v(101, 3), control_mean=v(104, 3), control_ticks=v(107, 3), midi_events=v(110, 3),
                main_peak=v(114, 3), product=bool(v(117)), block=v(118))


def wait_idle(dev, limit_s=400):
    """A run started earlier keeps going on the kit even if this script stopped; wait for it."""
    end = time.time() + limit_s
    while any(result(dev, c)['state'] == 'running' for c in range(4)):
        if time.time() > end:
            sys.exit('kit still busy')
        time.sleep(5)


def configure(dev, block=None, product=None):
    wait_idle(dev)
    if block is not None:
        dev.cc(116, 1 if block == 12 else 2)
        time.sleep(0.3)  # audio restart
    if product is not None:
        dev.cc(116, 4 if product else 3)
        time.sleep(0.1)
    info = identify(dev)
    if (block is not None and info['block'] != block) or (product is not None and info['product'] != product):
        sys.exit(f'configuration not applied: {info}')
    return info


def wait_run_all(dev, midi_load):
    """Start all four cases and wait. With midi_load, send CC1 and channel pressure on channel 1
    at about 200 messages per second, which the matrix uses as sources; otherwise stay silent.
    A message Windows refuses (WinMM error, seen 2026-11-16) is counted and skipped."""
    total = sum(WARMUP_S + n * POINT_S for n in POINTS)
    dev.cc(118, 5)
    print(f'running all four cases, about {total / 60:.1f} min; do not touch the kit', flush=True)
    end = time.time() + total + 5
    i = sent = failed = 0
    while time.time() < end:
        if midi_load:
            value = (i * 7) % 128
            for msg in (dev.mido.Message('control_change', channel=0, control=1, value=value),
                        dev.mido.Message('aftertouch', channel=0, value=127 - value)):
                try:
                    dev.out.send(msg)
                    sent += 1
                except Exception:  # rtmidi raises its own SystemError type
                    failed += 1
                    time.sleep(0.05)
            i += 1
            time.sleep(0.01)
        else:
            time.sleep(0.5)
    if midi_load:
        print(f'MIDI stream: {sent} messages sent, {failed} refused by Windows', flush=True)
    for _ in range(60):
        if all(result(dev, c)['state'] != 'running' for c in range(4)):
            return
        time.sleep(2)


def table(info, rows):
    lines = [f"Variant {info['variant']}, firmware {info['version']}, {info['clock_mhz']} MHz, block {info['block']}, "
             f"budget {info['budget_cycles']} cycles, product load {'on' if info['product'] else 'off'}, "
             f"wrong block sizes {info['wrong_block_size']}",
             '', '| Case | State | Peak load | Mean load | Peak cycles | Overruns | Worst point |',
             '| --- | --- | --- | --- | --- | --- | --- |']
    for c, r, _ in rows:
        worst = f"{r['worst']}: {describe(c, r['worst'])}" if r['state'] == 'done' else ''
        lines.append(f"| {CASES[c]} | {r['state']} | {r['peak']:.2f} % | {r['avg']:.2f} % | {r['peak_cycles']} | "
                     f"{r['overruns']} | {worst} |")
    if any(pr and pr['product'] for _, _, pr in rows):
        # The control timer runs once per millisecond; its share is given against 1 ms and against one
        # audio block. Callback peak + control peak per block is a conservative worst case.
        clock = info['clock_mhz'] * 1000
        lines += ['', '| Case | Control tick peak / mean (of 1 ms) | Callback peak + control peak (of one block) | '
                  'MIDI events | Main loop MIDI peak |', '| --- | --- | --- | --- | --- |']
        for c, r, pr in rows:
            budget = r['block'] * info['clock_mhz'] * 1000 // 48
            both = 100 * (r['peak_cycles'] + pr['control_peak']) / budget if budget else 0
            lines.append(f"| {CASES[c]} | {100 * pr['control_peak'] / clock:.2f} % / {100 * pr['control_mean'] / clock:.2f} % "
                         f"({pr['control_ticks']} ticks) | {both:.2f} % | {pr['midi_events']} | "
                         f"{pr['main_peak']} cycles |")
    return '\n'.join(lines)


def profile(dev, case):
    for _ in dev.inp.iter_pending():
        pass
    dev.cc(117, case + 1)
    points, current = {}, {}
    for msg in dev.replies(5.0):
        if msg.control == 96:
            current = {'i': msg.value}
        elif msg.control in (97, 98, 99) and 'i' in current:
            current[msg.control] = msg.value
            if msg.control == 99:
                points[current['i']] = current[97] | current[98] << 7 | current[99] << 14
        elif msg.control == 95:
            break
    return points


def main():
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port')
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('identify')
    p = sub.add_parser('config')
    p.add_argument('--block', type=int, choices=[12, 48])
    p.add_argument('--product', choices=['on', 'off'])
    for name in ('results', 'run-all', 'series'):
        p = sub.add_parser(name)
        p.add_argument('--md', help='also write the table(s) to this Markdown file')
        if name == 'run-all':
            p.add_argument('--midi-load', action='store_true')
    p = sub.add_parser('profile')
    p.add_argument('case', type=int, choices=[1, 2, 3, 4])
    args = ap.parse_args()
    dev = Device(args.port)
    info = identify(dev)
    if args.cmd == 'identify':
        print(info)
        return
    if args.cmd == 'config':
        print(configure(dev, args.block, None if args.product is None else args.product == 'on'))
        return
    if args.cmd == 'series':
        texts = []
        for block in (12, 48):
            for product in (False, True):
                info = configure(dev, block, product)
                print(f'== block {block}, product load {"on" if product else "off"}', flush=True)
                wait_run_all(dev, product)
                rows = [(c, result(dev, c), product_result(dev, c)) for c in range(4)]
                texts.append(table(info, rows))
                print(texts[-1], flush=True)
                if args.md:
                    with open(args.md, 'w', encoding='utf-8') as f:
                        f.write('\n\n'.join(texts) + '\n')
        configure(dev, 12, False)
        return
    if args.cmd == 'profile':
        c = args.case - 1
        pts = profile(dev, c)
        if not pts:
            sys.exit('no profile: is the case done?')
        for i, cycles in sorted(pts.items(), key=lambda kv: -kv[1]):
            print(f"{i:3d} {100 * cycles / info['budget_cycles']:6.2f} %  {describe(c, i)}")
        return
    if args.cmd == 'run-all':
        wait_run_all(dev, args.midi_load)
    rows = [(c, result(dev, c), product_result(dev, c)) for c in range(4)]
    text = table(info, rows)
    print(text)
    if args.md:
        with open(args.md, 'w', encoding='utf-8') as f:
            f.write(text + '\n')


if __name__ == '__main__':
    main()
