// The relay tried without Cloudflare or the web: `node web/relay/test.mjs`.
// The site it fetches from is faked here; what is tested is what the relay
// lets through and what it refuses. The deploy workflow runs it first.
import relay from './worker.js';

let asked = null;
globalThis.fetch = async (url, options) => {
  asked = { url, options };
  if (url.includes('moved')) {
    return new Response('', { status: 301, headers: { Location: 'http://example.org/new' } });
  }
  if (url.includes('big')) {
    return new Response(new Uint8Array(700 * 1024), { status: 200 });
  }
  return new Response('<p>hi</p>', { status: 404, headers: { 'Content-Type': 'text/html; charset=x' } });
};

const PAGE = 'https://www.hugochinchilla.net';
function ask(query, origin = PAGE, method = 'GET') {
  return relay.fetch(new Request('https://relay.example/?' + query, { method, headers: origin ? { Origin: origin } : {} }));
}

let failures = 0;
async function expect(name, answer, status, site, moved, bytes) {
  const size = (await answer.arrayBuffer()).byteLength;
  const got = [answer.status, answer.headers.get('X-Relay-Status'), answer.headers.get('X-Relay-Location'), size];
  const want = [status, site, moved, bytes];
  if (JSON.stringify(got) !== JSON.stringify(want)) {
    console.log('FAILED ' + name + ': got ' + JSON.stringify(got) + ', wanted ' + JSON.stringify(want));
    failures++;
  }
}

await expect('a page', await ask('url=' + encodeURIComponent('http://frogfind.com/?q=a b') + '&agent=Viz'), 200, '404', null, 9);
if (asked.url !== 'http://frogfind.com/?q=a%20b' || asked.options.headers['User-Agent'] !== 'Viz' ||
    asked.options.redirect !== 'manual') {
  console.log('FAILED: what the site was asked: ' + JSON.stringify(asked));
  failures++;
}
await expect('a moved site is not followed', await ask('url=http://example.org/moved'), 200, '301', 'http://example.org/new', 0);
await expect('cut at the limit', await ask('url=http://example.org/big'), 200, '200', null, 512 * 1024);
await expect('no origin', await ask('url=http://example.org/', null), 403, null, null, 47);
await expect('another page', await ask('url=http://example.org/', 'https://evil.example'), 403, null, null, 47);
await expect('https', await ask('url=https://example.org/'), 400, null, null, 62);
await expect('a private address', await ask('url=http://192.168.1.1/'), 400, null, null, 62);
await expect('another port', await ask('url=http://example.org:22/'), 400, null, null, 62);
await expect('POST', await ask('url=http://example.org/', PAGE, 'POST'), 405, null, null, 36);
await expect('nothing asked', await ask(''), 400, null, null, 25);

console.log(failures === 0 ? 'ok' : failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
