// Renders the Toy Piano on Cumulonimbus quick guide (English, plus the German translation) to PDF.
//   node render_cumulonimbus.js
// PLAYWRIGHT_MODULE may point to a Playwright module or wrapper (e.g. one using Edge).
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const path = require('path');
const DOCS = [
  ['cumulonimbus_quick_guide.html', 'Toy-Piano-Cumulonimbus-Quick-Guide.pdf', 'page'],
  ['cumulonimbus_quick_guide_de.html', 'Toy-Piano-Cumulonimbus-Kurzanleitung.pdf', 'Seite'],
];
(async () => {
  const browser = await chromium.launch();
  for (const [source, target, word] of DOCS) {
    const page = await browser.newPage();
    await page.goto('file://' + path.resolve(__dirname, source));
    await page.pdf({
      path: path.resolve(__dirname, target),
      preferCSSPageSize: true,
      printBackground: true,
      displayHeaderFooter: true,
      headerTemplate: '<span></span>',
      footerTemplate: '<div style="font-size:7pt;color:#777;width:100%;text-align:center">Toy Piano on Cumulonimbus 0.1.2 · '
        + word + ' <span class="pageNumber"></span> / <span class="totalPages"></span></div>',
    });
    await page.close();
    console.log('wrote', target);
  }
  await browser.close();
})();
