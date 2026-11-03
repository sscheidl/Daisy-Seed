// SPDX-License-Identifier: MIT
// Focused rendering/interaction check; live transport is covered by sq_client.test.cjs.
'use strict';
const assert = require('assert');
const path = require('path');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');

(async () => {
  const browser = await chromium.launch(process.env.CHROMIUM_EXECUTABLE ? { executablePath: process.env.CHROMIUM_EXECUTABLE } : {});
  try {
    const page = await browser.newPage({ viewport: { width: 1200, height: 900 } });
    await page.setContent('<div id="ambient"></div>');
    for (const name of ['sq_protocol.js', 'sq_contract.js', 'sq_panel.js'])
      await page.addScriptTag({ path: path.resolve(__dirname, '..', name) });
    await page.evaluate(() => {
      window.ambientClient = new SqPanel.OfflineClient('ambient-reverb');
      SqPanel.mount(document.querySelector('#ambient')).attach(window.ambientClient);
    });
    const slider = page.locator('#sqParams input[data-key="reverb_lfo_rate"]');
    await slider.waitFor();
    assert(await slider.isEnabled());
    assert((await page.locator('#sqParams').textContent()).includes('Reverb LFO rate'));
    assert((await page.locator('#sqParams').textContent()).includes('0.18 Hz'));
    for (const [position, hz] of [[1000, 1.5], [0, .03]]) {
      await slider.evaluate((el, p) => { el.value = p; el.dispatchEvent(new Event('change')); }, position);
      await page.waitForFunction((expected) => Math.abs(window.ambientClient.component('ambient-reverb').parameters[0].value - expected) < .00001, hz);
    }
    if(process.argv[2]) await page.screenshot({ path: process.argv[2], fullPage: true });
    console.log('PASS Ambient panel: visible English rate control, 0.18 Hz readback, interactive 0.03–1.5 Hz endpoints');
  } finally { await browser.close(); }
})().catch((e) => { console.error(e); process.exit(1); });
