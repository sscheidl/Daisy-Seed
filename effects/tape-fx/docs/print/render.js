const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  for (const [src, out, landscape] of [['anleitung.html', 'Tape-FX-Bedienungsanleitung.pdf', false], ['cheatsheet.html', 'Tape-FX-Cheatsheet.pdf', true]]) {
    await page.goto('file://' + path.resolve(src));
    await page.pdf({ path: out, preferCSSPageSize: true, printBackground: true, landscape });
  }
  await page.goto('file://' + path.resolve('cheatsheet.html'));
  await browser.close();
})();
