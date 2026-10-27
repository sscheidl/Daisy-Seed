#!/usr/bin/env python3
"""SQ probe: command-line client for Acid Seq firmwares (docs/PROTOCOL.md).

Not the editor. Calibration, CV enable and diagnostics already in phase 1 (4.5).

    pip install mido python-rtmidi
    python sq_probe.py ports
    python sq_probe.py identify
    python sq_probe.py components                 # components and parameter descriptors
    python sq_probe.py params step-seq
    python sq_probe.py set step-seq tempo 128
    python sq_probe.py pattern
    python sq_probe.py state
    python sq_probe.py transport start|stop
    python sq_probe.py cal-get
    python sq_probe.py cal-set 0 --offset 2048 --scale -409.6 --gate-low 2048 --gate-high 410
    python sq_probe.py cal-save kit.json          # store the kit's calibration under its UID
    python sq_probe.py cal-load kit.json          # send it back, only to the same UID
    python sq_probe.py enable | disable           # CV enable is never stored or restored
    python sq_probe.py monitor --seconds 10
    python sq_probe.py stats
    python sq_probe.py selftest                   # codec, vectors and store rules, no port

The port defaults to the first one containing "Daisy"; use --port to choose. Requests time
out after 200 ms and are retried at most three times with the same request ID.
"""
import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sq_codec as sq  # noqa: E402

TIMEOUT, RETRIES = 0.2, 3
STATS = ['bad_length', 'bad_crc', 'bad_version', 'unknown_type', 'too_long', 'status_in_frame',
         'duplicate', 'echo', 'busy', 'monitor_dropped', 'note_on_dropped', 'midi_queue_overflow',
         'clock_ignored', 'callback_overruns', 'callback_peak_cycles', 'queue_high_water', 'stack_free_bytes']
STATES = ['STOP', 'WARTET', 'LÄUFT', 'PAUSE']
SOURCES = ['intern', 'Gate IN 1', 'MIDI USB', 'MIDI DIN']
CONTRACT = json.loads((Path(__file__).resolve().parent.parent / 'contracts' / 'parameters.json')
                      .read_text(encoding='utf-8'))


class Link:
    """One request open at a time; replies matched by type and request ID."""

    def __init__(self, port_name=None):
        import mido
        names = mido.get_output_names()
        name = port_name or next((n for n in names if 'Daisy' in n), None)
        if not name:
            raise SystemExit(f'No Daisy port found; available: {names}')
        inputs = mido.get_input_names()
        self.out = mido.open_output(name)
        self.inp = mido.open_input(next((n for n in inputs if n.split(' ')[0] in name), inputs[0]))
        self.rid = 0
        self.lost = 0

    def send_raw(self, frame):
        import mido
        self.out.send(mido.Message('sysex', data=frame[1:-1]))

    def frames(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            for message in self.inp.iter_pending():
                if message.type == 'sysex':
                    yield [0xF0] + list(message.data) + [0xF7]
            time.sleep(0.001)

    def request(self, kind, payload=()):
        self.rid = (self.rid + 1) & 0x7F
        frame = sq.encode(kind, self.rid, list(payload))
        for _ in range(RETRIES + 1):
            self.send_raw(frame)
            for reply in self.frames(TIMEOUT):
                decoded = sq.decode_reply(reply)  # damaged frames count as no reply
                if decoded is None or decoded[1] != self.rid:
                    continue
                reply_type, _, payload = decoded
                if reply_type == sq.NAK:
                    r = sq.Reader(payload)
                    r.u7()
                    code = r.u7()
                    name = next((k for k, v in sq.ERRORS.items() if v == code), str(code))
                    raise SystemExit(f'NAK {name}')
                if reply_type == kind | sq.RESPONSE:
                    return sq.Reader(payload)
        self.lost += 1
        raise SystemExit(f'No reply to {kind:#04x} after {RETRIES} retries')


def identify(link):
    r = link.request(sq.TYPES['IDENTIFY'])
    info = {'protocol': f'{r.u7()}.{r.u7()}', 'product': r.text(), 'version': r.text(),
            'boot_id': f'{r.u32():08X}', 'uid': [r.u32(), r.u32(), r.u32()],
            'capabilities': r.u14(), 'components': r.u7()}
    return info


def components(link):
    count = identify(link)['components']
    result = []
    for index in range(count):
        r = link.request(sq.TYPES['GET_COMPONENT'], sq.u7(index))
        r.u7(), r.u7()
        entry = {'index': index, 'id': r.text(), 'kind': r.u7(), 'version': r.text(), 'schema': r.u7(),
                 'params': []}
        for p in range(r.u7()):
            d = link.request(sq.TYPES['GET_PARAM_DESC'], sq.u7(index) + sq.u7(p))
            d.u7(), d.u7(), d.u7()
            entry['params'].append({'id': d.u14(), 'type': d.u7(), 'unit': d.u7(), 'flags': d.u7(),
                                    'min': d.i32() / 1000, 'max': d.i32() / 1000, 'default': d.i32() / 1000,
                                    'cc': d.u7(), 'key': d.text()})
        result.append(entry)
    return result


def component_index(link, component):
    for entry in components(link):
        if entry['id'] == component:
            return entry
    raise SystemExit(f'Component {component} not on this device')


def uid_payload(uid):
    return sq.u32(uid[0]) + sq.u32(uid[1]) + sq.u32(uid[2])


def cal_payload(output, cal):
    return (sq.u7(output) + sq.i32(int(round(cal['offset'] * 1000))) + sq.i32(int(round(cal['scale'] * 1000))) +
            sq.u14(cal['gate_low']) + sq.u14(cal['gate_high']) + sq.u14(cal['clamp_min']) + sq.u14(cal['clamp_max']))


def valid_cal(cal):
    scale = abs(cal['scale'])
    codes = [cal[k] for k in ('gate_low', 'gate_high', 'clamp_min', 'clamp_max')]
    return (100 <= scale <= 2000 and 0 <= cal['offset'] <= 4095 and all(0 <= c <= 4095 for c in codes) and
            cal['clamp_min'] < cal['clamp_max'] and cal['gate_low'] != cal['gate_high'] and
            all(cal['clamp_min'] <= cal[k] <= cal['clamp_max'] for k in ('gate_low', 'gate_high')))


def store_entry(store, uid):
    """Calibration data belong to one kit: looked up by its 96-bit UID only."""
    return store.get('kits', {}).get('-'.join(f'{w:08X}' for w in uid))


def get_cal(link):
    result = []
    for output in (0, 1):
        r = link.request(sq.TYPES['GET_CAL'], sq.u7(output))
        r.u7()
        result.append({'valid': bool(r.u7()), 'offset': r.i32() / 1000, 'scale': r.i32() / 1000,
                       'gate_low': r.u14(), 'gate_high': r.u14(), 'clamp_min': r.u14(), 'clamp_max': r.u14()})
    return result


def selftest():
    vectors = json.loads((Path(__file__).resolve().parent.parent / 'contracts' / 'sq_vectors.json')
                         .read_text(encoding='utf-8'))
    assert vectors == sq.vectors(), 'stored vectors differ from the codec'
    for case in vectors['cases']:
        frame = [int(b, 16) for b in case['frame'].split()]
        verdict, detail = sq.classify(frame)
        assert verdict == case['verdict'], case['name']
        if 'stream' in case:
            assert sq.assemble([int(b, 16) for b in case['stream'].split()])[0] == frame
    cal = {'offset': 2048, 'scale': -409.6, 'gate_low': 2048, 'gate_high': 410, 'clamp_min': 0, 'clamp_max': 4095}
    assert valid_cal(cal)
    assert not valid_cal(dict(cal, gate_high=2048)) and not valid_cal(dict(cal, scale=50))
    store = {'kits': {'00000001-00000002-00000003': [cal, cal]}}
    assert store_entry(store, [1, 2, 3]) and store_entry(store, [1, 2, 4]) is None
    payload = cal_payload(0, cal)
    r = sq.Reader(payload)
    assert (r.u7(), r.i32(), r.i32()) == (0, 2048000, -409600)
    assert all(p['key'] for c in CONTRACT['components'].values() for p in c['parameters'])
    print('PASS: sq_probe selftest (vectors, calibration rules, UID-bound store)')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port')
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('ports', 'identify', 'components', 'pattern', 'state', 'cal-get', 'enable', 'disable',
                 'stats', 'reset-stats', 'selftest'):
        sub.add_parser(name)
    p = sub.add_parser('params')
    p.add_argument('component')
    p = sub.add_parser('set')
    p.add_argument('component')
    p.add_argument('key')
    p.add_argument('value', type=float)
    p = sub.add_parser('transport')
    p.add_argument('action', choices=['start', 'stop'])
    p = sub.add_parser('cal-set')
    p.add_argument('output', type=int, choices=[0, 1])
    for field, default in (('offset', None), ('scale', None), ('gate-low', None), ('gate-high', None),
                           ('clamp-min', 0), ('clamp-max', 4095)):
        p.add_argument('--' + field, type=float, required=default is None, default=default)
    for name in ('cal-save', 'cal-load'):
        p = sub.add_parser(name)
        p.add_argument('file', type=Path)
    p = sub.add_parser('monitor')
    p.add_argument('--seconds', type=float, default=10)
    args = parser.parse_args()
    if args.command == 'selftest':
        return selftest()
    if args.command == 'ports':
        import mido
        print('outputs:', mido.get_output_names())
        print('inputs:', mido.get_input_names())
        return
    link = Link(args.port)
    if args.command == 'identify':
        print(json.dumps(identify(link), indent=2))
    elif args.command == 'components':
        print(json.dumps(components(link), indent=2, ensure_ascii=False))
    elif args.command == 'params':
        entry = component_index(link, args.component)
        r = link.request(sq.TYPES['GET_PARAMS'], sq.u7(entry['index']) + sq.u7(0))
        r.u7(), r.u7()
        total, count = r.u7(), r.u7()
        values = {}
        for _ in range(count):
            pid = r.u14()
            values[pid] = r.i32() / 1000
        keys = {p['id']: p['key'] for p in entry['params']}
        for pid, value in values.items():
            print(f'{keys.get(pid, pid):16} {value:g}')
        if total > count:
            print(f'({total - count} more parameters in further fragments)')
    elif args.command == 'set':
        entry = component_index(link, args.component)
        param = next((p for p in entry['params'] if p['key'] == args.key), None)
        if not param:
            raise SystemExit(f'Unknown parameter {args.key}')
        r = link.request(sq.TYPES['SET_PARAM'], sq.u7(entry['index']) + sq.u14(param['id']) +
                         sq.i32(int(round(args.value * 1000))))
        r.u7(), r.u14()
        print(f'{args.key} = {r.i32() / 1000:g} (revision {r.u14()})')
    elif args.command == 'pattern':
        r = link.request(sq.TYPES['GET_PATTERN'])
        print(f'revision accepted {r.u14()}, applied {r.u14()}')
        names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'H']
        for i in range(16):
            note, length, bits = r.u7(), r.u7(), r.u7()
            flags = ''.join(c for c, b in (('G', 1), ('A', 2), ('S', 4)) if bits & b)
            print(f"{'AB'[i // 8]}{i % 8 + 1}: {names[note % 12]}{note // 12 + 1} {length:3d} % {flags}")
    elif args.command == 'state':
        r = link.request(sq.TYPES['GET_STATE'])
        state, mode, sw17, sw18, source, step = r.u7(), r.u7(), r.u7(), r.u7(), r.u7(), r.u7()
        print(f"{STATES[state]}, Modus {['Vorwärts', 'Rückwärts', 'Zufall'][mode]}, SW17 {'Run' if sw17 else 'Stop'}, "
              f"SW18 {sw18}, Quelle {SOURCES[source]}, Step {step + 1 if step < 16 else '-'}, "
              f"{r.i32() / 1000:.1f} BPM, Revision {r.u14()}/{r.u14()}, CV {'frei' if r.u7() else 'gesperrt'}")
    elif args.command == 'transport':
        link.request(sq.TYPES['TRANSPORT'], sq.u7(1 if args.action == 'start' else 0))
        print('OK')
    elif args.command == 'cal-get':
        print(json.dumps(get_cal(link), indent=2))
    elif args.command == 'cal-set':
        cal = {'offset': args.offset, 'scale': args.scale, 'gate_low': int(args.gate_low),
               'gate_high': int(args.gate_high), 'clamp_min': int(args.clamp_min), 'clamp_max': int(args.clamp_max)}
        if not valid_cal(cal):
            raise SystemExit('Calibration outside the documented bounds (PROTOCOL.md 3.5)')
        uid = identify(link)['uid']
        link.request(sq.TYPES['SET_CAL'], uid_payload(uid) + cal_payload(args.output, cal))
        print('OK (an existing CV enable was withdrawn)')
    elif args.command == 'cal-save':
        info = identify(link)
        store = json.loads(args.file.read_text()) if args.file.exists() else {'kits': {}}
        store.setdefault('kits', {})['-'.join(f'{w:08X}' for w in info['uid'])] = get_cal(link)
        args.file.write_text(json.dumps(store, indent=2))
        print(f'saved for UID {info["uid"]}')
    elif args.command == 'cal-load':
        info = identify(link)
        entry = store_entry(json.loads(args.file.read_text()), info['uid'])
        if entry is None:
            raise SystemExit('No calibration stored for this kit (UID differs): nothing sent')
        for output, cal in enumerate(entry):
            if cal.get('valid', True):
                link.request(sq.TYPES['SET_CAL'], uid_payload(info['uid']) + cal_payload(output, cal))
        print('sent; CV stays locked until "enable"')
    elif args.command in ('enable', 'disable'):
        uid = identify(link)['uid']
        link.request(sq.TYPES['CV_ENABLE'], uid_payload(uid) + sq.u7(1 if args.command == 'enable' else 0))
        print('CV frei' if args.command == 'enable' else 'CV gesperrt')
    elif args.command in ('stats', 'reset-stats'):
        if args.command == 'reset-stats':
            link.request(sq.TYPES['RESET_STATS'])
        r = link.request(sq.TYPES['GET_STATS'])
        for i in range(r.u7()):
            print(f'{STATS[i] if i < len(STATS) else i:22} {r.u32()}')
    elif args.command == 'monitor':
        link.request(sq.TYPES['MONITOR'], sq.u7(1))
        renewed = time.monotonic()
        last_rid = None
        for frame in link.frames(args.seconds):
            if time.monotonic() - renewed > 1:
                link.send_raw(sq.encode(sq.TYPES['MONITOR'], 0, sq.u7(1)))
                renewed = time.monotonic()
            if len(frame) < 12 or frame[5] != sq.MONITOR_EVENT:
                continue
            if last_rid is not None and frame[6] != (last_rid + 1) & 0x7F:
                print('(monitor frame lost)')
            last_rid = frame[6]
            r = sq.Reader(frame[8:-3])
            print(f'event {r.u7()}: {frame[9:-3]}')


if __name__ == '__main__':
    main()
