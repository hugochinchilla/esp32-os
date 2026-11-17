#!/usr/bin/env node
// Opens the board's own viewer page (core/src/net/viewer_page.cpp) in a real
// browser against the simulated board and uses its buttons for the keys a
// page never gets (Ctrl+Alt+Del, Alt+Tab, Alt+F4, the logo key): what each
// sends, that the board does it, and that the keyboard stays with the
// picture. Needs Node with Playwright and its Chromium; it is not part of
// ctest (tools/test_vnc_sim.py checks the same keys without a browser).
//
//   node tools/test_viewer_page.js build/host/sim [folder for pictures]
const { chromium } = require('playwright');
const { spawn } = require('child_process');
const fs = require('fs');
const net = require('net');
const os = require('os');
const path = require('path');

const shots = process.argv[3];

function check(what, ok) {
  console.log((ok ? 'ok    ' : 'FAILED ') + what);
  if (!ok) process.exitCode = 1;
}
const freePort = () => new Promise((done) => {
  const s = net.createServer().listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => done(p)); });
});

(async () => {
  const vnc = await freePort(), web = await freePort();
  const sim = spawn(process.argv[2], [fs.mkdtempSync(path.join(os.tmpdir(), 'chinchilla-c-')), String(vnc), String(web)],
                    { stdio: ['inherit', 'pipe', 'inherit'] });
  const browser = await chromium.launch({ args: ['--host-resolver-rules=MAP chinchilla.local 127.0.0.1', '--no-proxy-server'] });
  try {
    // The start-up screens first: the page is served once the desktop is up.
    for (let tries = 0; tries < 100; tries++) {
      const up = await new Promise((r) => {
        const c = net.connect(web, '127.0.0.1', () => { c.destroy(); r(true); }).on('error', () => r(false));
      });
      if (up) break;
      await new Promise((r) => setTimeout(r, 200));
    }
    const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
    const problems = [], sent = [];
    page.on('pageerror', (e) => problems.push(e.message));
    page.on('websocket', (ws) => ws.on('framesent', (f) => {
      const b = Buffer.from(f.payload);
      if (b[0] === 4) sent.push((b[1] ? '+' : '-') + b.readUInt32BE(4).toString(16));
    }));
    await page.goto('http://127.0.0.1:' + web + '/');
    // Until the desktop is up the board ends the connection: click, as the page says.
    for (let tries = 0; tries < 20 && (await page.textContent('#s')) !== ''; tries++) {
      await page.waitForTimeout(500);
      if ((await page.textContent('#s')).startsWith('Not connected')) await page.click('#s');
    }
    await page.waitForTimeout(1500);
    const shot = async (name) => shots && page.screenshot({ path: path.join(shots, name) });
    const press = async (label, want) => {
      sent.length = 0;
      await page.click('#k button:text-is("' + label + '")');
      await page.waitForTimeout(700);
      check(label + ' sends ' + want, sent.join(' ') === want);
    };
    await shot('viewer-buttons.png');

    await press('Ctrl+Alt+Del', '+ffe3 +ffe9 +ffff -ffff -ffe9 -ffe3');
    await shot('viewer-ctrl-alt-del.png');
    await press('Alt+Tab', '+ffe9 +ff09 -ff09 -ffe9');
    await shot('viewer-alt-tab.png');
    await press('Alt+F4', '+ffe9 +ffc1 -ffc1 -ffe9');
    await shot('viewer-alt-f4.png');
    await press('Start', '+ffeb -ffeb');
    await shot('viewer-start.png');
    // The keyboard is still the picture's: Escape closes the Start menu.
    sent.length = 0;
    await page.keyboard.press('Escape');
    await page.waitForTimeout(500);
    check('a key after a button still goes to the board', sent.join(' ') === '+ff1b -ff1b');
    check('no button keeps the keyboard', await page.evaluate(() => document.activeElement.tagName !== 'BUTTON'));
    await shot('viewer-after.png');

    // A narrow, tall window: the buttons go along the top.
    await page.setViewportSize({ width: 660, height: 900 });
    await page.waitForTimeout(300);
    check('in a tall window the buttons are in a row',
          await page.evaluate(() => getComputedStyle(document.getElementById('k')).flexDirection === 'row'));
    await shot('viewer-tall.png');
    // No room at all: over the picture, faint.
    await page.setViewportSize({ width: 640, height: 480 });
    await page.waitForTimeout(300);
    check('with no room the buttons are faint over the picture',
          await page.evaluate(() => document.getElementById('k').classList.contains('over')));
    await shot('viewer-exact.png');
    check('no errors on the page', problems.length === 0);
    // The same page by the board's name, as the owner opens it: no
    // certificate, so the browser does not offer it the keyboard lock.
    const named = await browser.newPage();
    await named.goto('http://chinchilla.local:' + web + '/');
    check('the keyboard lock is not offered to a page from http://chinchilla.local',
          await named.evaluate(() => !isSecureContext && !navigator.keyboard));
    check('and it would be to a page with a certificate (this browser has it)',
          await page.evaluate(() => isSecureContext && !!navigator.keyboard));
    if (problems.length) console.log(problems.join('\n'));
  } finally {
    await browser.close();
    sim.kill();
  }
})();
