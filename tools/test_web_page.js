#!/usr/bin/env node
// Opens the browser version (web/) in a real browser and uses it as a person
// would: start-up screens, a double click, typing, File > Save, loading the
// page again. Needs Node with Playwright and its Chromium; it is not part of
// ctest.
//
//   python3 -m http.server -d build/web/site 8080 &
//   node tools/test_web_page.js http://localhost:8080/ [folder for pictures]
const { chromium } = require('playwright');
const fs = require('fs');
const os = require('os');
const path = require('path');

const url = process.argv[2] || 'http://localhost:8080/';
const shots = process.argv[3];
const FILE = '/chinchilla-c/C/Documents/Desktop/welcome.txt';

function check(what, ok) {
  console.log((ok ? 'ok    ' : 'FAILED ') + what);
  if (!ok) process.exitCode = 1;
}

(async () => {
  // A profile on disk, so the browser's storage is the real thing.
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'chinchilla-web-'));
  const context = await chromium.launchPersistentContext(profile, { viewport: { width: 1280, height: 960 } });
  const page = context.pages()[0] || (await context.newPage());
  const problems = [];
  page.on('pageerror', (e) => problems.push(e.message));
  page.on('console', (m) => m.type() === 'error' && problems.push(m.text()));
  const shot = async (name) => shots && page.screenshot({ path: path.join(shots, name) });
  const read = () => page.evaluate((f) => FS.readFile(f, { encoding: 'utf8' }), FILE);
  // The start-up screens, skipped with a key as on the board.
  const start = async () => {
    await page.goto(url);
    await page.waitForFunction(() => typeof FS !== 'undefined' && !document.getElementById('note'));
    // The page starts switched off (a browser plays no sound on a page
    // nobody has clicked): the power button starts the machine.
    await page.waitForSelector('#off.ready');
    await shot('web-off.png');
    await page.click('#power');
    await page.waitForTimeout(800);
    await shot('web-start-up.png');
    await page.keyboard.press('Space');
    await page.waitForTimeout(1200);
  };

  await start();
  check('the power button is gone and the sound output runs', await page.evaluate(
    () => !document.getElementById('off') && Module.SDL2.audioContext.state === 'running'));
  check('a new C: has the sample files', (await read()).startsWith('Welcome to Chinchilla'));
  await shot('web-desktop.png');

  // The viewport is 2x, so screen pixel (x, y) is at (2x, 2y) on the page.
  await page.mouse.dblclick(37 * 2, 305 * 2); // welcome.txt on the desktop
  await page.waitForTimeout(600);
  await page.keyboard.down('Shift');
  await page.keyboard.press('KeyH');
  await page.keyboard.up('Shift');
  await page.keyboard.type('ola desde el navegador', { delay: 20 });
  await page.keyboard.down('Shift');
  await page.keyboard.press('Digit1');
  await page.keyboard.up('Shift');
  await page.keyboard.press('Space');
  await page.keyboard.press('Alt+f'); // File
  await page.waitForTimeout(300);
  await page.keyboard.press('s'); // Save
  await page.waitForTimeout(600);
  await shot('web-editor.png');
  check('typed text is saved in C:', (await read()).startsWith('Hola desde el navegador! Welcome'));

  await page.waitForTimeout(1500); // C: is written back to the browser a moment later
  await start(); // the page loaded again: a new visit
  check('C: is still there on the next visit', (await read()).startsWith('Hola desde el navegador! Welcome'));

  // A page that is not a multiple of 640x480: the picture is 933x700 in the
  // middle, and the mouse still lands where it points.
  await page.setViewportSize({ width: 1000, height: 700 });
  await page.waitForTimeout(500);
  const size = await page.evaluate(() => [Module.canvas.width, Module.canvas.height]);
  check('the canvas follows the page (1000x700)', size[0] === 1000 && size[1] === 700);
  await page.mouse.click(60, 680); // the Start button
  await page.waitForTimeout(400);
  await shot('web-start-menu-1000x700.png');

  // Start > Shut Down > Restart loads the page again.
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('Enter');
  await page.waitForTimeout(500);
  const loaded = page.waitForEvent('load', { timeout: 5000 }).then(() => true, () => false);
  await page.keyboard.press('Alt+r');
  check('Restart loads the page again', await loaded);

  check('no errors in the page: ' + JSON.stringify(problems), problems.length === 0);
  await context.close();
  fs.rmSync(profile, { recursive: true, force: true });
})();
