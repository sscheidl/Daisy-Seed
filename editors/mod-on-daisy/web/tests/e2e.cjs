// End-to-end test of the browser editor against the real Mod on Daisy controller.
// Chromium (Playwright) gets a fake Web MIDI port whose traffic goes to mod_sim,
// the host build of the firmware's Controller/Engine with an instrument's columns.
//
//   node tests/e2e.cjs <path to mod_sim> [screenshot dir]
// Needs Playwright with Chromium (preinstalled in the cloud environment).
'use strict';
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const P = require('../mod_protocol.js');

const SIM = process.argv[2];
const SHOTS = process.argv[3];
const PAGE = 'file://' + path.resolve(__dirname, '..', 'index.html');

function startSim(instrument) {
  const proc = spawn(SIM, [instrument], { stdio: ['pipe', 'pipe', 'inherit'] });
  let buffer = '';
  const waiting = [];
  let collected = [];
  proc.stdout.on('data', (chunk) => {
    buffer += chunk;
    let i;
    while ((i = buffer.indexOf('\n')) >= 0) {
      const line = buffer.slice(0, i);
      buffer = buffer.slice(i + 1);
      if (line === 'OK') { waiting.shift()(collected); collected = []; } else collected.push(line);
    }
  });
  // Commands are answered strictly in order.
  const command = (line) => new Promise((resolve) => { waiting.push(resolve); proc.stdin.write(line + '\n'); });
  return { proc, command };
}

async function openEditor(browser, sim) {
  const page = await browser.newPage({ viewport: { width: 1280, height: 1000 } });
  page.on('pageerror', (e) => { throw e; });
  await page.exposeFunction('__simSend', async (hex) => (await sim.command('S ' + hex)).filter((l) => l.startsWith('R ')).map((l) => l.slice(2)));
  await page.addInitScript(() => {
    const hexToBytes = (h) => Uint8Array.from(h.match(/../g).map((b) => parseInt(b, 16)));
    // Instrumented ports: open/close counts, an input another application holds, a link
    // that delivers nothing (port taken after connecting, cable pulled).
    window.__ports = { outOpen: 0, outClose: 0, inOpen: 0, inClose: 0, inTaken: false, silent: false };
    const input = { id: 'in', name: 'Daisy Seed External (Sim)', state: 'connected', onmidimessage: null,
      open: async () => { if (window.__ports.inTaken) throw new Error('belegt'); window.__ports.inOpen++; },
      close: async () => { window.__ports.inClose++; } };
    window.__deliver = (hex) => !window.__ports.silent && input.onmidimessage && input.onmidimessage({ data: hexToBytes(hex) });
    const output = {
      id: 'out', name: 'Daisy Seed External (Sim)', state: 'connected',
      open: async () => { window.__ports.outOpen++; }, close: async () => { window.__ports.outClose++; },
      send(data) {
        const hex = Array.from(data, (b) => b.toString(16).padStart(2, '0')).join('');
        window.__simSend(hex).then((replies) => replies.forEach((r) => window.__deliver(r)));
      },
    };
    navigator.requestMIDIAccess = async (opts) => {
      if (!opts || !opts.sysex) throw new Error('sysex required');
      return { inputs: new Map([['in', input]]), outputs: new Map([['out', output]]), onstatechange: null };
    };
  });
  await page.goto(PAGE);
  return page;
}

// Advance the simulated device clock so monitor frames are produced.
function pumpTime(page, sim) {
  let running = true;
  (async () => {
    while (running) {
      const lines = await sim.command('T 25');
      for (const l of lines) if (l.startsWith('R ')) await page.evaluate((h) => window.__deliver(h), l.slice(2)).catch(() => {});
      await new Promise((r) => setTimeout(r, 25));
    }
  })();
  return () => { running = false; };
}

async function dump(sim) {
  const line = (await sim.command('D')).find((l) => l.startsWith('CRC'));
  const [, crc] = line.match(/^CRC (\w+)/);
  const active = (line.match(/ACTIVE(.*) NAME/)[1].trim().split(' ').filter(Boolean)).map((x) => x.split(':').map(Number));
  return { crc: parseInt(crc, 16), active, name: line.split(' NAME ')[1] || '' };
}
const settle = (page) => page.waitForFunction(() => pendingCells.size === 0 && inFlightCell < 0 && changesInFlight === 0 && pendingSources.size === 0);
const pageCrc = (page) => page.evaluate(() => ModProtocol.patchCrc(state.patch));
const cell = (page, s, t) => page.locator(`td.cell[data-cell="${s * 8 + t}"]`);

async function testToyPiano(browser) {
  const sim = startSim('toy-piano');
  const page = await openEditor(browser, sim);
  await page.click('#connect');
  await page.waitForFunction(() => document.getElementById('status').textContent.includes('toy-piano'));
  const headers = await page.$$eval('#grid thead th .name', (els) => els.map((e) => e.textContent));
  assert.deepStrictEqual(headers, ['Pitch', 'Level', 'Harmonic', 'Attack', 'Decay', '—', '—', '—']);
  assert(await cell(page, 7, 3).evaluate((e) => e.classList.contains('blocked')), 'Poly AT × Attack greyed out');
  assert(await cell(page, 0, 6).evaluate((e) => e.classList.contains('blocked')), 'missing column greyed out');
  assert(!(await cell(page, 7, 1).evaluate((e) => e.classList.contains('blocked'))), 'Poly AT × Level allowed');
  assert(await page.locator('.source[data-src="0"] .fmRow').isHidden(), 'Toy Piano v0.3 has no LFO cross modulation');
  assert.strictEqual(await page.evaluate(() => state.patch.format), 1);

  // Keyboard: LFO 1 → Pitch +30.
  await cell(page, 0, 0).click();
  await page.focus('#grid');
  for (let i = 0; i < 3; ++i) await page.keyboard.press('PageUp');
  await settle(page);
  let d = await dump(sim);
  assert.deepStrictEqual(d.active, [[0, 0, 30]]);
  assert.strictEqual(d.crc, await pageCrc(page));
  assert(await page.textContent('#log').then((t) => t.includes('Device confirmed')));

  // Drag: Modwheel → Level, drag down 100 px (2 px per step) = -50.
  const box = await cell(page, 5, 1).boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  for (let y = 1; y <= 100; y += 4) await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2 + y);
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2 + 100);
  await page.mouse.up();
  await settle(page);
  d = await dump(sim);
  assert.deepStrictEqual(d.active, [[0, 0, 30], [5, 1, -50]]);
  assert.strictEqual(d.crc, await pageCrc(page));

  // Mouse wheel and the cell editor slider.
  await cell(page, 7, 1).hover();
  for (let i = 0; i < 5; ++i) await page.mouse.wheel(0, -100);
  await page.fill('#depthNum', '80');
  await page.dispatchEvent('#depthNum', 'change');
  await settle(page);
  d = await dump(sim);
  assert.deepStrictEqual(d.active, [[0, 0, 30], [5, 1, -50], [7, 1, 80]]);

  // Double-click switches off but keeps the depth.
  await cell(page, 5, 1).dblclick();
  await settle(page);
  d = await dump(sim);
  assert.deepStrictEqual(d.active, [[0, 0, 30], [7, 1, 80]]);
  assert(await cell(page, 5, 1).evaluate((e) => e.classList.contains('off') && e.textContent.includes('-50')));

  // Sources: LFO 1 S&H, synced to MIDI clock 1/8; Random smooth at gate 2.
  const lfo1 = page.locator('.source[data-src="0"]');
  await lfo1.locator('select.shape').selectOption('5');
  await lfo1.locator('.sync button[data-sync="1"]').click();
  assert(await lfo1.locator('.rateRow').isHidden() && await lfo1.locator('.divRow').isVisible());
  await lfo1.locator('select.division').selectOption('6');
  const rnd = page.locator('.source[data-src="2"]');
  await rnd.locator('select.shape').selectOption('1');
  await rnd.locator('.sync button[data-sync="2"]').click();
  await rnd.locator('select.gate').selectOption('1');
  await settle(page);
  assert.strictEqual((await dump(sim)).crc, await pageCrc(page), 'source parameters agree with the device');
  assert.strictEqual(await page.evaluate(() => state.patch.random.division), 2, 'gate sync starts at 1 cycle per pulse');
  assert.strictEqual(await rnd.locator('select.division').inputValue(), '2');
  const lfoState = await page.evaluate(() => state.patch.lfo[0]);
  assert.deepStrictEqual([lfoState.shape, lfoState.sync, lfoState.division], [5, 1, 6]);

  // Limit of 8 active connections.
  const cells = [[0, 1], [0, 2], [1, 0], [1, 1], [2, 2], [2, 3]]; // 2 active already (Modwheel → Level is off)
  for (const [s, t] of cells) { await cell(page, s, t).click(); await page.keyboard.press('PageUp'); }
  await settle(page);
  assert.strictEqual((await dump(sim)).active.length, 8);
  // A cell set to 0 stays switched on; raising it again is the 8th connection, not a 9th.
  await cell(page, 2, 3).click();
  await page.keyboard.press('0');
  await settle(page);
  assert.strictEqual((await dump(sim)).active.length, 7);
  await page.keyboard.press('PageUp');
  await settle(page);
  assert.strictEqual((await dump(sim)).active.length, 8, 'eighth connection allowed after zeroing');
  assert(!(await page.textContent('#log')).includes('At most 8'));
  await cell(page, 3, 3).click();
  await page.keyboard.press('PageUp');
  await settle(page);
  assert((await page.textContent('#log')).includes('At most 8'));
  assert.strictEqual((await dump(sim)).active.length, 8);
  assert.strictEqual((await dump(sim)).crc, await pageCrc(page));

  // Live monitor: LFO and Poly AT rows move.
  const stop = pumpTime(page, sim);
  await page.waitForFunction(() => state.monitor !== null, null, { timeout: 5000 });
  stop();

  // Patch name, save to a file, clear, load the file back.
  await page.fill('#patchName', 'Vibrato Test');
  await page.dispatchEvent('#patchName', 'change');
  await settle(page);
  const [download] = await Promise.all([page.waitForEvent('download'), page.click('#saveBtn')]);
  const file = path.join(os.tmpdir(), 'mod-e2e.modpatch');
  await download.saveAs(file);
  const saved = JSON.parse(fs.readFileSync(file, 'utf8'));
  assert.strictEqual(saved.instrument_key, 'toy-piano');
  assert.strictEqual(saved.depth[0][0], 30);
  const before = await dump(sim);
  await page.click('#clearBtn');
  await settle(page);
  assert.deepStrictEqual((await dump(sim)).active, []);
  await page.setInputFiles('#fileInput', file);
  await page.waitForFunction(() => state.patch.name === 'Vibrato Test' && changesInFlight === 0);
  await settle(page);
  await new Promise((r) => setTimeout(r, 300)); // the last SET_SOURCE/SET_NAME replies
  const after = await dump(sim);
  assert.deepStrictEqual(after.active, before.active);
  assert.strictEqual(after.name.trim(), 'Vibrato Test');
  assert.strictEqual(after.crc, before.crc);

  // A file with a value the device rejects: the editor reloads the device state.
  saved.lfo[1].shape = 9;
  saved.depth[4][4] = 40; saved.enabled[4][4] = false;
  fs.writeFileSync(file, JSON.stringify(saved));
  await page.setInputFiles('#fileInput', file);
  await page.waitForFunction(() => document.getElementById('log').textContent.includes('partly'));
  await settle(page);
  const partial = await dump(sim);
  assert.strictEqual(partial.crc, await pageCrc(page), 'editor shows the partly loaded device state');
  assert.strictEqual(await page.evaluate(() => state.patch.depth[36]), 40, 'the grid did arrive');

  // Disconnecting cancels the request in flight: no late retry, no late reload.
  const late = await page.evaluate(async () => {
    const sent = [];
    const input = { onmidimessage: null };
    const link = new DeviceLink({ send: (f) => sent.push(f) }, input, () => {});
    const reply = link.request(ModProtocol.CMD.getMatrix).then(() => 'answered', (e) => e.message);
    link.close();
    const result = await reply;
    await new Promise((r) => setTimeout(r, 600));
    return { result, sent: sent.length, current: link.current };
  });
  assert.deepStrictEqual(late, { result: 'getrennt', sent: 1, current: null });

  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'editor-toy-piano.png'), fullPage: true });
  await page.close();
  sim.proc.stdin.end();
  console.log('PASS toy-piano: connect, columns, greyed cells, keyboard/drag/wheel/slider, on/off, sources, limit 8, monitor, file save/load, CRC agreement');
}

async function testPhysicalModeling(browser) {
  const sim = startSim('physical-modeling');
  const page = await openEditor(browser, sim);
  await page.click('#connect');
  await page.waitForFunction(() => document.getElementById('status').textContent.includes('physical-modeling'));
  const headers = await page.$$eval('#grid thead th .name', (els) => els.map((e) => e.textContent));
  assert.deepStrictEqual(headers, ['Pitch', 'Structure', 'Brightness', 'Damping', 'Position', 'Excitation', 'Level', 'Dry/Wet']);
  assert(await cell(page, 7, 5).evaluate((e) => e.classList.contains('blocked')), 'Poly AT × Excitation greyed out');
  await cell(page, 7, 2).click();
  assert((await page.textContent('#cellInfo')).includes('per voice'));
  await page.keyboard.press('PageUp');
  await settle(page);
  assert.deepStrictEqual((await dump(sim)).active, [[7, 2, 10]]);
  // Pot positions: the PM build reports column base values; the header shows a tick there.
  const stop = pumpTime(page, sim);
  await page.waitForFunction(() => state.monitor && state.monitor.hasRange, null, { timeout: 5000 });
  stop();
  const based = await page.$$eval('#grid thead th[data-target] .meter', (els) => els.map((e) => e.classList.contains('based')));
  assert.deepStrictEqual(based, Array(8).fill(true));
  const tick = await page.$eval('#grid thead th[data-target="5"] .meter b', (b) => b.style.left);
  assert(Math.abs(parseFloat(tick) - 80) < 0.1, tick); // Excitation default 0.8
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'editor-physical-modeling.png'), fullPage: true });
  await page.close();
  sim.proc.stdin.end();
  console.log('PASS physical-modeling: columns, Poly AT per voice, greyed excitation, CRC agreement');
}

// Correction order 2026-10-24 (review F1-F7) in the browser.
async function testCorrections(browser) {
  const sim = startSim('physical-modeling');
  const page = await openEditor(browser, sim);
  const ports = () => page.evaluate(() => ({ ...window.__ports }));

  // F2: output open, input taken -> the output is released again.
  await page.evaluate(() => { window.__ports.inTaken = true; });
  await page.click('#connect');
  await page.waitForFunction(() => document.getElementById('log').textContent.includes('held by'));
  await page.evaluate(() => closing);
  let pc = await ports();
  assert.deepStrictEqual([pc.outOpen, pc.outClose, pc.inOpen], [1, 1, 0], 'output released after a taken input');
  // F2: IDENTIFY without answer -> both ports closed, no retries left.
  await page.evaluate(() => { window.__ports.inTaken = false; window.__ports.silent = true; });
  await page.click('#connect');
  await page.waitForFunction(() => document.getElementById('status').textContent === 'error', null, { timeout: 5000 });
  await page.evaluate(() => closing);
  pc = await ports();
  assert.deepStrictEqual([pc.outOpen, pc.outClose, pc.inOpen, pc.inClose], [2, 2, 1, 1], 'both released after a handshake timeout');
  assert.strictEqual(await page.evaluate(() => state.link), null);
  // Reconnect works and FM is offered (format 2 device).
  await page.evaluate(() => { window.__ports.silent = false; });
  await page.click('#connect');
  await page.waitForFunction(() => document.getElementById('status').textContent.includes('mod4'));
  assert.strictEqual(await page.evaluate(() => state.patch.format), 2);
  assert(await page.locator('.source[data-src="0"] .fmRow').isVisible());
  assert.strictEqual(await pageCrc(page), (await dump(sim)).crc, 'format 2 CRC agrees');

  // FM and a fast LFO: the range band covers the swing that 20 Hz samples miss.
  await page.locator('.source[data-src="0"] input.fm').fill('35');
  await page.locator('.source[data-src="0"] input.fm').dispatchEvent('input');
  await page.evaluate(() => { setSourceParam(0, ModProtocol.PARAM.rate, ModProtocol.hzToRate(20)); setDepth(1, 50); });
  await settle(page);
  await new Promise((r) => setTimeout(r, 200));
  assert.strictEqual((await dump(sim)).crc, await pageCrc(page), 'fm on the device');
  assert.strictEqual(await page.evaluate(() => state.patch.lfo[0].fm), 35);
  // Aftertouch: Poly on C4 from the keyboard.
  await sim.command('S 903c64');
  await sim.command('S a03c5a');
  const stop = pumpTime(page, sim);
  await page.waitForFunction(() => state.monitor && state.monitor.atKind === 2, null, { timeout: 5000 });
  await page.waitForFunction(() => state.monitor.max[0] - state.monitor.min[0] > 1.5, null, { timeout: 5000 });
  assert((await page.textContent('#grid th.corner')).includes('Poly · C4'));
  assert(await page.locator('#grid tbody tr:nth-child(8) th').evaluate((e) => e.classList.contains('atShown')));
  const band = await page.$eval('#grid tbody th .meter', (m) => parseFloat(m.querySelector('i').style.width));
  assert(band > 75, 'LFO 1 band ' + band);
  // Structure: pot 0.5 with LFO 1 at depth 50 reaches 0 … 1 (just not clipped).
  const goal = await page.$eval('#grid thead th[data-target="1"] .meter', (m) => parseFloat(m.querySelector('i').style.width));
  assert(goal > 95, 'Structure goal band ' + goal);
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'editor-ranges.png'), fullPage: true });
  // Clock state: MIDI clock sync without clock.
  await page.locator('.source[data-src="0"] .sync button[data-sync="1"]').click();
  await settle(page);
  await page.waitForFunction(() => document.querySelector('.source[data-src="0"] .clockRow span').textContent.includes('No MIDI clock'));
  assert(await page.locator('.source[data-src="0"] input.fm').isDisabled());
  await page.locator('.source[data-src="0"] .sync button[data-sync="0"]').click();
  await settle(page);

  // F5: nothing arrives -> no live data after 1.5 s, no replies after 5 s, recovery reloads.
  await page.evaluate(() => { window.__ports.silent = true; });
  await page.waitForFunction(() => document.getElementById('status').textContent.includes('no live data'), null, { timeout: 4000 });
  await page.waitForFunction(() => document.getElementById('status').textContent.includes('no answer'), null, { timeout: 8000 });
  assert((await page.textContent('#log')).includes('No answer from the device for 5 s'));
  await page.evaluate(() => { window.__ports.silent = false; });
  await page.waitForFunction(() => state.health === 'ok', null, { timeout: 5000 });
  stop();
  await settle(page);
  await new Promise((r) => setTimeout(r, 300));
  assert.strictEqual((await dump(sim)).crc, await pageCrc(page), 'state agrees after recovery');

  // F7: +50 -> 0 is sent, 0 while the previous value is in flight, clear keeps sources and name.
  await page.evaluate(() => setDepth(1, 0));
  await settle(page);
  assert.deepStrictEqual((await dump(sim)).active, []);
  await page.evaluate(() => { setDepth(10, 60); setDepth(10, 0); });
  await settle(page);
  await new Promise((r) => setTimeout(r, 200));
  assert.deepStrictEqual((await dump(sim)).active, []);
  assert.strictEqual((await dump(sim)).crc, await pageCrc(page));
  await page.fill('#patchName', 'Keep');
  await page.dispatchEvent('#patchName', 'change');
  await page.evaluate(() => setDepth(2, 40));
  await settle(page);
  await page.click('#clearBtn');
  await settle(page);
  await new Promise((r) => setTimeout(r, 200));
  let d = await dump(sim);
  assert.deepStrictEqual(d.active, []);
  assert.strictEqual(d.name.trim(), 'Keep');
  assert.strictEqual(await page.evaluate(() => state.patch.lfo[0].fm), 35, 'clear keeps the sources');
  assert.strictEqual(d.crc, await pageCrc(page));

  // F1: the review's legacy Reverb file. Cancel changes nothing; safe load leaves Level off.
  const legacy = path.join(__dirname, 'legacy-reverb.modpatch');
  const asked = [];
  await page.exposeFunction('__recordCompat', (notes) => asked.push(notes));
  await page.evaluate(() => { window.__compatChoice = (c) => { window.__recordCompat(c.notes); return 'cancel'; }; });
  const before = (await dump(sim)).crc;
  await page.setInputFiles('#fileInput', legacy);
  await page.waitForFunction(() => document.getElementById('log').textContent.includes('cancelled'));
  assert.strictEqual((await dump(sim)).crc, before, 'cancel leaves the device alone');
  assert(asked[0].some((n) => n.includes('Reverb') && n.includes('Level')), JSON.stringify(asked));
  await page.evaluate(() => { window.__compatChoice = () => 'safe'; });
  await page.setInputFiles('#fileInput', legacy);
  await page.waitForFunction(() => changesInFlight === 0 && state.patch.depth[5 * 8 + 6] === -100);
  await settle(page);
  await new Promise((r) => setTimeout(r, 300));
  d = await dump(sim);
  assert(!d.active.some(([s, t]) => s === 5 && t === 6), 'Level connection stays off');
  assert.strictEqual(d.crc, await pageCrc(page));
  // F7: the default patch (all zeros) replaces everything.
  const empty = { format: 'mod-on-daisy-patch', version: 1, instrument_key: 'physical-modeling', schema_hash: null,
    name: '', columns: [null, null, null, null, null, null, null, null],
    depth: Array.from({ length: 8 }, () => Array(8).fill(0)), enabled: Array.from({ length: 8 }, () => Array(8).fill(false)),
    lfo: [0, 1].map(() => ({ rate: 6000, shape: 0, sync: 0, gate: 0, division: 4, retrigger: 0, fm: 0 })),
    random: { rate: 8000, mode: 0, sync: 0, gate: 0, division: 4 } };
  const emptyFile = path.join(os.tmpdir(), 'mod-e2e-empty.modpatch');
  fs.writeFileSync(emptyFile, JSON.stringify(empty));
  await page.setInputFiles('#fileInput', emptyFile);
  await page.waitForFunction(() => changesInFlight === 0 && state.patch.name === '' && state.patch.lfo[0].fm === 0);
  await settle(page);
  await new Promise((r) => setTimeout(r, 300));
  d = await dump(sim);
  assert.strictEqual(d.crc, 0xc422b385, 'default patch (format 2) on the device');

  // F2/F5: disconnect closes both ports and clears the meters.
  await page.click('#connect');
  await page.evaluate(() => closing);
  pc = await ports();
  assert.strictEqual(pc.outOpen, pc.outClose);
  assert.strictEqual(pc.inOpen, pc.inClose);
  assert.strictEqual(await page.evaluate(() => state.monitor), null);
  const widths = await page.$$eval('#grid .meter i', (els) => els.map((e) => parseFloat(e.style.width || '0')));
  assert(widths.every((w) => w === 0), 'meters cleared: ' + widths);
  await page.close();
  sim.proc.stdin.end();
  console.log('PASS corrections: port release (taken input, handshake timeout, disconnect), FM, ranges, aftertouch, clock state, health, zero/clear/default patch, compatibility dialog');
}

async function testOffline(browser) {
  // No device: the editor still edits and saves files.
  const page = await browser.newPage();
  page.on('pageerror', (e) => { throw e; });
  await page.addInitScript(() => { navigator.requestMIDIAccess = async () => ({ inputs: new Map(), outputs: new Map() }); });
  await page.goto(PAGE);
  await cell(page, 1, 4).click();
  await page.keyboard.press('PageDown');
  assert.strictEqual(await page.evaluate(() => state.patch.depth[12]), -10);
  await page.click('#connect');
  assert((await page.textContent('#log')).includes('an input and an output'));
  await page.click('#demoBtn'); // offline preview with the Toy Piano columns
  const headers = await page.$$eval('#grid thead th .name', (els) => els.map((e) => e.textContent));
  assert.deepStrictEqual(headers.slice(0, 5), ['Pitch', 'Level', 'Harmonic', 'Attack', 'Decay']);
  assert.strictEqual(await page.evaluate(() => ModProtocol.activeCount(state.patch)), 6);
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'editor-demo.png'), fullPage: true });
  await page.close();
  console.log('PASS offline: editing without a device, demo preview');
}

(async () => {
  assert(SIM && fs.existsSync(SIM), 'usage: node tests/e2e.cjs <mod_sim> [screenshot dir]');
  const browser = await chromium.launch();
  try {
    await testToyPiano(browser);
    await testPhysicalModeling(browser);
    await testCorrections(browser);
    await testOffline(browser);
  } finally {
    await browser.close();
  }
  console.log('PASS: browser editor end-to-end against the real controller');
})().catch((e) => { console.error(e); process.exit(1); });
