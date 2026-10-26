// SQ SysEx protocol v1.0 for Acid Seq firmwares (sequencers/step-seq/docs/PROTOCOL.md).
// JavaScript mirror of step_seq/sq_protocol.h and tools/sq_codec.py, plus the dispatcher
// between Mod on Daisy (7D 4D 44), SQ (7D 53 51) and the bootloader reply (7D 42 4C), and
// the SQ handshake. Works as a browser global (window.SqProtocol) and as a Node module.
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.SqProtocol = api;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const HEADER = [0xf0, 0x7d, 0x53, 0x51];
  const VERSION = 1, VERSION_MINOR = 0, MAX_FRAME = 120, OVERHEAD = 11, MAX_PAYLOAD = MAX_FRAME - OVERHEAD;
  const RESPONSE = 0x40, MONITOR_EVENT = 0x7e, NAK = 0x7f;
  const TYPE = {
    identify: 0x01, getComponent: 0x02, getParamDesc: 0x03, getEnumLabel: 0x04, getParams: 0x05, setParam: 0x06,
    getPattern: 0x10, setPattern: 0x11, setStep: 0x12, transport: 0x20, getState: 0x21,
    getCal: 0x28, setCal: 0x29, cvEnable: 0x2a, monitor: 0x30, getStats: 0x31, resetStats: 0x32,
  };
  const ERROR = {
    length: 1, crc: 2, unknownType: 3, badArgument: 4, busy: 5, version: 6, uidMismatch: 7,
    notCalibrated: 8, notAllowed: 9,
  };
  const ERROR_TEXT = {
    1: 'Länge', 2: 'Prüfsumme', 3: 'unbekannter Typ', 4: 'Wert außerhalb des Bereichs', 5: 'beschäftigt',
    6: 'Protokollversion', 7: 'anderes Kit (UID)', 8: 'nicht kalibriert', 9: 'im aktuellen Zustand nicht erlaubt',
  };
  const TIMEOUT_MS = 200, RETRIES = 3;

  // CRC-8/SMBUS: polynomial 0x07, init 0, no reflection; check('123456789') = 0xF4.
  function crc8(bytes) {
    let crc = 0;
    for (const b of bytes) {
      crc ^= b;
      for (let i = 0; i < 8; i++) crc = crc & 0x80 ? ((crc << 1) ^ 0x07) & 0xff : (crc << 1) & 0xff;
    }
    return crc;
  }

  class Writer {
    constructor() { this.bytes = []; }
    u7(v) { if (v < 0 || v > 0x7f) throw new RangeError('u7 ' + v); this.bytes.push(v); return this; }
    u14(v) { if (v < 0 || v > 0x3fff) throw new RangeError('u14 ' + v); this.bytes.push(v & 0x7f, v >> 7); return this; }
    u32(v) { v >>>= 0; for (let i = 0; i < 5; i++) this.bytes.push(Math.floor(v / 2 ** (7 * i)) & 0x7f); return this; }
    i32(v) { return this.u32(v | 0); }
    str(s, limit) {
      const chars = Array.from(s).map((c) => c.charCodeAt(0));
      if (chars.length > limit || chars.some((c) => c < 0x20 || c > 0x7e)) throw new RangeError('str ' + s);
      this.u7(chars.length);
      chars.forEach((c) => this.bytes.push(c));
      return this;
    }
  }

  class Reader {
    constructor(bytes) { this.bytes = bytes; this.pos = 0; }
    take(n) {
      if (this.pos + n > this.bytes.length) throw new RangeError('payload too short');
      const part = this.bytes.slice(this.pos, this.pos + n);
      this.pos += n;
      return part;
    }
    u7() { return this.take(1)[0]; }
    u14() { const [lo, hi] = this.take(2); return lo | (hi << 7); }
    u32() {
      const p = this.take(5);
      if (p[4] > 0x0f) throw new RangeError('u32 overflow');
      return p.reduce((acc, b, i) => acc + b * 2 ** (7 * i), 0) >>> 0;
    }
    i32() { return this.u32() | 0; }
    str() { const n = this.u7(); return String.fromCharCode(...this.take(n)); }
    done() { return this.pos === this.bytes.length; }
  }

  function encode(type, rid, payload, version = VERSION) {
    if (payload.length > MAX_PAYLOAD) throw new RangeError('payload too long');
    const body = [version, type & 0x7f, rid & 0x7f, payload.length, ...payload];
    const crc = crc8(body);
    return [...HEADER, ...body, crc >> 7, crc & 0x7f, 0xf7];
  }

  // Real-time bytes inside SysEx are delivered as their own events by the MIDI parser.
  function assemble(stream) {
    const frame = stream.filter((b) => b < 0xf8);
    return { frame, realtime: stream.length - frame.length };
  }

  // Acceptance in the documented order (PROTOCOL.md § 2); same verdicts as sq_codec.classify.
  function classify(f) {
    if (f.length < 5 || HEADER.some((b, i) => f[i] !== b)) return { verdict: 'foreign' };
    if (f[f.length - 1] !== 0xf7) return { verdict: 'nak', error: ERROR.length };
    if (f.slice(1, -1).some((b) => b & 0x80)) return { verdict: 'nak', error: ERROR.crc };
    if (f.length > MAX_FRAME) return { verdict: 'nak', error: ERROR.length };
    if (f.length >= 6 && f[5] & RESPONSE) return { verdict: 'ignore', type: f[5], rid: f[6] };
    if (f.length < OVERHEAD || f[7] !== f.length - OVERHEAD) return { verdict: 'nak', error: ERROR.length };
    if (f[4] !== VERSION) return { verdict: 'nak', error: ERROR.version };
    const crc = crc8(f.slice(4, -3));
    if (f[f.length - 3] !== crc >> 7 || f[f.length - 2] !== (crc & 0x7f)) return { verdict: 'nak', error: ERROR.crc };
    return { verdict: 'ok', type: f[5], rid: f[6], payload: f.slice(8, -3) };
  }

  // Reply-class frames from the device: CRC checked, payload returned.
  function parseReply(f) {
    if (f.length < OVERHEAD || HEADER.some((b, i) => f[i] !== b) || !(f[5] & RESPONSE)) return null;
    if (f[f.length - 1] !== 0xf7 || f[7] !== f.length - OVERHEAD) return null;
    const crc = crc8(f.slice(4, -3));
    if (f[f.length - 3] !== crc >> 7 || f[f.length - 2] !== (crc & 0x7f)) return null;
    return { type: f[5], rid: f[6], payload: f.slice(8, -3) };
  }

  // Which conversation a SysEx belongs to.
  function protocolOf(f) {
    if (f.length < 5 || f[0] !== 0xf0 || f[1] !== 0x7d) return 'unknown';
    if (f[2] === 0x4d && f[3] === 0x44) return 'md';
    if (f[2] === 0x53 && f[3] === 0x51) return 'sq';
    if (f[2] === 0x42 && f[3] === 0x4c) return 'bootloader';
    return 'unknown';
  }

  // Handshake: IDENTIFY, then the component list. Sends nothing else (the device has
  // priority, D13). send(frame) transmits; feed device frames into onMessage(frame, now).
  class Session {
    constructor(send) {
      this.send = send;
      this.rid = 0;
      this.phase = 'offline';
      this.identity = null;
      this.components = [];
      this.lost = 0;
      this.ignored = 0;
      this.waiting = null;
    }
    connect(now) {
      this.identity = null;
      this.components = [];
      this.phase = 'identify';
      this.request(TYPE.identify, [], now);
    }
    request(type, payload, now) {
      this.rid = (this.rid + 1) & 0x7f;
      this.waiting = { type, frame: encode(type, this.rid, payload), tries: 0, sentAt: now };
      this.transmit(now);
    }
    transmit(now) {
      this.waiting.tries += 1;
      this.waiting.sentAt = now;
      this.send(this.waiting.frame);
    }
    tick(now) {
      if (!this.waiting || now - this.waiting.sentAt < TIMEOUT_MS) return;
      if (this.waiting.tries > RETRIES) {
        this.waiting = null;
        this.lost += 1;
        this.phase = 'failed';
        return;
      }
      this.transmit(now);
    }
    onMessage(frame, now) {
      if (protocolOf(frame) !== 'sq') return false;
      const reply = parseReply(frame);
      if (!reply || reply.type === MONITOR_EVENT || !this.waiting || reply.rid !== this.rid) {
        this.ignored += 1;
        return true;
      }
      const r = new Reader(reply.payload);
      if (reply.type === NAK) {
        this.waiting = null;
        r.u7();
        this.error = r.u7();
        this.phase = 'failed';
        return true;
      }
      if (reply.type !== (this.waiting.type | RESPONSE)) {
        this.ignored += 1;
        return true;
      }
      const type = this.waiting.type;
      this.waiting = null;
      if (type === TYPE.identify) {
        this.identity = {
          protocol: `${r.u7()}.${r.u7()}`, product: r.str(), version: r.str(), bootId: r.u32(),
          uid: [r.u32(), r.u32(), r.u32()], capabilities: r.u14(), componentCount: r.u7(),
        };
      } else if (type === TYPE.getComponent) {
        r.u7(); r.u7();
        this.components.push({ id: r.str(), kind: r.u7(), version: r.str(), schema: r.u7(), params: r.u7() });
      }
      if (this.components.length < this.identity.componentCount) {
        this.phase = 'components';
        this.request(TYPE.getComponent, [this.components.length], now);
      } else {
        this.phase = 'ready';
      }
      return true;
    }
  }

  return {
    HEADER, VERSION, VERSION_MINOR, MAX_FRAME, OVERHEAD, MAX_PAYLOAD, RESPONSE, MONITOR_EVENT, NAK,
    TYPE, ERROR, ERROR_TEXT, TIMEOUT_MS, RETRIES,
    crc8, Writer, Reader, encode, assemble, classify, parseReply, protocolOf, Session,
  };
});
