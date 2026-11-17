// The page the board serves at http://<its address>/ : the remote screen in
// a browser. It is a small VNC viewer written for this server only: 8-bit
// pixels with the board's colour map, rectangles as ZRLE tiles of up to
// 64x64 (or plain pixels where the browser cannot unpack zlib), and the
// pointer's shape as the browser's cursor. The VNC bytes travel in a
// WebSocket to the same address (core/net/web.hpp).
//
// The buttons send the keys a page never sees, because the viewing computer
// keeps them for itself: Ctrl+Alt+Del, Alt+Tab, Alt+F4 and the logo key
// (docs/journal/2026-11-17-viewer-system-keys.md).
#include "core/net/web.hpp"

namespace net {

namespace {

const char kPage[] = R"page(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Chinchilla</title>
<style>
html,body{margin:0;height:100%;background:#202020;overflow:hidden}
body{display:flex;align-items:center;justify-content:center}
canvas{image-rendering:pixelated;outline:none;touch-action:none;background:#000}
#s{position:fixed;left:0;right:0;bottom:8px;text-align:center;font:13px sans-serif;color:#bbb}
#k{position:fixed;top:0;right:0;display:flex;gap:4px;padding:4px;justify-content:center}
#k button{font:12px sans-serif;color:#ddd;background:#3a3a3a;border:1px solid #666;border-radius:3px;
  padding:4px 8px;cursor:pointer;white-space:nowrap}
#k button:hover{background:#505050}
#k button:active{background:#707070}
#k.over{opacity:.4}
#k.over:hover{opacity:1}
</style></head><body>
<canvas id="c" width="640" height="480" tabindex="0"></canvas><div id="s">Connecting...</div>
<div id="k" title="Keys this computer keeps for itself">
<button data-k="0xFFE3 0xFFE9 0xFFFF">Ctrl+Alt+Del</button>
<button data-k="0xFFE9 0xFF09">Alt+Tab</button>
<button data-k="0xFFE9 0xFFC1">Alt+F4</button>
<button data-k="0xFFEB">Start</button>
</div>
<script>
'use strict';
const cv = document.getElementById('c'), ctx = cv.getContext('2d'), note = document.getElementById('s');
const bar = document.getElementById('k');
let ws = null, live = false, W = 640, H = 480, pal = new Uint32Array(256), idx = new Uint8Array(W * H);
let mask = 0;
const held = new Map();

// Bytes as they arrive, read with "await q.take(n)".
function Queue() {
  let parts = [], have = 0, wake = null, dead = false;
  this.push = b => { parts.push(b); have += b.length; if (wake) wake(); };
  this.end = () => { dead = true; if (wake) wake(); };
  this.take = async n => {
    while (have < n) {
      if (dead) throw new Error('closed');
      await new Promise(r => wake = r);
      wake = null;
    }
    const out = new Uint8Array(n);
    let at = 0;
    while (at < n) {
      const p = parts[0], use = Math.min(p.length, n - at);
      out.set(p.subarray(0, use), at);
      at += use;
      if (use === p.length) parts.shift(); else parts[0] = p.subarray(use);
    }
    have -= n;
    return out;
  };
}

function fit() {
  let k = Math.min(innerWidth / W, innerHeight / H);
  if (k >= 1) k = Math.floor(k);
  cv.style.width = W * k + 'px';
  cv.style.height = H * k + 'px';
  // The buttons go where the picture leaves more room: down the right edge
  // or along the top. Where they must lie on the picture they are faint
  // until the pointer is on them.
  const beside = (innerWidth - W * k) / 2, above = (innerHeight - H * k) / 2, side = beside >= above;
  bar.style.flexDirection = side ? 'column' : 'row';
  bar.style.left = side ? 'auto' : '0';
  bar.classList.toggle('over', side ? beside < bar.offsetWidth : above < bar.offsetHeight);
}
addEventListener('resize', fit);

function send(bytes) { if (live) ws.send(new Uint8Array(bytes)); }
function ask(incremental) { send([3, incremental, 0, 0, 0, 0, W >> 8, W & 255, H >> 8, H & 255]); }

// Draws w x h colour numbers at x, y and keeps them for a new colour map.
function paint(x, y, w, h, d) {
  const img = ctx.createImageData(w, h), px = new Uint32Array(img.data.buffer);
  for (let i = 0; i < w * h; i++) px[i] = pal[d[i]];
  ctx.putImageData(img, x, y);
  for (let r = 0; r < h; r++) idx.set(d.subarray(r * w, r * w + w), (y + r) * W + x);
}

function pointerShape(x, y, w, h, d, m) {
  if (!w || !h) { cv.style.cursor = 'default'; return; }
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  const g = c.getContext('2d'), img = g.createImageData(w, h), px = new Uint32Array(img.data.buffer);
  const row = (w + 7) >> 3;
  for (let j = 0; j < h; j++) for (let i = 0; i < w; i++)
    px[j * w + i] = m[j * row + (i >> 3)] & (128 >> (i & 7)) ? pal[d[j * w + i]] : 0;
  g.putImageData(img, 0, 0);
  cv.style.cursor = 'url(' + c.toDataURL() + ') ' + x + ' ' + y + ', default';
}

async function run() {
  const q = new Queue();
  let zq = null, zw = null;
  if (window.DecompressionStream) {
    const ds = new DecompressionStream('deflate');
    zq = new Queue();
    zw = ds.writable.getWriter();
    const zr = ds.readable.getReader();
    (async () => {
      try { for (;;) { const r = await zr.read(); if (r.done) break; zq.push(r.value); } } catch (e) {}
      zq.end();
    })();
  }
  ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/rfb', ['binary']);
  ws.binaryType = 'arraybuffer';
  ws.onmessage = e => q.push(new Uint8Array(e.data));
  ws.onclose = () => { live = false; q.end(); if (zq) zq.end(); };
  await new Promise((ok, no) => { ws.onopen = ok; ws.onerror = no; });
  live = true;
  const u16 = (b, i) => b[i] << 8 | b[i + 1];
  const u32 = (b, i) => (b[i] << 24 | b[i + 1] << 16 | b[i + 2] << 8 | b[i + 3]) >>> 0;

  await q.take(12);
  send([...'RFB 003.008\n'].map(c => c.charCodeAt(0)));
  const kinds = (await q.take(1))[0];
  if (!kinds) throw new Error('refused');
  await q.take(kinds);
  send([1]);                                   // no password
  if (u32(await q.take(4), 0)) throw new Error('refused');
  send([1]);                                   // others may stay (the board keeps one viewer anyway)
  const init = await q.take(24);
  W = u16(init, 0); H = u16(init, 2);
  await q.take(u32(init, 20));                 // the name
  cv.width = W; cv.height = H; idx = new Uint8Array(W * H);
  fit();
  // 8 bits a pixel through a colour map: the board's own format.
  send([0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
  const enc = zq ? [16, 0, -239] : [0, -239];  // ZRLE, plain, pointer shape
  const m = [2, 0, 0, enc.length];
  for (const e of enc) m.push(e >> 24 & 255, e >> 16 & 255, e >> 8 & 255, e & 255);
  send(m);
  ask(0);
  note.textContent = '';
  cv.focus();

  for (;;) {
    const type = (await q.take(1))[0];
    if (type === 0) {
      const count = u16(await q.take(3), 1);
      for (let n = 0; n < count; n++) {
        const r = await q.take(12), x = u16(r, 0), y = u16(r, 2), w = u16(r, 4), h = u16(r, 6);
        const kind = u32(r, 8) | 0;
        if (kind === 0) {
          paint(x, y, w, h, await q.take(w * h));
        } else if (kind === 16) {
          zw.write(await q.take(u32(await q.take(4), 0))).catch(() => {});
          const sub = (await zq.take(1))[0];
          let d;
          if (sub === 0) {
            d = await zq.take(w * h);
          } else if (sub === 1) {
            d = new Uint8Array(w * h).fill((await zq.take(1))[0]);
          } else if (sub <= 16) {
            const p = await zq.take(sub), bits = sub === 2 ? 1 : sub <= 4 ? 2 : 4;
            const row = (w * bits + 7) >> 3, packed = await zq.take(row * h);
            d = new Uint8Array(w * h);
            for (let j = 0; j < h; j++) for (let i = 0; i < w; i++)
              d[j * w + i] = p[packed[j * row + (i * bits >> 3)] >> (8 - bits - (i * bits & 7)) & ((1 << bits) - 1)];
          } else {
            throw new Error('tile ' + sub);
          }
          paint(x, y, w, h, d);
        } else if (kind === -239) {
          const d = await q.take(w * h);
          pointerShape(x, y, w, h, d, await q.take(((w + 7) >> 3) * h));
        } else {
          throw new Error('encoding ' + kind);
        }
      }
      ask(1);
    } else if (type === 1) {
      const head = await q.take(5), first = u16(head, 1), count = u16(head, 3), c = await q.take(count * 6);
      for (let i = 0; i < count; i++)
        pal[first + i] = (0xFF000000 | c[i * 6 + 4] << 16 | c[i * 6 + 2] << 8 | c[i * 6]) >>> 0;
      paint(0, 0, W, H, idx.slice());
    } else if (type === 2) {
      // a bell: nothing to do
    } else if (type === 3) {
      await q.take(u32(await q.take(7), 3));   // text for the clipboard: not used
    } else {
      throw new Error('message ' + type);
    }
  }
}

function start() {
  note.textContent = 'Connecting...';
  run().catch(() => {
    live = false;
    held.clear();
    try { ws.close(); } catch (e) {}
    note.textContent = 'Not connected. Click to connect again.';
  });
}

/* Mouse and touch. */
function place(e) {
  const r = cv.getBoundingClientRect();
  const x = Math.max(0, Math.min(W - 1, Math.floor((e.clientX - r.left) * W / r.width)));
  const y = Math.max(0, Math.min(H - 1, Math.floor((e.clientY - r.top) * H / r.height)));
  return [x >> 8, x & 255, y >> 8, y & 255];
}
function pointer(e) {
  mask = (e.buttons & 1) | (e.buttons & 4 ? 2 : 0) | (e.buttons & 2 ? 4 : 0);
  send([5, mask].concat(place(e)));
  e.preventDefault();
}
cv.addEventListener('pointerdown', e => { cv.focus(); cv.setPointerCapture(e.pointerId); pointer(e); });
cv.addEventListener('pointermove', pointer);
cv.addEventListener('pointerup', pointer);
cv.addEventListener('pointercancel', pointer);
cv.addEventListener('contextmenu', e => e.preventDefault());
cv.addEventListener('wheel', e => {
  const p = place(e), bit = e.deltaY < 0 ? 8 : 16;
  send([5, mask | bit].concat(p));
  send([5, mask].concat(p));
  e.preventDefault();
}, {passive: false});
addEventListener('click', () => { if (!live && (!ws || ws.readyState === 3)) start(); });

/* Keyboard. */
const named = {Backspace: 0xFF08, Tab: 0xFF09, Enter: 0xFF0D, Escape: 0xFF1B, Insert: 0xFF63, Delete: 0xFFFF,
  Home: 0xFF50, End: 0xFF57, PageUp: 0xFF55, PageDown: 0xFF56, ArrowLeft: 0xFF51, ArrowUp: 0xFF52,
  ArrowRight: 0xFF53, ArrowDown: 0xFF54, Shift: 0xFFE1, Control: 0xFFE3, Alt: 0xFFE9, CapsLock: 0xFFE5};
function keysym(e) {
  if (e.code === 'ShiftRight') return 0xFFE2;
  if (e.code === 'ControlRight') return 0xFFE4;
  if (e.code === 'AltRight') return 0xFFEA;
  if (named[e.key]) return named[e.key];
  const f = /^F(\d+)$/.exec(e.key);
  if (f && f[1] >= 1 && f[1] <= 12) return 0xFFBD + +f[1];
  const cp = [...e.key].length === 1 ? e.key.codePointAt(0) : 0;
  return cp < 0x20 ? 0 : cp < 0x100 ? cp : 0x01000000 + cp;
}
function key(sym, down) { send([4, down, 0, 0, sym >>> 24, sym >> 16 & 255, sym >> 8 & 255, sym & 255]); }
addEventListener('keydown', e => {
  if (!live || e.metaKey) return;
  const sym = keysym(e);
  if (sym) { held.set(e.code, sym); key(sym, 1); }
  e.preventDefault();
});
addEventListener('keyup', e => {
  const sym = held.get(e.code);
  if (sym) { held.delete(e.code); key(sym, 0); }
  e.preventDefault();
});
addEventListener('blur', () => { for (const sym of held.values()) key(sym, 0); held.clear(); });
// The buttons: their keys go down in order and come up in reverse. The
// keyboard stays with the picture.
for (const b of bar.children) {
  b.tabIndex = -1;
  b.addEventListener('mousedown', e => e.preventDefault());
  b.addEventListener('click', () => {
    const syms = b.dataset.k.split(' ').map(Number);
    for (const sym of syms) key(sym, 1);
    for (const sym of syms.reverse()) key(sym, 0);
  });
}

fit();
start();
</script></body></html>
)page";

// The same screen with noVNC (https://novnc.com, MPL 2.0), which is not kept
// on the board: the browser fetches it from the jsDelivr CDN, from noVNC's
// GitHub repository at the exact commit of release 1.7.0. The page carries
// the hash of each of noVNC's files (novnc_files.inc, made by
// tools/novnc_integrity.py), and the browser refuses a file that does not
// match, so the CDN cannot serve the board's viewers anything else. The
// viewing computer needs the internet for it.
const char kNoVnc[] = R"page(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Chinchilla (noVNC)</title>
<style>
html,body{margin:0;height:100%;background:#202020;overflow:hidden}
#screen{width:100%;height:100%}
#s{position:fixed;left:0;right:0;bottom:8px;text-align:center;font:13px sans-serif;color:#bbb}
</style></head><body>
<div id="screen"></div><div id="s">Loading noVNC...</div>
<script type="importmap">{"integrity":{
)page"
#include "novnc_files.inc"
R"page(}}</script>
<script type="module">
const note = document.getElementById('s');
let RFB;
try {
  RFB = (await import('https://cdn.jsdelivr.net/gh/novnc/noVNC@63107bd06d9e1f6136ff21aeda8cd62cbf0d433e/core/rfb.js')).default;
} catch (e) {
  note.innerHTML = 'noVNC could not be loaded: no internet, or its files are not the expected ones. <a href="/" style="color:#9cf">Use the built-in viewer</a>.';
  throw e;
}
function connect() {
  note.textContent = 'Connecting...';
  const rfb = new RFB(document.getElementById('screen'),
                      (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/rfb');
  rfb.scaleViewport = true;
  rfb.background = '#202020';
  rfb.addEventListener('connect', () => { note.textContent = ''; rfb.focus(); });
  rfb.addEventListener('disconnect', () => {
    note.textContent = 'Not connected. Click to connect again.';
    addEventListener('click', connect, {once: true});
  });
  window.rfb = rfb;
}
connect();
</script></body></html>
)page";

} // namespace

const char *novnc_page() { return kNoVnc; }
size_t novnc_page_size() { return sizeof(kNoVnc) - 1; }

const char *viewer_page() { return kPage; }
size_t viewer_page_size() { return sizeof(kPage) - 1; }

} // namespace net
