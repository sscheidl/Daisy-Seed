#!/usr/bin/env python3
"""Reference codec for the SQ SysEx protocol (F0 7D 53 51 ...), see docs/PROTOCOL.md.

The firmware (C++), the editor (C++ and JavaScript) and sq_probe.py implement the same
rules; contracts/sq_vectors.json holds the shared test vectors generated from this file:

    python sequencers/step-seq/tools/sq_codec.py --write   # regenerate the vectors
    python sequencers/step-seq/tools/sq_codec.py --check   # compare with the stored file
"""
import argparse
import json
from pathlib import Path

HEADER = (0xF0, 0x7D, 0x53, 0x51)
VERSION = 1
VERSION_MINOR = 0
MAX_FRAME = 120                      # bytes including F0 and F7
OVERHEAD = 11                        # F0 7D 53 51 ver type rid len crc_hi crc_lo F7
MAX_PAYLOAD = MAX_FRAME - OVERHEAD   # 109
RESPONSE = 0x40
MONITOR_EVENT = 0x7E
NAK = 0x7F

TYPES = {
    'IDENTIFY': 0x01, 'GET_COMPONENT': 0x02, 'GET_PARAM_DESC': 0x03, 'GET_ENUM_LABEL': 0x04,
    'GET_PARAMS': 0x05, 'SET_PARAM': 0x06,
    'GET_PATTERN': 0x10, 'SET_PATTERN': 0x11, 'SET_STEP': 0x12,
    'TRANSPORT': 0x20, 'GET_STATE': 0x21,
    'GET_CAL': 0x28, 'SET_CAL': 0x29, 'CV_ENABLE': 0x2A,
    'MONITOR': 0x30, 'GET_STATS': 0x31, 'RESET_STATS': 0x32,
}

ERRORS = {'LENGTH': 1, 'CRC': 2, 'UNKNOWN_TYPE': 3, 'BAD_ARGUMENT': 4, 'BUSY': 5,
          'VERSION': 6, 'UID_MISMATCH': 7, 'NOT_CALIBRATED': 8, 'NOT_ALLOWED': 9}


def crc8(data):
    """CRC-8, polynomial 0x07, init 0x00, no reflection, no final XOR (CRC-8/SMBUS)."""
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


# 7-bit field encoders; every multi-byte value is sent LSB first.
def u7(value):
    if not 0 <= value <= 0x7F:
        raise ValueError(f'u7 out of range: {value}')
    return [value]


def u14(value):
    if not 0 <= value <= 0x3FFF:
        raise ValueError(f'u14 out of range: {value}')
    return [value & 0x7F, value >> 7]


def u32(value):
    if not 0 <= value <= 0xFFFFFFFF:
        raise ValueError(f'u32 out of range: {value}')
    return [(value >> (7 * i)) & 0x7F for i in range(5)]


def i32(value):
    if not -0x80000000 <= value <= 0x7FFFFFFF:
        raise ValueError(f'i32 out of range: {value}')
    return u32(value & 0xFFFFFFFF)


def text(value, limit):
    data = value.encode('ascii')
    if len(data) > limit or any(b > 0x7E or b < 0x20 for b in data):
        raise ValueError(f'invalid string {value!r}')
    return [len(data)] + list(data)


class Reader:
    def __init__(self, payload):
        self.data = list(payload)
        self.pos = 0

    def take(self, count):
        if self.pos + count > len(self.data):
            raise ValueError('payload too short')
        part = self.data[self.pos:self.pos + count]
        self.pos += count
        return part

    def u7(self):
        return self.take(1)[0]

    def u14(self):
        lo, hi = self.take(2)
        return lo | (hi << 7)

    def u32(self):
        parts = self.take(5)
        if parts[4] > 0x0F:
            raise ValueError('u32 overflow')
        return sum(p << (7 * i) for i, p in enumerate(parts))

    def i32(self):
        value = self.u32()
        return value - (1 << 32) if value & 0x80000000 else value

    def text(self):
        length = self.u7()
        return bytes(self.take(length)).decode('ascii')

    def done(self):
        return self.pos == len(self.data)


def encode(kind, rid, payload, version=VERSION):
    payload = list(payload)
    if len(payload) > MAX_PAYLOAD:
        raise ValueError('payload too long')
    body = [version, kind, rid, len(payload)] + payload
    crc = crc8(body)
    return list(HEADER) + body + [crc >> 7, crc & 0x7F, 0xF7]


def assemble(stream):
    """Bytes of one SysEx as the MIDI parser hands them over.

    Real-time bytes (F8..FF) may appear anywhere in a MIDI stream, also inside SysEx.
    libDaisy's MidiParser (and bootloader::SysExMatcher) emits them as their own
    events and keeps collecting the SysEx, so they never reach the SQ frame. Returns
    (frame, number of real-time bytes removed).
    """
    frame = [b for b in stream if b < 0xF8]
    return frame, len(stream) - len(frame)


def classify(frame):
    """Device-side acceptance of an assembled frame (see assemble) in the documented order.

    Returns (verdict, detail): 'foreign' (not SQ, pass on), 'ignore' (reply-class frame,
    never answered), 'nak' (detail = error code) or 'ok' (detail = (type, rid, payload)).
    """
    frame = list(frame)
    if len(frame) < 5 or frame[0] != 0xF0 or tuple(frame[:4]) != HEADER:
        return 'foreign', None
    if frame[-1] != 0xF7:
        return 'nak', ERRORS['LENGTH']
    inner = frame[1:-1]
    if any(b & 0x80 for b in inner):
        return 'nak', ERRORS['CRC']         # a status byte inside the frame
    if len(frame) > MAX_FRAME:
        return 'nak', ERRORS['LENGTH']      # truncated by the 128-byte parser buffer
    if len(frame) >= 6 and frame[5] & RESPONSE:
        return 'ignore', None               # replies, NAKs and monitor frames: echo guard
    if len(frame) < OVERHEAD or frame[7] != len(frame) - OVERHEAD:
        return 'nak', ERRORS['LENGTH']
    if frame[4] != VERSION:
        return 'nak', ERRORS['VERSION']
    body = frame[4:-3]
    crc = crc8(body)
    if frame[-3:-1] != [crc >> 7, crc & 0x7F]:
        return 'nak', ERRORS['CRC']
    return 'ok', (frame[5], frame[6], frame[8:-3])


def step(note, gate_length, gate=True, accent=False, slide=False):
    return u7(note) + u7(gate_length) + u7(int(gate) | int(accent) << 1 | int(slide) << 2)


def _hex(frame):
    return ' '.join(f'{b:02X}' for b in frame)


def vectors():
    """Valid and invalid frames shared by every implementation."""
    t = TYPES
    pattern = []
    for index in range(16):
        pattern += step(note=24 + (index * 5) % 36, gate_length=50 + index,
                        gate=index % 4 != 3, accent=index % 3 == 0, slide=index % 5 == 2)
    uid = (0x12345678, 0x9ABCDEF0, 0x0F1E2D3C)
    valid = [
        ('identify request', t['IDENTIFY'], 1, []),
        ('identify reply', t['IDENTIFY'] | RESPONSE, 1,
         u7(VERSION) + u7(VERSION_MINOR) + text('step-seq', 16) + text('0.1.0', 12) +
         u32(0xCAFEBABE) + u32(uid[0]) + u32(uid[1]) + u32(uid[2]) + u14(0b111001) + u7(1)),
        ('get component 0', t['GET_COMPONENT'], 2, u7(0)),
        ('component reply', t['GET_COMPONENT'] | RESPONSE, 2,
         u7(0) + u7(1) + text('step-seq', 16) + u7(0) + text('0.1.0', 12) + u7(1) + u7(19)),
        ('param descriptor reply (tempo)', t['GET_PARAM_DESC'] | RESPONSE, 3,
         u7(0) + u7(0) + u7(19) + u14(1) + u7(0) + u7(7) + u7(0) + i32(30000) + i32(300000) +
         i32(120000) + u7(127) + text('tempo', 16)),
        ('set param transpose -7', t['SET_PARAM'], 4, u7(0) + u14(4) + i32(-7000)),
        ('set param reply', t['SET_PARAM'] | RESPONSE, 4, u7(0) + u14(4) + i32(-7000) + u14(3)),
        ('set pattern', t['SET_PATTERN'], 5, pattern),
        ('pattern reply', t['GET_PATTERN'] | RESPONSE, 6, u14(9) + u14(8) + pattern),
        ('set step 15', t['SET_STEP'], 7, u7(15) + step(59, 100, True, True, True)),
        ('transport start', t['TRANSPORT'], 8, u7(1)),
        ('set calibration', t['SET_CAL'], 9,
         u32(uid[0]) + u32(uid[1]) + u32(uid[2]) + u7(0) + i32(2048000) + i32(-409600) +
         u14(2048) + u14(410) + u14(0) + u14(4095)),
        ('cv enable', t['CV_ENABLE'], 10, u32(uid[0]) + u32(uid[1]) + u32(uid[2]) + u7(1)),
        ('nak bad argument', NAK, 11, u7(t['SET_STEP']) + u7(ERRORS['BAD_ARGUMENT'])),
        ('monitor step event', MONITOR_EVENT, 12, u7(1) + u7(5) + u7(1)),
        ('max payload', t['SET_PATTERN'], 127, [0x55] * MAX_PAYLOAD),
    ]
    cases = []
    for name, kind, rid, payload in valid:
        frame = encode(kind, rid, payload)
        verdict = 'ignore' if kind & RESPONSE else 'ok'
        cases.append({'name': name, 'frame': _hex(frame), 'verdict': verdict,
                      'type': kind, 'rid': rid, 'payload': _hex(payload)})
    request = encode(t['SET_STEP'], 20, u7(3) + step(30, 40))
    corrupt = list(request)
    corrupt[9] ^= 0x01
    short_len = list(request)
    short_len[7] += 1
    realtime = request[:9] + [0xF8] + request[9:12] + [0xFA, 0xF8] + request[12:]
    status = request[:9] + [0x90] + request[9:]
    version = encode(t['IDENTIFY'], 21, [], version=2)
    unknown = encode(0x3E, 22, [])
    oversize = list(HEADER) + [VERSION, t['SET_PATTERN'], 23, 0x7F] + [0] * 120 + [0, 0, 0xF7]
    bad = [
        ('flipped payload bit', corrupt, 'nak', ERRORS['CRC']),
        ('length field mismatch', short_len, 'nak', ERRORS['LENGTH']),
        ('status byte 0x90 inside the frame', status, 'nak', ERRORS['CRC']),
        ('protocol version 2', version, 'nak', ERRORS['VERSION']),
        ('frame over 120 bytes', oversize, 'nak', ERRORS['LENGTH']),
        # Framing is valid; the dispatcher answers NAK UNKNOWN_TYPE.
        ('unknown request type 0x3E', unknown, 'ok', ERRORS['UNKNOWN_TYPE']),
        ('Mod on Daisy frame', [0xF0, 0x7D, 0x4D, 0x44, 1, 1, 0, 0, 0x7F, 0xF7], 'foreign', None),
        ('bootloader request', [0xF0, 0x7D, 0x42, 0x4C, 0xF7], 'foreign', None),
    ]
    for name, frame, verdict, error in bad:
        case = {'name': name, 'frame': _hex(frame), 'verdict': verdict}
        if error is not None:
            case['error'] = error
        cases.append(case)
    # Clock, Start and Clock between the bytes of a request: the parser delivers them
    # as real-time events, the frame itself stays valid (DAW clock while the panel works).
    cases.append({'name': 'interleaved real-time bytes F8 FA F8', 'stream': _hex(realtime),
                  'realtime_bytes': 3, 'frame': _hex(request), 'verdict': 'ok',
                  'type': t['SET_STEP'], 'rid': 20, 'payload': _hex(request[8:-3])})
    return {
        'format_version': 1,
        'protocol_version': f'{VERSION}.{VERSION_MINOR}',
        'crc8_check': {'input': '123456789', 'crc': crc8(b'123456789')},
        'cases': cases,
    }


VECTORS = Path(__file__).resolve().parent.parent / 'contracts' / 'sq_vectors.json'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = json.dumps(vectors(), indent=2) + '\n'
    if args.write:
        VECTORS.write_text(data, encoding='utf-8', newline='\n')
    elif args.check:
        if VECTORS.read_text(encoding='utf-8') != data:
            parser.exit(1, 'sq_vectors.json is out of date\n')
        print('PASS: sq_vectors.json matches the reference codec')
    else:
        print(data, end='')


if __name__ == '__main__':
    main()
