#!/usr/bin/env python3
"""Mod on Daisy probe: SysEx tests and bench measurements (TEST_PLAN_PHASE2.md).

Not the editor. A small command-line client for the protocol in docs/PROTOCOL.md.

    pip install mido python-rtmidi
    python mod_probe.py ports
    python mod_probe.py identify
    python mod_probe.py modes                       # instrument modes (capabilities 2 bit 0)
    python mod_probe.py matrix
    python mod_probe.py set-cell LFO1 0 35          # source, target column, depth
    python mod_probe.py source LFO1 sync 1          # MIDI clock
    python mod_probe.py lengths                     # H2/H3: frame lengths in both directions
    python mod_probe.py monitor --seconds 10        # live values, raw CV (H5)
    python mod_probe.py bench --csv results.csv     # scenarios S0..S8 (bench firmware)
    python mod_probe.py cc-stats                    # plain-CC fallback of the bench
    python mod_probe.py selftest                    # codec check, no MIDI port needed

The port defaults to the first one containing "Daisy"; use --port to choose.
"""
import argparse
import csv
import random
import re
import sys
import time

MANUFACTURER, PROJECT, VERSION = 0x7D, (0x4D, 0x44), 1
MAX_FRAME, HEADER = 120, 8
CMD = dict(identify=0x01, get_target=0x02, get_allowed=0x03, get_mode=0x04, get_matrix=0x10,
           set_matrix=0x11, set_cell=0x12, clear_matrix=0x13, get_source=0x18, set_source=0x19, get_name=0x1A,
           set_name=0x1B, get_calibration=0x24, set_calibration=0x25, monitor=0x30, get_stats=0x31,
           reset_stats=0x32, set_scenario=0x33)
MONITOR_FRAME, NAK = 0x7E, 0x7F  # 0x70 is the reply to MONITOR
SOURCES = ['LFO1', 'LFO2', 'RANDOM', 'CV1', 'CV2', 'MODWHEEL', 'CHAT', 'POLYAT']
SOURCE_PARAMS = ['rate', 'shape', 'sync', 'gate', 'division', 'retrigger', 'fm']  # fm: -100..100, LFO cross modulation (capability bit 6)
ERRORS = {1: 'checksum', 2: 'length', 3: 'unknown command', 4: 'range', 5: 'busy', 6: 'version',
          7: 'limit of active connections', 8: 'cell not allowed', 9: 'not implemented', 10: 'storage',
          11: 'schema'}
STATS = ['ticks', 'cycles_min', 'cycles_avg', 'cycles_max', 'load_avg', 'load_max', 'overruns',
         'midi_lost', 'dispatch_max', 'service_max']


# ---------------------------------------------------------------- codec (mirror of protocol.h)
def checksum(data):
    return (128 - (sum(data) & 127)) & 127


def build_frame(device, command, seq, payload=b''):
    body = bytes([VERSION, device & 127, command & 127, seq & 127, len(payload)]) + bytes(payload)
    return bytes([0xF0, MANUFACTURER, *PROJECT]) + body + bytes([checksum(body), 0xF7])


def parse_body(body):
    """body = bytes between F0 and F7. Returns (command, seq, payload) or None."""
    if len(body) < HEADER + 1 or body[0] != MANUFACTURER or tuple(body[1:3]) != PROJECT:
        return None
    length = body[7]
    if len(body) != HEADER + length + 1 or checksum(body[3:HEADER + length]) != body[HEADER + length]:
        raise ValueError('reply with bad length or checksum: ' + body.hex(' '))
    return body[5], body[6], bytes(body[HEADER:HEADER + length])


class Writer:
    def __init__(self):
        self.data = bytearray()

    def u7(self, v):
        self.data.append(v & 127)
        return self

    def u14(self, v):
        return self.u7(v).u7(v >> 7)

    def u32(self, v):
        for i in range(5):
            self.u7(v >> (7 * i))
        return self

    def str(self, s, limit):
        s = s.encode('ascii')[:limit]
        self.u7(len(s))
        self.data += s
        return self

    def packed(self, data):
        for g in range(0, len(data), 7):
            group = data[g:g + 7]
            self.u7(sum(((b >> 7) & 1) << i for i, b in enumerate(group)))
            for b in group:
                self.u7(b)
        return self

    def mask64(self, m):
        for i in range(10):
            self.u7(m >> (7 * i))
        return self


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def u7(self):
        v = self.data[self.pos] & 127
        self.pos += 1
        return v

    def u14(self):
        return self.u7() | (self.u7() << 7)

    def u32(self):
        return sum(self.u7() << (7 * i) for i in range(5)) & 0xFFFFFFFF

    def str(self):
        n = self.u7()
        s = bytes(self.data[self.pos:self.pos + n]).decode('ascii', 'replace')
        self.pos += n
        return s

    def packed(self, n):
        out = []
        while len(out) < n:
            msbs = self.u7()
            for i in range(min(7, n - len(out))):
                out.append(self.u7() | (((msbs >> i) & 1) << 7))
        return out

    def mask64(self):
        return sum(self.u7() << (7 * i) for i in range(10))


def signed(v):
    return ((v & 16383) - 8192) / 8191.0


def crc32(data):
    import zlib
    return zlib.crc32(bytes(data)) & 0xFFFFFFFF


def serialize_patch(depth, enabled, lfo, rnd, name, fmt=1):
    """Same byte layout as Serialize() in patch.h (109 bytes; format 2: + fm of both LFOs)."""
    b = bytearray([fmt])
    for s in range(8):
        for t in range(8):
            b.append(depth[s][t] & 0xFF)
    b += bytes(enabled)
    for l in lfo:
        b += bytes([l['rate'] & 0xFF, l['rate'] >> 8, l['shape'], l['sync'], l['gate'], l['division'],
                    l['retrigger']])
    b += bytes([rnd['rate'] & 0xFF, rnd['rate'] >> 8, rnd['mode'], rnd['sync'], rnd['gate'], rnd['division']])
    b += name.encode('ascii')[:16].ljust(16, b'\0')
    if fmt == 2:
        b += bytes([l.get('fm', 0) & 0xFF for l in lfo])
    return bytes(b)


def selftest():
    w = Writer().packed([0x80, 0x00, 0xFF, 0x7F, 0x9C, 0x64, 0x01, 0xC8]).mask64((1 << 63) | 5)
    r = Reader(w.data)
    assert r.packed(8) == [0x80, 0x00, 0xFF, 0x7F, 0x9C, 0x64, 0x01, 0xC8] and r.mask64() == (1 << 63) | 5
    frame = build_frame(0, CMD['get_name'], 5)
    assert parse_body(frame[1:-1]) == (CMD['get_name'], 5, b'')
    assert len(build_frame(0, CMD['set_matrix'], 1, bytes(84))) == 95
    # Test vector of test_mod.cpp: LFO1->0 +35, ChannelAT->4 -60, name "Vector".
    depth = [[0] * 8 for _ in range(8)]
    depth[0][0], depth[6][4] = 35, -60
    enabled = [0] * 8
    enabled[0], enabled[6] = 1, 1 << 4
    lfo = [dict(rate=6000, shape=0, sync=0, gate=0, division=4, retrigger=0) for _ in range(2)]
    rnd = dict(rate=8000, mode=0, sync=0, gate=0, division=4)
    patch = serialize_patch(depth, enabled, lfo, rnd, 'Vector')
    assert len(patch) == 109
    assert crc32(patch) == 0xE2340CC6, hex(crc32(patch))
    # Format 2 (LFO cross modulation), reference values of patch.h with MOD_ON_DAISY_EXTENSIONS.
    lfo[0]['fm'], lfo[1]['fm'] = -37, 100
    patch2 = serialize_patch(depth, enabled, lfo, rnd, 'Vector', fmt=2)
    assert len(patch2) == 111 and crc32(patch2) == 0x9D6E020E, hex(crc32(patch2))
    default2 = serialize_patch([[0] * 8 for _ in range(8)], [0] * 8,
                               [dict(rate=6000, shape=0, sync=0, gate=0, division=4, retrigger=0)] * 2,
                               rnd, '', fmt=2)
    assert crc32(default2) == 0xC422B385, hex(crc32(default2))
    # Port pairing: Windows indices differ per direction (hardware test of 24.10.2026),
    # ALSA appends client:port, macOS uses the plain name.
    win_out = ['Microsoft GS Wavetable Synth 0', 'Tulip 1', 'Daisy Seed External 19']
    win_in = ['Tulip 0', 'General MIDI Synth 17', 'Daisy Seed External 18']
    assert matching_input('Daisy Seed External 19', win_out, win_in) == 'Daisy Seed External 18'
    assert resolve_port('Daisy Seed External', win_out) == 'Daisy Seed External 19'
    assert resolve_port('Daisy Seed External 18', win_out) == 'Daisy Seed External 19'
    two_out, two_in = ['Daisy Seed External 3', 'Daisy Seed External 4'], ['Daisy Seed External 1', 'Daisy Seed External 2']
    assert matching_input('Daisy Seed External 4', two_out, two_in) == 'Daisy Seed External 2'
    alsa = ['Daisy Seed External:Daisy Seed External MIDI 1 20:0']
    assert matching_input(alsa[0], alsa, alsa) == alsa[0]
    assert matching_input('Daisy Seed External', ['Daisy Seed External'], ['Daisy Seed External']) == 'Daisy Seed External'
    print('selftest passed')


# ---------------------------------------------------------------- device link
def port_base(name):
    # rtmidi decorates port names per platform: Windows appends an index that differs
    # between inputs and outputs ('Daisy Seed External 18' / '... 19'), ALSA appends
    # ':port client:port'. The base is the name without these decorations.
    return re.sub(r'\s+\d+$', '', name.split(':')[0]).strip()


def resolve_port(wanted, names):
    # Exact name first, otherwise the first port with the same base name.
    if wanted in names:
        return wanted
    return next((n for n in names if port_base(n) == port_base(wanted)), None)


def matching_input(out_name, out_names, in_names):
    # The k-th output of a base name pairs with the k-th input of that base name, so two
    # kits with the same name keep their own ports.
    base = port_base(out_name)
    outs = [n for n in out_names if port_base(n) == base]
    ins = [n for n in in_names if port_base(n) == base]
    if not ins:
        return next((n for n in in_names if out_name in n), None)
    k = outs.index(out_name) if out_name in outs else 0
    return ins[min(k, len(ins) - 1)]


class Device:
    def __init__(self, port_name=None, device=0, timeout=0.25):
        import mido
        self.mido = mido
        names = mido.get_output_names()
        if port_name is None:
            port_name = next((n for n in names if 'Daisy' in n), None)
        else:
            port_name = resolve_port(port_name, names) or port_name
        if port_name is None:
            sys.exit('no MIDI port with "Daisy" in its name; use --port (see "ports")')
        in_name = matching_input(port_name, names, mido.get_input_names()) or port_name
        try:
            self.out = mido.open_output(port_name)
            self.inp = mido.open_input(in_name)
        except OSError as e:
            sys.exit(f'cannot open {port_name}: {e}\n(Windows: is the port open in Studio One? TEST_PLAN H4)')
        # Random start: a new invocation should not reuse the previous one's sequence numbers.
        self.device, self.timeout, self.seq = device, timeout, random.randrange(128)
        self.monitor = []

    def send_raw(self, frame):
        self.out.send(self.mido.Message('sysex', data=list(frame[1:-1])))

    def receive(self, deadline):
        while time.time() < deadline:
            for msg in self.inp.iter_pending():
                if msg.type != 'sysex':
                    yield msg
                    continue
                parsed = parse_body(bytes(msg.data))
                if parsed is None:
                    continue
                if parsed[0] == MONITOR_FRAME:
                    self.monitor.append((time.time(), parsed[2]))
                    continue
                yield parsed
            time.sleep(0.0005)

    def request(self, command, payload=b'', device=None, retries=3):
        self.seq = (self.seq + 1) & 127
        frame = build_frame(self.device if device is None else device, command, self.seq, payload)
        for _ in range(retries + 1):
            self.send_raw(frame)
            for reply in self.receive(time.time() + self.timeout):
                if isinstance(reply, tuple) and reply[1] == self.seq:
                    cmd, _, data = reply
                    if cmd == NAK:
                        raise RuntimeError(f'NAK {data[1]} ({ERRORS.get(data[1], "?")}) for command {data[0]:#04x}')
                    return data
        raise TimeoutError(f'no reply to command {command:#04x}')


def identify(dev):
    r = Reader(dev.request(CMD['identify'], device=0x7F))
    info = dict(protocol=f'{r.u7()}.{r.u7()}', module=f'{r.u7()}.{r.u7()}.{r.u7()}', instrument=r.str(),
                version=r.str(), max_active=r.u7(), tick_hz=r.u14(), slots=r.u7(), schema=f'{r.u32():08X}',
                capabilities=r.u7())
    # Capabilities 2 is appended (PROTOCOL.md § 3.6); older firmware ends here.
    info['capabilities2'] = r.u7() if r.pos < len(r.data) else 0
    return info


def modes(dev):
    """Names and selection hints of the instrument modes (GET_MODE), [] without capability 2 bit 0."""
    if not identify(dev)['capabilities2'] & 1:
        return []
    result, count, m = [], 1, 0
    while m < count:
        r = Reader(dev.request(CMD['get_mode'], bytes([m])))
        r.u7()
        count = r.u7()
        result.append(dict(name=r.str(), hint=r.str()))
        m += 1
    return result


def targets(dev):
    result = []
    for t in range(8):
        r = Reader(dev.request(CMD['get_target'], bytes([t])))
        r.u7()
        flags, span = r.u7(), r.u14() / 16383
        result.append(dict(present=bool(flags & 1), per_voice=bool(flags & 2), latched=bool(flags & 4),
                           span=span, name=r.str(), span_text=r.str()))
    return result


def matrix(dev):
    r = Reader(dev.request(CMD['get_matrix']))
    depth = [d - 256 if d > 127 else d for d in r.packed(64)]
    return depth, r.mask64(), r.u32()


def print_matrix(dev):
    cols = targets(dev)
    allowed = Reader(dev.request(CMD['get_allowed'])).mask64()
    depth, enabled, crc = matrix(dev)
    print('          ' + ''.join(f'{(c["name"] or "-")[:7]:>8}' for c in cols))
    for s in range(8):
        cells = []
        for t in range(8):
            bit = 1 << (s * 8 + t)
            d = depth[s * 8 + t]
            cells.append('    ░░░ ' if not allowed & bit else f'{d:+5d}{"*" if enabled & bit and d else " "}  '
                         if d else '      · ')
        print(f'{SOURCES[s]:<10}' + ''.join(cells))
    print(f'patch CRC {crc:08X}   (* = active)')


def lengths(dev):
    """H2/H3: request frames of every length, replies of many lengths."""
    bad = []
    for total in range(HEADER + 3, MAX_FRAME + 1):  # unknown command 0x3E with padding: NAK 3 = intact
        payload = bytes(total - HEADER - 3)
        try:
            dev.request(0x3E, payload)
            bad.append((total, 'unexpected ACK'))
        except RuntimeError as e:
            if 'NAK 3' not in str(e):
                bad.append((total, str(e)))
        except TimeoutError as e:
            bad.append((total, str(e)))
    try:  # 136 bytes: libDaisy truncates the body to 128 bytes -> NAK 2
        dev.request(0x3E, bytes(125))
        bad.append((136, 'accepted'))
    except RuntimeError as e:
        if 'NAK 2' not in str(e):
            bad.append((136, str(e)))
    for n in range(17):  # reply lengths 11..27 via GET_NAME
        name = ('ABCDEFGHIJKLMNOP'[:n])
        dev.request(CMD['set_name'], bytes(Writer().str(name, 16).data))
        r = Reader(dev.request(CMD['get_name']))
        if r.str() != name:
            bad.append((11 + n, 'reply content'))
    identify(dev)
    matrix(dev)  # 100-byte reply
    print('frame lengths OK' if not bad else f'{len(bad)} problems: {bad[:10]}')
    return not bad


def stats(dev):
    r = Reader(dev.request(CMD['get_stats']))
    return {k: r.u32() for k in STATS}


def run_bench(dev, args):
    rows = []
    info = identify(dev)
    print(f'{info["instrument"]} v{info["version"]}, kMaxActive {info["max_active"]}, tick {info["tick_hz"]} Hz')
    for n in args.scenarios:
        try:
            dev.request(CMD['set_scenario'], bytes([n]))
        except RuntimeError:
            print(f'S{n}: not available in this build')
            continue
        time.sleep(0.2)
        dev.request(CMD['reset_stats'])
        end = time.time() + args.seconds
        seq, cell = 0, 0
        while time.time() < end:
            if n == 7:  # editor load: SET_CELL 50/s, monitor 25 Hz
                dev.request(CMD['set_cell'], bytes([0, 0, cell % 101, 2]))
                cell += 1
                if cell % 50 == 1:
                    dev.request(CMD['monitor'], bytes([25]))
                time.sleep(0.02)
            elif n == 8:  # MIDI flood: Poly AT 16 notes, CC1, channel AT, numbered CC3
                for note in range(60, 76):
                    dev.out.send(dev.mido.Message('polytouch', note=note, value=random.randint(0, 127)))
                dev.out.send(dev.mido.Message('control_change', control=1, value=random.randint(0, 127)))
                dev.out.send(dev.mido.Message('aftertouch', value=random.randint(0, 127)))
                dev.out.send(dev.mido.Message('control_change', control=3, value=seq & 127))
                seq += 1
                time.sleep(0.01)
            else:
                time.sleep(0.1)
        if n == 8:
            for note in range(60, 76):
                dev.out.send(dev.mido.Message('note_on', note=note, velocity=0))
        s = stats(dev)
        s['scenario'] = n
        rows.append(s)
        us = s['cycles_max'] / 480.0
        print(f'S{n}: matrix cycles min/avg/max {s["cycles_min"]}/{s["cycles_avg"]}/{s["cycles_max"]} '
              f'(max {us:.2f} us @480 MHz), callback load avg/max {s["load_avg"] / 100:.2f}/{s["load_max"] / 100:.2f} %, '
              f'overruns {s["overruns"]}, MIDI lost {s["midi_lost"]}, '
              f'main max {s["dispatch_max"]}/{s["service_max"]} cycles')
    if args.csv:
        with open(args.csv, 'w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=['scenario'] + STATS)
            writer.writeheader()
            writer.writerows(rows)
        print('written', args.csv)


def monitor(dev, seconds, rate=20):
    dev.request(CMD['monitor'], bytes([rate]))
    end, last_renew = time.time() + seconds, time.time()
    raw = [[], []]
    while time.time() < end:
        for _ in dev.receive(time.time() + 0.05):
            pass
        if time.time() - last_renew > 1.0:
            dev.request(CMD['monitor'], bytes([rate]))
            last_renew = time.time()
        while dev.monitor:
            _, data = dev.monitor.pop(0)
            r = Reader(data)
            # 18 values, plus 8 column base values (pot/CC position) when the
            # instrument reports them (52 bytes, IDENTIFY capability bit 4).
            values = [signed(r.u14()) for _ in range(len(data) // 2)]
            raw[0].append(values[16])
            raw[1].append(values[17])
            line = ' '.join(f'{SOURCES[i][:5]}={values[i]:+.3f}' for i in range(8))
            line += ' | CV raw ' + f'{values[16]:+.4f} {values[17]:+.4f}'
            if len(values) >= 26:
                line += ' | base ' + ' '.join('--' if v < 0 else f'{v:.2f}' for v in values[18:26])
            if len(data) >= 89:  # extended monitor: per-window range, clocks, aftertouch
                lo, hi = values[26:34], values[34:42]
                line += ' | range ' + ' '.join(f'{SOURCES[i][:4]}{lo[i]:+.2f}..{hi[i]:+.2f}' for i in range(3))
                clocks, kind, note, value, age = data[84:89]
                line += f' | clock {"M" if clocks & 1 else "-"}{"1" if clocks & 2 else "-"}{"2" if clocks & 4 else "-"}'
                line += ' | AT ' + ('-' if kind == 0 else ('Kanal' if kind == 1 else f'Poly n{note}'))
                line += f' {value} ({"alt" if age >= 127 else f"{age * 20} ms"})'
            if len(data) >= 91:  # instrument mode and origin of the last change
                mode, origin = data[89:91]
                line += ' | mode ' + ('?' if mode == 127 else str(mode))
                line += {0: ' (Geraet)', 1: ' (MIDI)'}.get(origin, '')
            print(line)
    dev.request(CMD['monitor'], bytes([0]))
    for i in range(2):
        if raw[i]:
            mean = sum(raw[i]) / len(raw[i])
            sd = (sum((x - mean) ** 2 for x in raw[i]) / len(raw[i])) ** 0.5
            print(f'CV{i + 1} raw: mean {mean:+.5f}, std {sd:.5f}, min {min(raw[i]):+.4f}, max {max(raw[i]):+.4f}')


def cc_stats(dev):
    dev.out.send(dev.mido.Message('control_change', channel=15, control=119, value=1))
    values = {}
    for msg in dev.receive(time.time() + 0.5):
        if isinstance(msg, tuple):
            continue
        if msg.type == 'control_change' and msg.channel == 15 and 100 <= msg.control <= 119:
            values[msg.control] = msg.value
            if msg.control == 119:
                break
    if 119 not in values:
        sys.exit('no CC reply')
    num = lambda cc, n: sum(values.get(cc + i, 0) << (7 * i) for i in range(n))
    print(f'scenario S{values.get(113, 0)}: matrix cycles avg/max {num(104, 3)}/{num(101, 3)}, '
          f'load avg/max {num(114, 3) / 100:.2f}/{num(107, 3) / 100:.2f} %, overruns {num(110, 3)}, '
          f'MIDI lost {values.get(117, 0)}')


def main():
    # The matrix printout uses non-ASCII cell marks; a Windows console defaults to cp1252.
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port')
    p.add_argument('--device', type=int, default=0)
    sub = p.add_subparsers(dest='cmd', required=True)
    for name in ['ports', 'identify', 'modes', 'targets', 'matrix', 'clear', 'lengths', 'stats', 'cc-stats', 'selftest']:
        sub.add_parser(name)
    c = sub.add_parser('set-cell')
    c.add_argument('source', choices=SOURCES, type=str.upper)
    c.add_argument('target', type=int)
    c.add_argument('depth', type=int)
    c.add_argument('--off', action='store_true')
    s = sub.add_parser('source')
    s.add_argument('source', choices=SOURCES[:3], type=str.upper)
    s.add_argument('param', choices=SOURCE_PARAMS)
    s.add_argument('value', type=int)
    sc = sub.add_parser('scenario')
    sc.add_argument('number', type=int)
    m = sub.add_parser('monitor')
    m.add_argument('--seconds', type=float, default=10)
    m.add_argument('--rate', type=int, default=20)
    cal = sub.add_parser('calibrate')
    cal.add_argument('cv1', type=float)
    cal.add_argument('cv2', type=float)
    b = sub.add_parser('bench')
    b.add_argument('--seconds', type=float, default=60)
    b.add_argument('--scenarios', type=int, nargs='*', default=list(range(9)))
    b.add_argument('--csv')
    args = p.parse_args()

    if args.cmd == 'selftest':
        return selftest()
    if args.cmd == 'ports':
        import mido
        print('outputs:', mido.get_output_names())
        print('inputs: ', mido.get_input_names())
        return
    dev = Device(args.port, args.device)
    if args.cmd == 'identify':
        print(identify(dev))
    elif args.cmd == 'modes':
        for i, m in enumerate(modes(dev)):
            print(i, m['name'], '-', m['hint'])
    elif args.cmd == 'targets':
        for i, t in enumerate(targets(dev)):
            print(i, t)
    elif args.cmd == 'matrix':
        print_matrix(dev)
    elif args.cmd == 'clear':
        dev.request(CMD['clear_matrix'])
    elif args.cmd == 'set-cell':
        r = Reader(dev.request(CMD['set_cell'], bytes([SOURCES.index(args.source), args.target, abs(args.depth),
                                                       (1 if args.depth < 0 else 0) | (0 if args.off else 2)])))
        r.u7()
        print(f'active {r.u7()}, patch CRC {r.u32():08X}')
    elif args.cmd == 'source':
        raw = args.value + 100 if args.param == 'fm' else args.value  # fm is sent as fm + 100
        value = Writer().u7(SOURCES.index(args.source)).u7(SOURCE_PARAMS.index(args.param)).u14(raw)
        dev.request(CMD['set_source'], bytes(value.data))
    elif args.cmd == 'scenario':
        dev.request(CMD['set_scenario'], bytes([args.number]))
    elif args.cmd == 'lengths':
        sys.exit(0 if lengths(dev) else 1)
    elif args.cmd == 'stats':
        print(stats(dev))
    elif args.cmd == 'cc-stats':
        cc_stats(dev)
    elif args.cmd == 'monitor':
        monitor(dev, args.seconds, args.rate)
    elif args.cmd == 'calibrate':
        enc = lambda v: max(0, min(16383, round(v * 8191) + 8192))
        dev.request(CMD['set_calibration'], bytes(Writer().u14(enc(args.cv1)).u14(enc(args.cv2)).u7(args.device).data))
    elif args.cmd == 'bench':
        run_bench(dev, args)


if __name__ == '__main__':
    main()
