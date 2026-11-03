// Renders manual.html to Streichwurst-Manual.pdf (Playwright with Chromium).
//   python build_manual.py && node render.js
// PLAYWRIGHT_MODULE may point to a Playwright module or wrapper (e.g. one using Edge).
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const path = require('path');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto('file://' + path.resolve(__dirname, 'manual.html'));
  await page.pdf({
    path: path.resolve(__dirname, '../../../../releases/streichwurst-0.1.0-test1/Streichwurst-Manual.pdf'),
    preferCSSPageSize: true,
    printBackground: true,
    displayHeaderFooter: true,
    headerTemplate: '<span></span>',
    footerTemplate: '<div style="font-size:7pt;color:#777;width:100%;text-align:center">Streichwurst 0.1.0 · page <span class="pageNumber"></span> / <span class="totalPages"></span></div>',
  });
  await browser.close();
})();
