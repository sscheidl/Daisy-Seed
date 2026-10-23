// Node test of the browser codec against the C++ reference values
// (tests/test_mod.cpp in modules/mod-on-daisy). Run: node protocol.test.cjs
'use strict';
const assert = require('assert');
const P = require('../mod_protocol.js');

assert.strictEqual(P.crc32(Array.from(Buffer.from('123456789'))), 0xcbf43926);

// Patch test vector of test_mod.cpp / mod_probe.py: LFO1->0 +35, Channel AT->4 -60, "Vector".
const p = P.defaultPatch();
p.depth[0] = 35;
p.depth[6 * 8 + 4] = -60;
p.enabled = P.bit(0) | P.bit(6 * 8 + 4);
p.name = 'Vector';
assert.strictEqual(P.serializePatch(p).length, 109);
assert.strictEqual(P.patchCrc(p), 0xe2340cc6);
assert.strictEqual(P.activeCount(p), 2);

// The device starts with this CRC (empty patch, default source parameters; mod_sim "D").
assert.strictEqual(P.patchCrc(P.defaultPatch()), 0x61c264c4);
assert.notStrictEqual(P.MONITOR_FRAME, P.CMD.monitor | P.REPLY_BIT);

// Frames: sizes from PROTOCOL.md, round trip, corruption detection.
const grid = Array.from({ length: 64 }, (_, i) => ((i * 7) % 201 - 100) & 0xff);
const w = new P.Writer().packed(grid).mask64((1n << 63n) | 5n);
assert.strictEqual(w.bytes.length, 84);
const frame = P.buildFrame(0, P.CMD.setMatrix, 9, w.bytes);
assert.strictEqual(frame.length, 95);
const parsed = P.parseFrame(frame);
assert.strictEqual(parsed.command, P.CMD.setMatrix);
assert.strictEqual(parsed.seq, 9);
const r = new P.Reader(parsed.payload);
assert.deepStrictEqual(r.packed(64), grid);
assert.strictEqual(r.mask64(), (1n << 63n) | 5n);
const bad = frame.slice();
bad[20] ^= 1;
assert.throws(() => P.parseFrame(bad));
assert.strictEqual(P.parseFrame([0xf0, 0x43, 0x10, 0x4c, 0, 0, 0, 0, 0, 0, 0xf7]), null); // foreign SysEx

// u32 and strings.
const s = new P.Reader(new P.Writer().u32(0xe2340cc6).str('physical-modeling', 24).u14(16383).bytes);
assert.strictEqual(s.u32(), 0xe2340cc6);
assert.strictEqual(s.str(), 'physical-modeling');
assert.strictEqual(s.u14(), 16383);

// Rate mapping as in RateToHz().
assert(Math.abs(P.rateToHz(0) - 0.02) < 1e-9 && Math.abs(P.rateToHz(16383) - 20) < 1e-9);
assert.strictEqual(P.hzToRate(P.rateToHz(9000)), 9000);
console.log('PASS: browser codec matches the C++ reference (CRC E2340CC6, frame sizes, packing)');
