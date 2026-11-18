// The relay of Chinchilla's web page: a Cloudflare Worker.
//
// A web page cannot open a connection to a site, and is not allowed to
// fetch a plain-HTTP page of another site. The page of Chinchilla therefore
// asks this Worker, which fetches the address and hands the answer back
// with the header that lets the page read it:
//
//     GET https://<this worker>/?url=http%3A%2F%2Ffrogfind.com%2F&agent=...
//
// It is not a proxy for anything else. It answers only pages that came from
// ALLOWED_ORIGINS, only GET, only http:// addresses on port 80 or 8080,
// never an address inside a private network, and at most MAX_BYTES of
// answer. A site that has moved is not followed here: the status and the
// new address go back (X-Relay-Status, X-Relay-Location) and Chinchilla's
// browser follows it itself, as it does on the board.
//
// How to put it on the web: web/relay/README.md.

// The pages that may use the relay. Add http://localhost:8080 while trying
// the page on your own computer.
const ALLOWED_ORIGINS = [
  'https://www.hugochinchilla.net',
  'http://localhost:8080',
];
const MAX_BYTES = 512 * 1024;
const TIMEOUT_MS = 20000;
const PORTS = ['', '80', '8080'];

function refuse(status, text, origin) {
  return new Response(text + '\n', {
    status: status,
    headers: Object.assign({ 'Content-Type': 'text/plain; charset=utf-8' }, cors(origin)),
  });
}

function cors(origin) {
  if (!origin) {
    return {};
  }
  return {
    'Access-Control-Allow-Origin': origin,
    'Access-Control-Expose-Headers': 'X-Relay-Status, X-Relay-Location',
    'Vary': 'Origin',
  };
}

// Addresses that are somebody's own network, not the web.
function is_private(host) {
  if (host === 'localhost' || host.endsWith('.local') || host.endsWith('.internal') || host.includes(':')) {
    return true;
  }
  const parts = host.split('.');
  if (parts.length === 4 && parts.every(function (p) { return /^\d+$/.test(p); })) {
    const a = Number(parts[0]), b = Number(parts[1]);
    return a === 10 || a === 127 || a === 0 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) ||
           (a === 169 && b === 254) || (a === 100 && b >= 64 && b <= 127) || a >= 224;
  }
  return !host.includes('.'); // a bare name
}

async function relay(request) {
  const origin = request.headers.get('Origin');
  if (!origin || !ALLOWED_ORIGINS.includes(origin)) {
    return refuse(403, 'This relay serves the page of Chinchilla only.', null);
  }
  if (request.method !== 'GET') {
    return refuse(405, 'The relay only fetches pages (GET).', origin);
  }
  const asked = new URL(request.url).searchParams;
  let target;
  try {
    target = new URL(asked.get('url') || '');
  } catch (e) {
    return refuse(400, 'Ask with ?url=http://...', origin);
  }
  if (target.protocol !== 'http:' || !PORTS.includes(target.port) || target.username || is_private(target.hostname)) {
    return refuse(400, 'The relay fetches plain http:// pages of the public web only.', origin);
  }

  const stop = new AbortController();
  const timer = setTimeout(function () { stop.abort(); }, TIMEOUT_MS);
  let answer;
  try {
    answer = await fetch(target.toString(), {
      method: 'GET',
      redirect: 'manual',
      signal: stop.signal,
      headers: {
        'User-Agent': (asked.get('agent') || 'Mozilla/4.0 (compatible; Chinchilla)').slice(0, 200),
        'Accept': 'text/html, text/plain, */*',
      },
    });
  } catch (e) {
    clearTimeout(timer);
    return refuse(502, 'The site did not answer the relay.', origin);
  }

  // No more than MAX_BYTES of it.
  const chunks = [];
  let size = 0;
  if (answer.body) {
    const reader = answer.body.getReader();
    try {
      while (size < MAX_BYTES) {
        const { done, value } = await reader.read();
        if (done) {
          break;
        }
        const room = MAX_BYTES - size;
        chunks.push(value.length > room ? value.subarray(0, room) : value);
        size += Math.min(value.length, room);
      }
    } catch (e) {
      // What came is what there is.
    }
    reader.cancel().catch(function () {});
  }
  clearTimeout(timer);
  const body = new Uint8Array(size);
  let at = 0;
  for (const chunk of chunks) {
    body.set(chunk, at);
    at += chunk.length;
  }

  const headers = Object.assign({
    'Content-Type': answer.headers.get('Content-Type') || 'text/html',
    'X-Relay-Status': String(answer.status),
    'Cache-Control': 'no-store',
  }, cors(origin));
  const moved = answer.headers.get('Location');
  if (moved) {
    headers['X-Relay-Location'] = moved;
  }
  // Always 200 to the page: what the site said is in X-Relay-Status, and a
  // 301 of ours would send the page's own fetch() somewhere else.
  return new Response(body, { status: 200, headers: headers });
}

export default {
  fetch: relay,
};
