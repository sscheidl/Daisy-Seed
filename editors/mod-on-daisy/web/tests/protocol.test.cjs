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
// Format 2 (LFO cross modulation): reference CRCs of test_mod.cpp / mod_probe.py.
assert.strictEqual(P.patchCrc(P.defaultPatch(2)), 0xc422b385);
const v2 = P.defaultPatch(2);
v2.depth[0] = 35; v2.depth[6 * 8 + 4] = -60;
v2.enabled = P.bit(0) | P.bit(6 * 8 + 4);
v2.name = 'Vector';
v2.lfo[0].fm = -37; v2.lfo[1].fm = 100;
assert.strictEqual(P.serializePatch(v2).length, 111);
assert.strictEqual(P.patchCrc(v2), 0x9d6e020e);

// Monitor frames of all three sizes.
const enc = (v) => { const x = Math.round(v * 8191) + 8192; return [x & 127, x >> 7]; };
const plain = [...Array(18)].flatMap((_, i) => enc(i === 0 ? 0.5 : 0));
assert.strictEqual(P.decodeMonitor(plain).hasRange, false);
assert(Math.abs(P.decodeMonitor(plain).source[0] - 0.5) < 1e-3);
const ext = [...plain, ...[...Array(8)].flatMap(() => enc(0.25)), ...[...Array(8)].flatMap(() => enc(-1)),
  ...[...Array(8)].flatMap(() => enc(1)), 1, 2, 60, 90, 3];
assert.strictEqual(ext.length, 89);
const m = P.decodeMonitor(ext);
const near = (a, b) => Math.abs(a - b) < 1e-3;
assert(m.hasRange && near(m.base[3], 0.25) && near(m.min[0], -1) && near(m.max[7], 1));
assert.deepStrictEqual([m.clocks, m.atKind, m.atNote, m.atValue, m.atAge], [1, 2, 60, 90, 3]);
assert.strictEqual(P.clockState(m, 1, 0), 1);
assert.strictEqual(P.clockState(m, 2, 0), 0);
assert.deepStrictEqual([m.mode, m.modeOrigin], [-1, -1]); // 89 bytes: no instrument mode
const withMode = P.decodeMonitor([...ext, 4, 1]); // 91 bytes: Western Chords, from MIDI
assert.deepStrictEqual([withMode.mode, withMode.modeOrigin], [4, 1]);
assert.deepStrictEqual([P.decodeMonitor([...ext, 127, 127]).mode, P.decodeMonitor([...ext, 127, 127]).modeOrigin], [-1, -1]);

// Column header values, as Column::ValueLabel in the JUCE editor.
const pitch = { spanText: '+-12 st', span: 0.25 }, level = { spanText: '+-100%', span: 0.5 }, plainCol = { spanText: '100%', span: 1 };
assert.deepStrictEqual([P.valueLabel(plainCol, 0.63), P.valueLabel(plainCol, 0), P.valueLabel(plainCol, 1)], ['63 %', '0 %', '100 %']);
assert.deepStrictEqual([P.valueLabel(pitch, 0.5), P.valueLabel(pitch, 0.75), P.valueLabel(pitch, 0)], ['+0.0 st', '+12.0 st', '-24.0 st']);
assert.deepStrictEqual([P.valueLabel(level, 0.5), P.valueLabel(level, 1)], ['+0 %', '+100 %']);

// Goal range: pot 0.1 / 0.5 / 0.9 with LFO +-1 at depth 50 -> 0..0.6 / 0..1 / 0.4..1.
const targets = Array.from({ length: 8 }, () => ({ present: true, perVoice: false, span: 1, name: 'X' }));
const gp = P.defaultPatch();
gp.depth[1] = 50; gp.enabled = P.bit(1);
for (const [base, lo, hi] of [[0.1, 0, 0.6], [0.5, 0, 1], [0.9, 0.4, 1]]) {
  const mm = { ...m, base: [0, base, 0, 0, 0, 0, 0, 0] };
  const g = P.goalSpan(gp, targets, mm, 1);
  assert(Math.abs(g.lo - lo) < 1e-3 && Math.abs(g.hi - hi) < 1e-3, JSON.stringify(g));
}

// Compatibility: the review's Reverb file on the Level build.
const dev = { key: 'physical-modeling', schema: 0x942b2d07, caps: 0x7a };
const legacy = P.defaultPatch();
legacy.depth[5 * 8 + 6] = -100; legacy.depth[1] = 30;
legacy.enabled = P.bit(5 * 8 + 6) | P.bit(1);
const now = ['Pitch', 'Structure', 'Brightness', 'Damping', 'Position', 'Excitation', 'Level', 'Dry/Wet'].map((name) => ({ present: true, name }));
const old = now.map((t) => t.name); old[6] = 'Reverb';
const c = P.checkCompatibility({ instrument_key: 'physical-modeling', schema_hash: 'E337FC28', columns: old, patch: legacy }, now, dev);
assert(c.schemaChanged && c.needsDecision && c.affected === P.bit(5 * 8 + 6) && /Reverb/.test(c.notes[0]));
assert(!P.isEnabled(P.safePart(legacy, c), 5 * 8 + 6) && P.isActive(P.safePart(legacy, c), 1));
assert(!P.checkCompatibility({ instrument_key: 'physical-modeling', schema_hash: '942B2D07', columns: old, patch: legacy }, now, dev).needsDecision);
assert(P.checkCompatibility({ patch: legacy }, now, dev).schemaUnknown);
assert(P.checkCompatibility({ instrument_key: 'toy-piano', patch: legacy }, now, dev).keyMismatch);
assert(P.checkCompatibility({ instrument_key: 'toy-piano', schema_hash: '0', patch: P.defaultPatch(), hasFm: true }, now,
  { key: 'toy-piano', schema: 0, caps: 0x0a }).fmUnsupported);

// Firmware update frames (common/bootloader/README.md, tests/test_bootloader_entry.cpp).
assert.deepStrictEqual(P.BOOTLOADER_REQUEST, [0xf0, 0x7d, 0x42, 0x4c, 0x01, 0x01, 0x42, 0x4f, 0x4f, 0x54, 0xf7]);
assert.deepStrictEqual(P.BOOTLOADER_ACK, [0xf0, 0x7d, 0x42, 0x4c, 0x01, 0x41, 0xf7]);
assert(P.isBootloaderAck(Uint8Array.from(P.BOOTLOADER_ACK)));
assert(!P.isBootloaderAck(P.BOOTLOADER_REQUEST) && !P.isBootloaderAck(P.BOOTLOADER_ACK.slice(0, 6)));
assert.strictEqual(P.parseFrame(P.BOOTLOADER_REQUEST), null); // not an MD frame: the link ignores both
assert.strictEqual(P.parseFrame(P.BOOTLOADER_ACK), null);

console.log('PASS: browser codec matches the C++ reference (CRC E2340CC6, format 2 9D6E020E, frame sizes, packing, monitor incl. mode, value labels, goal span, compatibility, bootloader frames)');
