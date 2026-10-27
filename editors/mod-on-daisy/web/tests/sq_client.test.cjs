// Node test of the browser SQ client and the .acidpatch functions against the firmware's
// SQ device (tests/sq_sim.cpp around SqSimulator). Run: node sq_client.test.cjs <sq_sim>
'use strict';
const assert = require('assert');
const { spawn } = require('child_process');
const S = require('../sq_protocol.js');

const SIM = process.argv[2];

function startSim(product) {
  const proc = spawn(SIM, [product], { stdio: ['pipe', 'pipe', 'inherit'] });
  let buffer = '';
  const waiting = [];
  let collected = [];
  proc.stdout.on('data', (chunk) => {
    buffer += chunk;
    let i;
    while ((i = buffer.indexOf('\n')) >= 0) {
      const line = buffer.slice(0, i).replace(/\r$/, '');
      buffer = buffer.slice(i + 1);
      if (line === 'OK') { waiting.shift()(collected); collected = []; } else collected.push(line);
    }
  });
  const command = (line) => new Promise((resolve) => { waiting.push(resolve); proc.stdin.write(line + '\n'); });
  return { proc, command };
}

const hex = (bytes) => bytes.map((b) => b.toString(16).padStart(2, '0')).join('');
const bytes = (h) => h.match(/../g).map((b) => parseInt(b, 16));

function connect(sim, { drop = 0 } = {}) {
  const sent = [];
  let dropped = 0;
  const client = new S.Client((frame) => {
    sent.push(frame);
    if (dropped < drop) { dropped += 1; return; }
    sim.command('S ' + hex(frame)).then((lines) => lines.filter((l) => l.startsWith('R ')).forEach((l) => client.onMessage(bytes(l.slice(2)))));
  }, { timeoutMs: 50 });
  return { client, sent };
}

(async () => {
  const sim = startSim('acid-seq');
  const { client, sent } = connect(sim, { drop: 1 }); // the first request is lost: retried
  await client.readAll();
  assert.deepStrictEqual(sent[0], sent[1]);
  assert.strictEqual(client.identity.product, 'acid-seq');
  assert.deepStrictEqual(client.components.map((c) => c.id), ['step-seq', 'acid-303', 'drive', 'stereo-delay']);
  const tempo = client.component('step-seq').parameters.find((p) => p.key === 'tempo');
  assert.strictEqual(tempo.value, 120);
  assert.deepStrictEqual(client.component('step-seq').parameters.find((p) => p.key === 'clock_source').enumKeys,
    ['internal', 'gate_in_1', 'midi', 'auto']);
  assert.strictEqual(client.component('stereo-delay').parameters.find((p) => p.key === 'note_value').enumKeys.length, 18);
  assert.strictEqual(client.pattern.length, 16);
  // Reading wrote nothing: only reads went out (D13).
  const writes = [S.TYPE.setParam, S.TYPE.setPattern, S.TYPE.setStep, S.TYPE.transport, S.TYPE.setCal, S.TYPE.cvEnable];
  assert.ok(sent.every((f) => !writes.includes(f[5])));
  // Edits.
  assert.strictEqual(await client.setParam('acid-303', 'cutoff', 1234.5), 1234.5);
  assert.strictEqual(await client.setParam('step-seq', 'length', 7.4), 7);
  await assert.rejects(client.setParam('step-seq', 'play_mode', 1));
  await client.setStep(4, { note: 40, gateLength: 80, gate: true, accent: true, slide: false });
  await client.transport(false);
  await client.transport(true);
  await assert.rejects(client.cvEnable(true), (e) => e.code === S.ERROR.notCalibrated);
  const cal = { offset: 2048, scale: -409.6, gateLow: 2048, gateHigh: 410, clampMin: 0, clampMax: 4095 };
  await client.setCal(0, cal);
  await client.setCal(1, cal);
  await client.cvEnable(true);
  assert.ok(client.state.cvEnabled);
  // Monitor: steps and a knob on the kit.
  await sim.command('K 0 1 150000').then((l) => l.filter((x) => x.startsWith('R ')).forEach((x) => client.onMessage(bytes(x.slice(2)))));
  const frames = client.monitorFrames;
  const lines = await sim.command('T 600');
  lines.filter((l) => l.startsWith('R ')).forEach((l) => client.onMessage(bytes(l.slice(2))));
  assert.ok(client.monitorFrames > frames + 3 && client.state.step >= 0);
  assert.strictEqual(tempo.value, 150);
  assert.strictEqual((await client.readStats()).length, 17);
  // Patch: save, parse, identical text, explicit upload to a second kit.
  const text = S.serializePatch(S.patchFromClient(client));
  const patch = S.parsePatch(text);
  assert.strictEqual(S.serializePatch(patch), text);
  assert.strictEqual(patch.components[1].params.cutoff, 1234.5);
  assert.strictEqual(patch.pattern[4].note, 40);
  const other = startSim('acid-seq');
  const second = connect(other).client;
  await second.readAll();
  assert.ok(S.checkPatch(patch, second).clean);
  const count = await S.uploadPatch(patch, second);
  assert.ok(count > 40);
  assert.strictEqual(second.component('acid-303').parameters.find((p) => p.key === 'cutoff').value, 1234.5);
  // A drive-only device: other components missing, pattern skipped.
  const fx = startSim('drive-fx');
  const third = connect(fx).client;
  await third.readAll();
  const compat = S.checkPatch(patch, third);
  assert.deepStrictEqual(compat.missing, ['step-seq', 'acid-303', 'stereo-delay']);
  assert.ok(compat.patternSkipped && !compat.clean);
  assert.throws(() => S.parsePatch('{"format":"other"}'));
  assert.throws(() => S.parsePatch('{"format":"acid-seq-patch","format_version":9}'));
  for (const s of [sim, other, fx]) { s.proc.stdin.write('Q\n'); }
  console.log('PASS: browser SQ client against the firmware device (retry, read-only connect, edits, NAK, '
    + 'calibration/enable, monitor, stats) and .acidpatch (round trip, explicit upload, compatibility)');
})().catch((e) => { console.error(e); process.exit(1); });
