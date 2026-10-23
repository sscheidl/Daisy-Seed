// Mod on Daisy SysEx protocol v1 and patch model (modules/mod-on-daisy/docs/PROTOCOL.md).
// JavaScript mirror of include/mod_on_daisy/protocol.h and patch.h. Works as a
// browser global (window.ModProtocol) and as a Node module (tests).
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.ModProtocol = api;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const MANUFACTURER = 0x7d, PROJECT = [0x4d, 0x44], VERSION = 1;
  const MAX_FRAME = 120, HEADER = 8, MAX_PAYLOAD = MAX_FRAME - 2 - HEADER - 1;
  const BROADCAST = 0x7f, REPLY_BIT = 0x40, NAK = 0x7f, MONITOR_FRAME = 0x7e; // 0x70 = reply to monitor
  const CMD = {
    identify: 0x01, getTarget: 0x02, getAllowed: 0x03,
    getMatrix: 0x10, setMatrix: 0x11, setCell: 0x12, clearMatrix: 0x13,
    getSource: 0x18, setSource: 0x19, getName: 0x1a, setName: 0x1b,
    getCalibration: 0x24, setCalibration: 0x25,
    monitor: 0x30, getStats: 0x31, resetStats: 0x32, setScenario: 0x33,
  };
  const ERRORS = {
    1: 'Prüfsumme', 2: 'Länge', 3: 'unbekanntes Kommando', 4: 'Wert außerhalb des Bereichs',
    5: 'beschäftigt', 6: 'Protokollversion', 7: 'höchstens 8 aktive Verbindungen',
    8: 'Zelle nicht erlaubt', 9: 'nicht implementiert', 10: 'Speicherfehler', 11: 'Schema',
  };

  const SOURCES = ['LFO 1', 'LFO 2', 'Random', 'CV 1', 'CV 2', 'Modwheel', 'Channel AT', 'Poly AT'];
  const SHAPES = ['Sinus', 'Dreieck', 'Sägezahn ↑', 'Sägezahn ↓', 'Rechteck', 'S&H'];
  const RANDOM_MODES = ['Stufe', 'Geglättet'];
  const SYNCS = ['Frei', 'MIDI-Clock', 'Gate'];
  const MIDI_DIVISIONS = ['4 Takte', '2 Takte', '1 Takt', '1/2', '1/4', '1/4T', '1/8', '1/8T', '1/16', '1/16T', '1/32'];
  const GATE_DIVISIONS = ['1/4', '1/2', '1/1', '2/1', '4/1'];
  const DEFAULT_MIDI_DIVISION = 4, DEFAULT_GATE_DIVISION = 2;
  const PARAM = { rate: 0, shape: 1, sync: 2, gate: 3, division: 4, retrigger: 5 };
  const RATE_MAX = 16383, CELLS = 64, MAX_DEPTH = 100;

  const rateToHz = (r) => 0.02 * Math.pow(1000, Math.min(RATE_MAX, Math.max(0, r)) / RATE_MAX);
  const hzToRate = (hz) => Math.round(Math.log(hz / 0.02) / Math.log(1000) * RATE_MAX);

  function checksum(bytes) {
    let sum = 0;
    for (const b of bytes) sum += b;
    return (128 - (sum & 127)) & 127;
  }

  class Writer {
    constructor() { this.bytes = []; }
    u7(v) { this.bytes.push(v & 127); return this; }
    u14(v) { return this.u7(v).u7(v >> 7); }
    u32(v) { for (let i = 0; i < 5; ++i) this.u7(Math.floor(v / 2 ** (7 * i))); return this; }
    str(s, max) {
      const chars = Array.from(String(s || '')).map((c) => c.charCodeAt(0) & 127).slice(0, max);
      this.u7(chars.length);
      chars.forEach((c) => this.u7(c));
      return this;
    }
    packed(data) {
      for (let g = 0; g < data.length; g += 7) {
        const group = data.slice(g, g + 7);
        let msbs = 0;
        group.forEach((b, i) => { msbs |= ((b >> 7) & 1) << i; });
        this.u7(msbs);
        group.forEach((b) => this.u7(b));
      }
      return this;
    }
    mask64(mask) { // BigInt
      for (let i = 0; i < 10; ++i) this.u7(Number((BigInt(mask) >> BigInt(7 * i)) & 127n));
      return this;
    }
  }

  class Reader {
    constructor(bytes) { this.bytes = bytes; this.pos = 0; }
    u7() {
      if (this.pos >= this.bytes.length) throw new Error('Antwort zu kurz');
      return this.bytes[this.pos++] & 127;
    }
    u14() { const lo = this.u7(); return lo | (this.u7() << 7); }
    u32() { let v = 0; for (let i = 0; i < 5; ++i) v += this.u7() * 2 ** (7 * i); return v % 2 ** 32; }
    str() { const n = this.u7(); let s = ''; for (let i = 0; i < n; ++i) s += String.fromCharCode(this.u7()); return s; }
    packed(n) {
      const out = [];
      while (out.length < n) {
        const msbs = this.u7();
        for (let i = 0; i < 7 && out.length < n; ++i) out.push(this.u7() | (((msbs >> i) & 1) << 7));
      }
      return out;
    }
    mask64() { let m = 0n; for (let i = 0; i < 10; ++i) m |= BigInt(this.u7()) << BigInt(7 * i); return m & ((1n << 64n) - 1n); }
  }

  // Complete MIDI message F0 … F7.
  function buildFrame(device, command, seq, payload = []) {
    if (payload.length > MAX_PAYLOAD) throw new Error('Nutzdaten zu lang');
    const body = [VERSION, device & 127, command & 127, seq & 127, payload.length, ...payload.map((b) => b & 127)];
    return [0xf0, MANUFACTURER, ...PROJECT, ...body, checksum(body), 0xf7];
  }

  // message: full F0 … F7 bytes. Returns null when it is not a Mod on Daisy frame.
  function parseFrame(message) {
    const m = Array.from(message);
    if (m.length < HEADER + 3 || m[0] !== 0xf0 || m[m.length - 1] !== 0xf7) return null;
    const body = m.slice(1, -1);
    if (body[0] !== MANUFACTURER || body[1] !== PROJECT[0] || body[2] !== PROJECT[1]) return null;
    const length = body[7];
    if (body.length !== HEADER + length + 1) throw new Error('Rahmenlänge stimmt nicht');
    if (checksum(body.slice(3, HEADER + length)) !== body[HEADER + length]) throw new Error('Prüfsumme falsch');
    return { version: body[3], device: body[4], command: body[5], seq: body[6], payload: body.slice(HEADER, HEADER + length) };
  }

  // CRC-32/ISO-HDLC, identical to Crc32() in patch.h.
  const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; ++n) {
      let c = n;
      for (let k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();
  function crc32(bytes) {
    let c = 0xffffffff;
    for (const b of bytes) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }

  function defaultPatch() {
    return {
      depth: new Array(CELLS).fill(0),     // index = 8 * source + target, -100 … +100
      enabled: 0n,                          // BigInt, bit 8 * source + target
      lfo: [0, 1].map(() => ({ rate: 6000, shape: 0, sync: 0, gate: 0, division: DEFAULT_MIDI_DIVISION, retrigger: 0 })),
      random: { rate: 8000, mode: 0, sync: 0, gate: 0, division: DEFAULT_MIDI_DIVISION },
      name: '',
    };
  }
  const bit = (cell) => 1n << BigInt(cell);
  const isEnabled = (patch, cell) => (patch.enabled & bit(cell)) !== 0n;
  const isActive = (patch, cell) => isEnabled(patch, cell) && patch.depth[cell] !== 0;
  const activeCount = (patch) => patch.depth.reduce((n, d, cell) => n + (isActive(patch, cell) ? 1 : 0), 0);

  // Byte layout of Serialize() in patch.h (109 bytes).
  function serializePatch(p) {
    const b = [1];
    for (let i = 0; i < CELLS; ++i) b.push(p.depth[i] & 0xff);
    for (let s = 0; s < 8; ++s) b.push(Number((p.enabled >> BigInt(8 * s)) & 0xffn));
    for (const l of p.lfo) b.push(l.rate & 0xff, l.rate >> 8, l.shape, l.sync, l.gate, l.division, l.retrigger);
    const r = p.random;
    b.push(r.rate & 0xff, r.rate >> 8, r.mode, r.sync, r.gate, r.division);
    for (let i = 0; i < 16; ++i) b.push(i < p.name.length ? p.name.charCodeAt(i) & 127 : 0);
    return b;
  }
  const patchCrc = (p) => crc32(serializePatch(p));

  const divisionNames = (sync) => (sync === 2 ? GATE_DIVISIONS : MIDI_DIVISIONS);

  return {
    MANUFACTURER, PROJECT, VERSION, MAX_FRAME, HEADER, MAX_PAYLOAD, BROADCAST, REPLY_BIT, NAK, MONITOR_FRAME,
    CMD, ERRORS, SOURCES, SHAPES, RANDOM_MODES, SYNCS, MIDI_DIVISIONS, GATE_DIVISIONS,
    DEFAULT_MIDI_DIVISION, DEFAULT_GATE_DIVISION, PARAM, RATE_MAX, CELLS, MAX_DEPTH,
    rateToHz, hzToRate, checksum, Writer, Reader, buildFrame, parseFrame, crc32,
    defaultPatch, bit, isEnabled, isActive, activeCount, serializePatch, patchCrc, divisionNames,
  };
});
