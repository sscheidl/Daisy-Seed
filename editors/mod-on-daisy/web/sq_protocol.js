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
    1: 'length', 2: 'checksum', 3: 'unknown type', 4: 'value out of range', 5: 'busy',
    6: 'protocol version', 7: 'another kit (UID)', 8: 'not calibrated', 9: 'not allowed in the current state',
  };
  const TIMEOUT_MS = 200, RETRIES = 3;
  // IDENTIFY capabilities and GET_COMPONENT kinds (SQ 1.1 adds bit 6 and kind 4).
  const CAP = { cv: 1, din: 2, voice: 4, pattern: 8, monitor: 16, calibration: 32, modMatrix: 64 };
  const KIND = { sequencer: 0, instrument: 1, effect: 2, dsp: 3, product: 4 };
  const KIND_NAMES = ['Sequencer', 'Instrument', 'Effect', 'DSP', 'Product'];

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

  const STATE_NAMES = ['STOP', 'WAITING', 'RUNNING', 'PAUSE'];
  const SOURCE_NAMES = ['internal', 'Gate IN 1', 'MIDI USB', 'MIDI DIN'];
  const UNIT_TEXT = ['', '%', 'Hz', 'ms', 'dB', 'st', 'ct', 'BPM', '', ''];
  const MONITOR = { step: 1, state: 2, tempo: 3, revision: 4, param: 5, stats: 6, stepEdit: 7 };
  const noteName = (n) => ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'][n % 12] + (Math.floor(n / 12) + 1);

  const readStep = (r) => {
    const note = r.u7(), gateLength = r.u7(), bits = r.u7();
    return { note, gateLength, gate: !!(bits & 1), accent: !!(bits & 2), slide: !!(bits & 4) };
  };
  const writeStep = (w, s) => w.u7(s.note).u7(s.gateLength).u7((s.gate ? 1 : 0) | (s.accent ? 2 : 0) | (s.slide ? 4 : 0));

  // The Acid Seq panel client (mirror of src/core/SqSession.h): reads the device first
  // (D13), one request at a time with 200 ms timeout and three retries (same request ID),
  // explicit edits only. send(frame) transmits; feed every received SysEx into onMessage().
  class Client {
    constructor(send, { timeoutMs = TIMEOUT_MS, retries = RETRIES, setTimer = setTimeout, clearTimer = clearTimeout } = {}) {
      this.send = send;
      this.timeoutMs = timeoutMs;
      this.retries = retries;
      // Wrapped: browser timers throw "Illegal invocation" when called as methods of
      // another object (Codex F16); Node accepts that, so only a browser shows it.
      this.setTimer = (fn, ms) => setTimer(fn, ms);
      this.clearTimer = (timer) => clearTimer(timer);
      this.rid = 0;
      this.queue = [];
      this.identity = null;
      this.components = [];
      this.pattern = [];
      this.state = { state: 0, mode: 0, sw17: 0, sw18: 1, source: 0, step: -1, gate: false, bpm: 120,
        accepted: 0, applied: 0, cvEnabled: false, usb: false };
      this.cal = [{ valid: false }, { valid: false }];
      this.stats = [];
      this.lost = 0;
      this.ignored = 0;
      this.monitorFrames = 0;
      this.onChange = () => {};
    }
    request(type, payload = []) {
      return new Promise((resolve, reject) => {
        this.rid = (this.rid + 1) & 0x7f;
        this.queue.push({ type, rid: this.rid, frame: encode(type, this.rid, payload), resolve, reject, tries: 0, timer: null });
        if (this.queue.length === 1) this.transmit();
      });
    }
    transmit() {
      const req = this.queue[0];
      if (!req) return;
      req.tries += 1;
      this.send(req.frame);
      req.timer = this.setTimer(() => {
        if (this.queue[0] !== req) return;
        if (req.tries > this.retries) {
          this.queue.shift();
          this.lost += 1;
          req.reject(new Error('no answer'));
          this.transmit();
        } else {
          this.transmit();
        }
      }, this.timeoutMs);
    }
    onMessage(frame) {
      if (protocolOf(frame) !== 'sq') return false;
      const reply = parseReply(frame);
      if (!reply) { this.ignored += 1; return true; }
      if (reply.type === MONITOR_EVENT) { this.monitorFrames += 1; this.monitor(new Reader(reply.payload)); return true; }
      const req = this.queue[0];
      if (!req || reply.rid !== req.rid) { this.ignored += 1; return true; }
      this.clearTimer(req.timer);
      this.queue.shift();
      const r = new Reader(reply.payload);
      if (reply.type === NAK) {
        r.u7();
        const code = r.u7();
        const error = new Error(ERROR_TEXT[code] || 'error ' + code);
        error.code = code;
        req.reject(error);
      } else if (reply.type === (req.type | RESPONSE)) {
        req.resolve(r);
      } else {
        req.reject(new Error('unexpected answer'));
      }
      this.transmit();
      return true;
    }
    async readAll() {
      let r = await this.request(TYPE.identify);
      const major = r.u7(), minor = r.u7();
      const id = { protocol: `${major}.${minor}`, minor, product: r.str(), version: r.str(), bootId: r.u32(),
        uid: [r.u32(), r.u32(), r.u32()], capabilities: r.u14(), componentCount: r.u7() };
      // SQ 1.1, bit 6: the device also speaks Mod on Daisy on this port (Streichwurst).
      id.hasModMatrix = !!(id.capabilities & CAP.modMatrix);
      this.restarted = !!(this.identity && this.identity.bootId !== id.bootId);
      this.identity = id;
      this.components = [];
      for (let c = 0; c < id.componentCount; ++c) {
        r = await this.request(TYPE.getComponent, [c]);
        r.u7(); r.u7();
        const comp = { id: r.str(), kind: r.u7(), version: r.str(), schema: r.u7(), parameters: [] };
        const count = r.u7();
        for (let i = 0; i < count; ++i) {
          const d = await this.request(TYPE.getParamDesc, [c, i]);
          d.u7(); d.u7(); d.u7();
          const p = { id: d.u14(), type: d.u7(), unit: d.u7(), flags: d.u7(), min: d.i32() / 1000, max: d.i32() / 1000,
            def: d.i32() / 1000, cc: d.u7(), key: d.str(), enumKeys: [], value: 0 };
          p.value = p.def;
          if (p.type === 2) {
            for (let v = 0; v <= p.max; ++v) {
              const e = await this.request(TYPE.getEnumLabel, new Writer().u7(c).u14(p.id).u7(v).bytes);
              e.u7(); e.u14(); e.u7();
              p.enumKeys.push(e.str());
            }
          }
          comp.parameters.push(p);
        }
        for (let first = 0; first < count;) {
          const v = await this.request(TYPE.getParams, [c, first]);
          v.u7(); const start = v.u7(); v.u7(); const n = v.u7();
          for (let k = 0; k < n; ++k) {
            const pid = v.u14(), value = v.i32() / 1000;
            const p = comp.parameters.find((q) => q.id === pid);
            if (p) p.value = value;
          }
          first = start + n;
          if (!n) break;
        }
        this.components.push(comp);
      }
      if (this.component('step-seq')) {
        r = await this.request(TYPE.getPattern);
        this.state.accepted = r.u14();
        this.state.applied = r.u14();
        this.pattern = Array.from({ length: 16 }, () => readStep(r));
        await this.readState();
      } else if (this.components.some((c) => c.kind === KIND.product)) {
        await this.readState(); // accepted/applied revision of a product without sequencer
      }
      if (id.capabilities & 32) for (const o of [0, 1]) await this.readCal(o);
      await this.request(TYPE.monitor, [1]);
      this.onChange();
      return this;
    }
    async readState() {
      const r = await this.request(TYPE.getState);
      const s = this.state;
      s.state = r.u7(); s.mode = r.u7(); s.sw17 = r.u7(); s.sw18 = r.u7(); s.source = r.u7();
      const step = r.u7();
      s.step = step > 15 ? -1 : step;
      s.bpm = r.i32() / 1000; s.accepted = r.u14(); s.applied = r.u14(); s.cvEnabled = !!r.u7(); s.usb = !!r.u7();
    }
    async readCal(output) {
      const r = await this.request(TYPE.getCal, [output]);
      r.u7();
      this.cal[output] = { valid: !!r.u7(), offset: r.i32() / 1000, scale: r.i32() / 1000, gateLow: r.u14(),
        gateHigh: r.u14(), clampMin: r.u14(), clampMax: r.u14() };
    }
    component(id) { return this.components.find((c) => c.id === id); }
    componentIndex(id) { return this.components.findIndex((c) => c.id === id); }
    async setParam(componentId, key, value) {
      const ci = this.componentIndex(componentId);
      const p = ci >= 0 && this.components[ci].parameters.find((q) => q.key === key);
      if (!p || p.flags & 1) throw new Error('parameter not writable: ' + componentId + '.' + key);
      if (typeof value !== 'number' || !Number.isFinite(value)) throw new Error('invalid value for ' + key);
      // Into the descriptor's range before encoding (Codex F07): no i32 wrap-around.
      const clamped = Math.min(p.max, Math.max(p.min, value));
      const r = await this.request(TYPE.setParam, new Writer().u7(ci).u14(p.id).i32(Math.round(clamped * 1000)).bytes);
      r.u7(); r.u14();
      p.value = r.i32() / 1000;
      this.state.accepted = r.u14();
      this.onChange();
      return p.value;
    }
    async setStep(index, step) {
      const w = new Writer().u7(index);
      writeStep(w, step);
      const r = await this.request(TYPE.setStep, w.bytes);
      r.u7();
      this.state.accepted = r.u14();
      this.pattern[index] = { ...step };
      this.onChange();
    }
    async setPattern(steps) {
      const w = new Writer();
      steps.forEach((s) => writeStep(w, s));
      const r = await this.request(TYPE.setPattern, w.bytes);
      r.u7();
      this.state.accepted = r.u14();
      this.pattern = steps.map((s) => ({ ...s }));
      this.onChange();
    }
    async transport(start) { await this.request(TYPE.transport, [start ? 1 : 0]); }
    uidBytes() { const w = new Writer(); this.identity.uid.forEach((u) => w.u32(u)); return w; }
    async setCal(output, c) {
      const w = this.uidBytes().u7(output).i32(Math.round(c.offset * 1000)).i32(Math.round(c.scale * 1000))
        .u14(c.gateLow).u14(c.gateHigh).u14(c.clampMin).u14(c.clampMax);
      await this.request(TYPE.setCal, w.bytes);
      this.cal[output] = { ...c, valid: true };
      this.state.cvEnabled = false;
      this.onChange();
    }
    async cvEnable(on) {
      await this.request(TYPE.cvEnable, this.uidBytes().u7(on ? 1 : 0).bytes);
      this.state.cvEnabled = on;
      this.onChange();
    }
    async readStats() {
      const r = await this.request(TYPE.getStats);
      const n = r.u7();
      this.stats = Array.from({ length: n }, () => r.u32());
      return this.stats;
    }
    monitor(r) {
      const s = this.state;
      const code = r.u7();
      if (code === MONITOR.step) { const step = r.u7(); s.step = step > 15 ? -1 : step; s.gate = !!r.u7(); }
      else if (code === MONITOR.state) { s.state = r.u7(); s.mode = r.u7(); s.sw17 = r.u7(); s.sw18 = r.u7(); s.source = r.u7(); }
      else if (code === MONITOR.tempo) s.bpm = r.i32() / 1000;
      else if (code === MONITOR.revision) { s.accepted = r.u14(); s.applied = r.u14(); }
      else if (code === MONITOR.param) {
        const c = this.components[r.u7()], pid = r.u14(), value = r.i32() / 1000;
        const p = c && c.parameters.find((q) => q.id === pid);
        if (p) p.value = value;
      } else if (code === MONITOR.stepEdit) { const i = r.u7(); this.pattern[i] = readStep(r); }
      this.onChange();
    }
  }

  // ---- product patch (.acidpatch), same format and text as src/core/AcidPatch.h ----
  const number = (v) => {
    let s = (Math.round(v * 1000) / 1000).toFixed(3).replace(/0+$/, '').replace(/\.$/, '');
    return s === '-0' ? '0' : s;
  };
  // File format by product: Streichwurst has its own name and may embed the matrix.
  const patchFormat = (product) => (product === 'streichwurst' ? 'streichwurst-patch' : 'acid-seq-patch');
  const patchExtension = (product) => (product === 'streichwurst' ? 'swpatch' : 'acidpatch');
  function patchFromClient(client, matrix = null) {
    return {
      matrix,
      product: client.identity ? client.identity.product : '',
      components: client.components.map((c) => ({ id: c.id, version: c.version, schema: c.schema,
        params: Object.fromEntries(c.parameters.filter((p) => !(p.flags & 1)).map((p) => [p.key, p.value])) })),
      pattern: client.component('step-seq') ? client.pattern.map((s) => ({ ...s })) : null,
    };
  }
  function serializePatch(p) {
    const q = JSON.stringify;
    let out = '{\n  "format": "' + patchFormat(p.product) + '",\n  "format_version": 1,\n  "product": ' + q(p.product) +
      ',\n  "components": [';
    p.components.forEach((c, i) => {
      const keys = Object.keys(c.params).sort();
      out += (i ? ',' : '') + '\n    {"id": ' + q(c.id) + ', "version": ' + q(c.version) + ', "schema": ' + c.schema +
        ', "params": {' + keys.map((k) => q(k) + ': ' + number(c.params[k])).join(', ') + '}}';
    });
    out += '\n  ]';
    if (p.pattern) {
      out += ',\n  "pattern": [' + p.pattern.map((s, i) => (i ? ',' : '') + '\n    {"note": ' + s.note + ', "gate_length": ' +
        s.gateLength + ', "gate": ' + s.gate + ', "accent": ' + s.accent + ', "slide": ' + s.slide + '}').join('') + '\n  ]';
    }
    if (p.matrix) out += ',\n  "matrix": ' + JSON.stringify(p.matrix);
    return out + '\n}\n';
  }
  // Whole number within [lo, hi]; checked before use (Codex F07: 1e999, 3.5, strings).
  const isInt = (v, lo, hi) => typeof v === 'number' && Number.isInteger(v) && v >= lo && v <= hi;
  function parsePatch(text) {
    let root;
    try { root = JSON.parse(text); } catch (e) { throw new Error('not a valid JSON file'); }
    if (!root || typeof root !== 'object' || !['acid-seq-patch', 'streichwurst-patch'].includes(root.format) ||
        !isInt(root.format_version, 1, 1e6)) {
      throw new Error('not an Acid Seq or Streichwurst patch');
    }
    if (root.format === 'streichwurst-patch' && root.product !== 'streichwurst') {
      throw new Error('Streichwurst patch for another product');
    }
    if (root.matrix !== undefined && (!root.matrix || typeof root.matrix !== 'object' || Array.isArray(root.matrix))) {
      throw new Error('invalid matrix');
    }
    if (root.format_version > 1) throw new Error('Patch format ' + root.format_version + ' is newer than this editor');
    if (root.pattern !== undefined && (!Array.isArray(root.pattern) || root.pattern.length !== 16)) {
      throw new Error('pattern needs 16 steps');
    }
    const pattern = root.pattern ? root.pattern.map((s, i) => {
      const note = s && s.note !== undefined ? s.note : 0, gateLength = s && s.gate_length !== undefined ? s.gate_length : 50;
      if (!s || typeof s !== 'object' || !isInt(note, 0, 59) || !isInt(gateLength, 5, 100)) throw new Error('invalid step ' + (i + 1));
      return { note, gateLength, gate: !!s.gate, accent: !!s.accent, slide: !!s.slide };
    }) : null;
    if (root.components !== undefined && !Array.isArray(root.components)) throw new Error('not an Acid Seq patch');
    const components = (root.components || []).map((c) => {
      if (!c || typeof c.id !== 'string' || !c.id) throw new Error('component without ID');
      if (c.schema !== undefined && !isInt(c.schema, 0, 127)) throw new Error('invalid schema version');
      const params = {};
      for (const [k, v] of Object.entries(c.params && typeof c.params === 'object' ? c.params : {})) {
        if (typeof v === 'boolean') params[k] = v ? 1 : 0;
        else if (typeof v === 'number' && Number.isFinite(v) && Math.abs(v) <= 2147483) params[k] = v;
        else throw new Error('invalid value for ' + k);
      }
      return { id: c.id, version: c.version, schema: c.schema, params };
    });
    return { product: root.product || '', components, pattern, matrix: root.matrix || null };
  }
  function checkPatch(p, client) {
    const result = { missing: [], incompatible: [], unknown: [], patternSkipped: false };
    for (const c of p.components) {
      const dev = client.component(c.id);
      if (!dev) { result.missing.push(c.id); continue; }
      if (dev.schema !== c.schema) { result.incompatible.push(c.id); continue; }
      for (const k of Object.keys(c.params)) {
        const q = dev.parameters.find((x) => x.key === k);
        if (!q || q.flags & 1) result.unknown.push(c.id + '.' + k);
      }
    }
    result.patternSkipped = !!p.pattern && !client.component('step-seq');
    result.clean = !result.missing.length && !result.incompatible.length && !result.unknown.length && !result.patternSkipped;
    return result;
  }
  async function uploadPatch(p, client) {
    let sent = 0;
    for (const c of p.components) {
      const dev = client.component(c.id);
      if (!dev || dev.schema !== c.schema) continue;
      for (const [k, v] of Object.entries(c.params)) {
        const q = dev.parameters.find((x) => x.key === k);
        if (!q || q.flags & 1) continue;
        await client.setParam(c.id, k, v);
        sent += 1;
      }
    }
    if (p.pattern && client.component('step-seq')) { await client.setPattern(p.pattern); sent += 1; }
    return sent;
  }

  return {
    HEADER, VERSION, VERSION_MINOR, MAX_FRAME, OVERHEAD, MAX_PAYLOAD, RESPONSE, MONITOR_EVENT, NAK,
    TYPE, ERROR, ERROR_TEXT, TIMEOUT_MS, RETRIES, STATE_NAMES, SOURCE_NAMES, UNIT_TEXT, MONITOR,
    crc8, Writer, Reader, encode, assemble, classify, parseReply, protocolOf, Session, Client, noteName,
    patchFromClient, serializePatch, parsePatch, checkPatch, uploadPatch, patchFormat, patchExtension, CAP, KIND, KIND_NAMES,
  };
});
