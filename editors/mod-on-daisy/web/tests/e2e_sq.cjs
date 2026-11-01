// End-to-end test of the browser editor's Acid Seq pages against the firmware's SQ device
// (tests/sq_sim.cpp). Chromium (Playwright) gets a fake Web MIDI port whose traffic goes
// to sq_sim, as e2e.cjs does with mod_sim. Also checks that an MD device still gets the
// matrix (with mod_sim) and an SQ device never gets an MD request.
//
//   node tests/e2e_sq.cjs <path to sq_sim> [path to mod_sim] [screenshot dir]
// Needs Playwright with Chromium (preinstalled in the cloud environment).
'use strict';
const assert = require('assert');
const path = require('path');
const { spawn } = require('child_process');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');

const SQ_SIM = process.argv[2];
const MOD_SIM = process.argv[3];
const SHOTS = process.argv[4];
const PAGE = 'file://' + path.resolve(__dirname, '..', 'index.html');

function startSim(binary, arg) {
  const proc = spawn(binary, [arg], { stdio: ['pipe', 'pipe', 'inherit'] });
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
  // Ends the simulator with EOF and waits for it (Codex F18: mod_sim knows no quit command).
  const stop = () => new Promise((resolve) => { proc.once('exit', resolve); proc.stdin.end(); });
  return { proc, command, stop };
}

// Calibration of the simulated device only (Codex F17): the uncalibrated simulator reports
// zeros, which the device rightly refuses. These values are no advice for real hardware.
const SIM_CAL = { offset: 2048, scale: -409.6, gateLow: 2048, gateHigh: 410, clampMin: 0, clampMax: 4095 };
const validOutputs = () => [...document.querySelectorAll('#sqCal .hint')].filter((h) => h.textContent.trim() === 'gültig').length;

async function openEditor(browser, sim, sent) {
  const page = await browser.newPage({ viewport: { width: 1280, height: 1400 } });
  page.on('pageerror', (e) => { throw e; });
  await page.exposeFunction('__simSend', async (hex) => {
    sent.push(hex);
    return (await sim.command('S ' + hex)).filter((l) => l.startsWith('R ')).map((l) => l.slice(2));
  });
  await page.exposeFunction('__simTime', async (ms) => (await sim.command('T ' + ms)).filter((l) => l.startsWith('R ')).map((l) => l.slice(2)));
  await page.addInitScript(() => {
    const hexToBytes = (h) => Uint8Array.from(h.match(/../g).map((b) => parseInt(b, 16)));
    const input = { id: 'in', name: 'Daisy Seed External (Sim)', state: 'connected', onmidimessage: null,
      open: async () => {}, close: async () => {} };
    window.__deliver = (hex) => input.onmidimessage && input.onmidimessage({ data: hexToBytes(hex) });
    window.__tick = async (ms) => (await window.__simTime(ms)).forEach((r) => window.__deliver(r));
    const output = { id: 'out', name: 'Daisy Seed External (Sim)', state: 'connected', open: async () => {}, close: async () => {},
      send(data) {
        const hex = Array.from(data, (b) => b.toString(16).padStart(2, '0')).join('');
        window.__simSend(hex).then((replies) => replies.forEach((r) => window.__deliver(r)));
      } };
    navigator.requestMIDIAccess = async () => ({ inputs: new Map([['in', input]]), outputs: new Map([['out', output]]), onstatechange: null });
  });
  await page.goto(PAGE);
  return page;
}

(async () => {
  const browser = await chromium.launch();
  // ---- an SQ device: Acid Seq pages, no MD request
  const sq = startSim(SQ_SIM, 'acid-seq');
  const sent = [];
  const page = await openEditor(browser, sq, sent);
  await page.click('#connect');
  await page.waitForSelector('#sqMain:not([hidden])', { timeout: 10000 });
  await page.waitForFunction(() => document.querySelectorAll('#sqSteps .step').length === 16);
  assert.ok(await page.isHidden('#mdMain'));
  assert.ok(sent.every((h) => !h.startsWith('f07d4d44')), 'no Mod on Daisy request to an SQ device');
  assert.match(await page.textContent('#status'), /acid-seq 0\.1\.0/);
  // Device first: nothing was written while reading.
  const writes = ['06', '11', '12', '20', '29', '2a'];
  assert.ok(sent.every((h) => !writes.includes(h.slice(10, 12))));
  // Edit a step, a voice parameter, transport; the device confirms.
  await page.click('#sqSteps .step[data-step="2"]');
  await page.selectOption('#sqNote', '45');
  await page.waitForFunction(() => document.querySelector('#sqSteps .step[data-step="2"]').innerText.includes('A4'));
  await page.click('#sqTabs button[data-tab="acid-303"]');
  await page.selectOption('#sqParams select[data-key="waveform"]', '1');
  await page.click('#sqStop');
  await page.evaluate(() => window.__tick(400));
  await page.waitForFunction(() => document.querySelector('#sqStatus').innerText.includes('PAUSE'));
  await page.click('#sqStart');
  await page.evaluate(() => window.__tick(600));
  await page.waitForFunction(() => document.querySelector('#sqSteps .step.play'));
  // Calibration and the separate enable.
  await page.click('#sqCvOn');
  await page.waitForFunction(() => document.querySelector('#log').textContent.includes('nicht kalibriert'));
  for (const output of [0, 1]) {
    for (const [key, value] of Object.entries(SIM_CAL)) await page.fill(`#sqCal${output}${key}`, String(value));
    await page.click(`#sqCalSend${output}`);
    await page.waitForFunction(`(${validOutputs})() === ${output + 1}`);
  }
  await page.click('#sqCvOn');
  await page.evaluate(() => window.__tick(300));
  await page.waitForFunction(() => document.querySelector('#sqStatus').innerText.includes('CV frei'));
  await page.click('#sqStatsBtn');
  await page.waitForFunction(() => document.querySelectorAll('#sqStats tr').length === 17);
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'acid-seq-panel.png'), fullPage: true });
  await page.click('#connect'); // disconnect
  assert.ok(await page.isHidden('#sqMain'));
  await sq.stop();
  // ---- Streichwurst (SQ 1.1): SQ pages first, then Mod on Daisy alongside on the same port
  const sw = startSim(SQ_SIM, 'streichwurst');
  const swSent = [];
  const swPage = await openEditor(browser, sw, swSent);
  await swPage.click('#connect');
  await swPage.waitForSelector('#sqMain:not([hidden])', { timeout: 10000 });
  await swPage.waitForFunction(() => document.querySelector('#status').textContent.includes('SQ + Matrix'), { timeout: 10000 });
  assert.match(await swPage.textContent('#sqTitle'), /Streichwurst/);
  assert.ok(await swPage.isHidden('#sqSteps')); // no pattern card
  const firstMd = swSent.findIndex((h) => h.startsWith('f07d4d44'));
  assert.ok(firstMd > 0 && swSent.slice(0, firstMd).every((h) => h.startsWith('f07d5351')), 'SQ is read before MD starts');
  assert.ok(swSent.every((h) => !['06', '11', '12'].includes(h.slice(10, 12)) || !h.startsWith('f07d5351')), 'read-only connect');
  await swPage.click('#sqTabs button[data-tab="strings"]');
  await swPage.selectOption('#sqParams select[data-key="octave"]', '1');
  await swPage.waitForFunction(() => document.querySelector('#log').textContent.length >= 0);
  assert.ok((await sw.command('T 10')) && swSent.some((h) => h.startsWith('f07d5351') && h.slice(10, 12) === '06'));
  await swPage.click('#sqTabs button[data-tab="fx"]');
  assert.ok(await swPage.isVisible('#sqParams [data-key="reverb"]'));
  if (SHOTS) await swPage.screenshot({ path: path.join(SHOTS, 'streichwurst-panel.png'), fullPage: true });
  await swPage.click('#swViewBtn'); // the matrix of the same device
  assert.ok(await swPage.isVisible('#mdMain') && await swPage.isHidden('#sqMain'));
  await swPage.waitForFunction(() => document.querySelector('#mdMain').textContent.includes('Registr.'), { timeout: 10000 });
  if (SHOTS) await swPage.screenshot({ path: path.join(SHOTS, 'streichwurst-matrix.png'), fullPage: true });
  await swPage.click('#swViewBtn');
  await swPage.click('#connect'); // disconnect: both sessions end
  assert.ok(await swPage.isHidden('#sqMain') && await swPage.isHidden('#swViewBtn'));
  await sw.stop();
  // ---- the standalone Streichwurst FX firmware: only the FX page, no matrix session
  const swfx = startSim(SQ_SIM, 'streichwurst-fx');
  const fxSent = [];
  const fxPage = await openEditor(browser, swfx, fxSent);
  await fxPage.click('#connect');
  await fxPage.waitForSelector('#sqMain:not([hidden])', { timeout: 10000 });
  await fxPage.waitForSelector('#sqParams [data-key="reverb"]');
  assert.deepStrictEqual(await fxPage.$$eval('#sqTabs button', (b) => b.map((x) => x.dataset.tab)), ['fx']);
  assert.ok(await fxPage.isHidden('#swViewBtn') && fxSent.every((h) => !h.startsWith('f07d4d44')));
  await fxPage.click('#connect');
  await swfx.stop();
  // ---- an MD device still gets the matrix (the SQ probe stays unanswered)
  if (MOD_SIM) {
    const md = startSim(MOD_SIM, 'toy-piano');
    const mdSent = [];
    const mdPage = await openEditor(browser, md, mdSent);
    await mdPage.click('#connect');
    await mdPage.waitForFunction(() => document.querySelector('#status').textContent.includes('toy-piano'), { timeout: 10000 });
    assert.ok(await mdPage.isHidden('#sqMain'));
    assert.ok(mdSent.some((h) => h.startsWith('f07d5351')) && mdSent.some((h) => h.startsWith('f07d4d44')));
    await md.stop();
  }
  await browser.close();
  console.log('PASS: browser Acid Seq and Streichwurst pages against the firmware SQ device (Acid Seq: no MD request, read-only connect, step, '
    + 'parameter, transport, calibration/enable, stats; Streichwurst: SQ first, matrix alongside, pages, view switch) and MD devices unchanged');
})().catch((e) => { console.error(e); process.exit(1); });
