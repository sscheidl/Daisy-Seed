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
    const input = { id: 'in', name: 'Daisy Seed External (Sim)', state: 'connected', onmidimessage: null, open: async () => {} };
    window.__deliver = (hex) => input.onmidimessage && input.onmidimessage({ data: hexToBytes(hex) });
    const output = {
      id: 'out', name: 'Daisy Seed External (Sim)', state: 'connected', open: async () => {},
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

  // Keyboard: LFO 1 → Pitch +30.
  await cell(page, 0, 0).click();
  await page.focus('#grid');
  for (let i = 0; i < 3; ++i) await page.keyboard.press('PageUp');
  await settle(page);
  let d = await dump(sim);
  assert.deepStrictEqual(d.active, [[0, 0, 30]]);
  assert.strictEqual(d.crc, await pageCrc(page));
  assert(await page.textContent('#log').then((t) => t.includes('Gerät bestätigt')));

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
  await cell(page, 3, 3).click();
  await page.keyboard.press('PageUp');
  await settle(page);
  assert((await page.textContent('#log')).includes('Höchstens 8'));
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
  assert.deepStrictEqual(headers, ['Pitch', 'Structure', 'Brightness', 'Damping', 'Position', 'Excitation', 'Reverb', 'Dry/Wet']);
  assert(await cell(page, 7, 5).evaluate((e) => e.classList.contains('blocked')), 'Poly AT × Excitation greyed out');
  await cell(page, 7, 2).click();
  assert((await page.textContent('#cellInfo')).includes('pro Stimme'));
  await page.keyboard.press('PageUp');
  await settle(page);
  assert.deepStrictEqual((await dump(sim)).active, [[7, 2, 10]]);
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'editor-physical-modeling.png'), fullPage: true });
  await page.close();
  sim.proc.stdin.end();
  console.log('PASS physical-modeling: columns, Poly AT per voice, greyed excitation, CRC agreement');
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
  assert((await page.textContent('#log')).includes('Ein- und Ausgang'));
  await page.close();
  console.log('PASS offline: editing without a device');
}

(async () => {
  assert(SIM && fs.existsSync(SIM), 'usage: node tests/e2e.cjs <mod_sim> [screenshot dir]');
  const browser = await chromium.launch();
  try {
    await testToyPiano(browser);
    await testPhysicalModeling(browser);
    await testOffline(browser);
  } finally {
    await browser.close();
  }
  console.log('PASS: browser editor end-to-end against the real controller');
})().catch((e) => { console.error(e); process.exit(1); });
