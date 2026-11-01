// DDP framing and PM* helpers shared by panel.html and the node test. Plain script
// (no module) so file:// works; exports to window.PM or module.exports.
(function (root) {
  'use strict';
  const MAX_BODY = 400;

  function crc16(text) {  // CRC-16/CCITT-FALSE over the ASCII body
    let crc = 0xFFFF;
    for (let i = 0; i < text.length; i++) {
      crc ^= text.charCodeAt(i) << 8;
      for (let b = 0; b < 8; b++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
    }
    return crc;
  }

  function frame(body) {
    if (!body || body.length > MAX_BODY || /[^\x20-\x7e]|[$*]/.test(body)) throw new Error('invalid body: ' + body);
    return '$' + body + '*' + crc16(body).toString(16).toUpperCase().padStart(4, '0') + '\n';
  }

  // Byte-wise receiver with resynchronisation on '$'.
  class Parser {
    constructor() { this.buf = null; this.bad = 0; }
    feed(text) {
      const out = [];
      for (const ch of text) {
        if (ch === '$') { if (this.buf) this.bad++; this.buf = ''; continue; }
        if (this.buf === null) continue;
        if (ch === '\n') {
          const t = this.buf.replace(/\r$/, ''); this.buf = null;
          const star = t.lastIndexOf('*');
          const body = star >= 0 ? t.slice(0, star) : '';
          const ok = star >= 0 && t.length - star === 5 && /^[0-9a-fA-F]{4}$/.test(t.slice(star + 1))
            && parseInt(t.slice(star + 1), 16) === crc16(body) && body.length > 0;
          if (ok) out.push(body); else this.bad++;
          continue;
        }
        this.buf += ch;
        if (this.buf.length > MAX_BODY + 6) { this.buf = null; this.bad++; }
      }
      return out;
    }
  }

  function fields(body) {
    const parts = body.split(' ');
    const out = { _type: parts[0] };
    for (const p of parts.slice(1)) { const i = p.indexOf('='); if (i > 0) out[p.slice(0, i)] = p.slice(i + 1); }
    return out;
  }

  function decodeName(text) {
    try { return decodeURIComponent(text); } catch (e) { return text; }
  }

  function isManager(f) {
    return f && f.proto === '1' && typeof f.app === 'string' && f.app.indexOf('DaisyProgramManager') === 0;
  }

  // Slots occupied in a PMLIST mask (hex, bit i = slot i+1).
  function slotsOf(mask) {
    const m = parseInt(mask || '0', 16) || 0;
    const out = [];
    for (let s = 1; s <= 15; s++) if (m & (1 << (s - 1))) out.push(s);
    return out;
  }

  function startBody(entry, gen, rid) {
    const hash = (entry.hash || '').slice(0, 32).toUpperCase();
    if (hash.length < 16) throw new Error('entry has no usable hash');
    return 'PMSTART slot=' + entry.slot + ' id=' + entry.id + ' hash=' + hash + ' gen=' + gen + ' rid=' + rid;
  }

  const api = { MAX_BODY, crc16, frame, Parser, fields, decodeName, isManager, slotsOf, startBody };
  if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.PM = api;
})(typeof window !== 'undefined' ? window : globalThis);
