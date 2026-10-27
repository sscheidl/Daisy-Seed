// Node test of the browser SQ codec against the shared vectors
// (sequencers/step-seq/contracts/sq_vectors.json) and the handshake. Run: node sq_protocol.test.cjs
'use strict';
const assert = require('assert');
const path = require('path');
const fs = require('fs');
const S = require('../sq_protocol.js');

const vectors = JSON.parse(fs.readFileSync(
  path.join(__dirname, '../../../../sequencers/step-seq/contracts/sq_vectors.json'), 'utf8'));
const hex = (s) => s.split(' ').map((b) => parseInt(b, 16));

assert.strictEqual(S.crc8(Array.from(Buffer.from('123456789'))), 0xf4);
assert.strictEqual(vectors.crc8_check.crc, 0xf4);
let cases = 0;
for (const c of vectors.cases) {
  const frame = hex(c.frame);
  if (c.stream) assert.deepStrictEqual(S.assemble(hex(c.stream)), { frame, realtime: c.realtime_bytes }, c.name);
  const v = S.classify(frame);
  assert.strictEqual(v.verdict, c.verdict, c.name);
  if (v.verdict === 'nak') assert.strictEqual(v.error, c.error, c.name);
  if (c.payload !== undefined && v.verdict === 'ok') {
    assert.deepStrictEqual(v.payload, c.payload ? hex(c.payload) : [], c.name);
    assert.deepStrictEqual(S.encode(c.type, c.rid, v.payload), frame, c.name);
  }
  if (c.payload !== undefined) {
    // Reply-class vectors parse as replies with the same payload.
    const reply = S.parseReply(frame);
    if (c.type & S.RESPONSE) assert.deepStrictEqual(reply.payload, c.payload ? hex(c.payload) : [], c.name);
  }
  cases += 1;
}
assert.ok(cases >= 25);

// Field codecs.
const w = new S.Writer().u14(0x3fff).u32(0xcafebabe).i32(-7000).str('tempo', 16);
const r = new S.Reader(w.bytes);
assert.strictEqual(r.u14(), 0x3fff);
assert.strictEqual(r.u32(), 0xcafebabe);
assert.strictEqual(r.i32(), -7000);
assert.strictEqual(r.str(), 'tempo');
assert.ok(r.done());
assert.throws(() => new S.Writer().str('Länge', 16));

// Dispatcher.
assert.strictEqual(S.protocolOf([0xf0, 0x7d, 0x4d, 0x44, 1, 0xf7]), 'md');
assert.strictEqual(S.protocolOf([0xf0, 0x7d, 0x53, 0x51, 1, 0xf7]), 'sq');
assert.strictEqual(S.protocolOf([0xf0, 0x7d, 0x42, 0x4c, 1, 0x41, 0xf7]), 'bootloader');
assert.strictEqual(S.protocolOf([0xf0, 0x7e, 0x7f, 0xf7]), 'unknown');

// Handshake against a minimal simulated SQ device (same reply layout as the firmware).
function device(frame) {
  const v = S.classify(frame);
  if (v.verdict !== 'ok') return null;
  const w2 = new S.Writer();
  if (v.type === S.TYPE.identify) {
    w2.u7(1).u7(0).str('acid-seq', 16).str('0.1.0', 12).u32(0x1234).u32(1).u32(2).u32(3).u14(0x3f).u7(2);
  } else if (v.type === S.TYPE.getComponent) {
    const i = v.payload[0];
    w2.u7(i).u7(2).str(['step-seq', 'acid-303'][i], 16).u7(i).str('0.1.0', 12).u7(1).u7(19);
  } else {
    return S.encode(S.NAK, v.rid, [v.type, S.ERROR.unknownType]);
  }
  return S.encode(v.type | S.RESPONSE, v.rid, w2.bytes);
}
let now = 0, drop = 0;
const sent = [];
const session = new S.Session((f) => {
  sent.push(f);
  if (drop > 0) { drop -= 1; return; }
  const reply = device(f);
  if (reply) setImmediate(() => session.onMessage(reply, now));
});
assert.strictEqual(session.onMessage([0xf0, 0x7d, 0x4d, 0x44, 1, 0xf7], 0), false); // MD is not SQ
drop = 1;
session.connect(now);
const timer = setInterval(() => {
  now += 10;
  session.tick(now);
  if (session.phase === 'ready' || session.phase === 'failed' || now > 2000) {
    clearInterval(timer);
    assert.strictEqual(session.phase, 'ready');
    assert.strictEqual(session.identity.product, 'acid-seq');
    assert.deepStrictEqual(session.components.map((c) => c.id), ['step-seq', 'acid-303']);
    assert.deepStrictEqual(sent[0], sent[1]); // the retry repeats the same frame (request ID)
    assert.ok(sent.every((f) => f[5] === S.TYPE.identify || f[5] === S.TYPE.getComponent));
    console.log('PASS: browser SQ codec matches the shared vectors, dispatcher, handshake with retry');
  }
}, 1);
