// The way out of the web page (the browser version, web/): a net::Dialer
// that is not a connection at all.
//
// A page cannot open a TCP connection, and may not fetch a plain-HTTP page
// of another site either. So the request the program writes into its
// "connection" is read here, sent through a relay (web/relay/worker.js, a
// Cloudflare Worker the owner runs) with the browser's own fetch(), and the
// answer is handed back as if it had come down the wire. To the program it
// is a cable; underneath, only web requests pass (the owner's choice,
// 2026-11-17: docs/journal/2026-11-17-web-browser.md).
//
// The relay's address is built in (CHINCHILLA_RELAY, web/CMakeLists.txt);
// "?relay=https://..." after the page's address names another. With none,
// connect() says there is no network.
#pragma once

#include <cstring>
#include <string>

#include <emscripten.h>

#include "core/net/dialer.hpp"

#ifndef CHINCHILLA_RELAY
#define CHINCHILLA_RELAY ""
#endif

namespace host {

// The relay's address into the page, once. Returns 1 if there is one.
EM_JS(int, web_relay_setup, (const char *built_in), {
    if (Module.chinchillaRelay === undefined) {
        const asked = new URLSearchParams(location.search).get('relay');
        Module.chinchillaRelay = asked || UTF8ToString(built_in);
        Module.chinchillaLines = {};
    }
    return Module.chinchillaRelay ? 1 : 0;
});

// Asks the relay for an address. The answer is kept in the page, whole,
// with the lines an HTTP answer starts with put in front of it.
EM_JS(void, web_relay_fetch, (int line, const char *address, const char *agent), {
    // A line end, spelt so that the C preprocessor leaves it alone.
    const nl = String.fromCharCode(13, 10);
    const state = { bytes: null, at: 0, stop: new AbortController() };
    Module.chinchillaLines[line] = state;
    const answer = function (head, body) {
        const top = new TextEncoder().encode(head);
        const all = new Uint8Array(top.length + body.length);
        all.set(top, 0);
        all.set(body, top.length);
        state.bytes = all;
    };
    const url = Module.chinchillaRelay + '?url=' + encodeURIComponent(UTF8ToString(address)) +
                '&agent=' + encodeURIComponent(UTF8ToString(agent));
    fetch(url, { signal: state.stop.signal, cache: 'no-store' })
        .then(function (r) {
            return r.arrayBuffer().then(function (buffer) {
                // The relay answers 200 itself and says in its own headers
                // what the site answered; anything else is the relay's word.
                const status = r.headers.get('X-Relay-Status') || String(r.status);
                const moved = r.headers.get('X-Relay-Location');
                let head = 'HTTP/1.0 ' + status + ' Relayed' + nl;
                head += 'Content-Type: ' + (r.headers.get('Content-Type') || 'text/html') + nl;
                if (moved) {
                    head += 'Location: ' + moved + nl;
                }
                answer(head + nl, new Uint8Array(buffer));
            });
        })
        .catch(function () {
            answer('HTTP/1.0 502 No relay' + nl + 'Content-Type: text/html' + nl + nl,
                   new TextEncoder().encode('<h2>The relay did not answer</h2><p>This page reaches the web through a ' +
                                            'relay, and the relay could not be reached. The site itself may be fine.</p>'));
        });
});

// As a Stream's read(): bytes, 0 while the answer has not come, -1 at its end.
EM_JS(int, web_relay_read, (int line, uint8_t *out, int capacity), {
    const state = Module.chinchillaLines[line];
    if (!state) {
        return -1;
    }
    if (state.bytes === null) {
        return 0;
    }
    const left = state.bytes.length - state.at;
    if (left <= 0) {
        return -1;
    }
    const n = Math.min(left, capacity);
    HEAPU8.set(state.bytes.subarray(state.at, state.at + n), out);
    state.at += n;
    return n;
});

EM_JS(void, web_relay_drop, (int line), {
    const state = Module.chinchillaLines[line];
    if (state) {
        state.stop.abort();
        delete Module.chinchillaLines[line];
    }
});

class WebDialer : public net::Dialer {
public:
    static constexpr int kLines = 4;

    // Does not wait: the "connection" is open at once, and the waiting
    // happens when the request has been written.
    int connect(const char *host, int port) override
    {
        if (web_relay_setup(CHINCHILLA_RELAY) == 0) {
            return -static_cast<int>(net::DialFailure::NoNetwork);
        }
        for (int i = 0; i < kLines; i++) {
            if (!lines_[i].used) {
                lines_[i].used = true;
                lines_[i].asked = false;
                lines_[i].request.clear();
                lines_[i].origin = std::string("http://") + host;
                if (port != 80) {
                    lines_[i].origin += ":" + std::to_string(port);
                }
                return i;
            }
        }
        return -static_cast<int>(net::DialFailure::NoLines);
    }

    int read(int line, uint8_t *out, size_t capacity) override
    {
        if (!valid(line)) {
            return -1;
        }
        return lines_[line].asked ? web_relay_read(line, out, static_cast<int>(capacity)) : 0;
    }

    // Takes the request. Once its blank line is in, the address and the
    // browser's name are read out of it and the relay is asked.
    int write(int line, const uint8_t *data, size_t size) override
    {
        if (!valid(line)) {
            return -1;
        }
        Line &l = lines_[line];
        if (l.asked) {
            return static_cast<int>(size); // nothing more is expected
        }
        l.request.append(reinterpret_cast<const char *>(data), size);
        if (l.request.find("\r\n\r\n") == std::string::npos) {
            return static_cast<int>(size);
        }
        const size_t path = l.request.find(' ');
        const size_t path_end = l.request.find(' ', path + 1);
        if (l.request.compare(0, 4, "GET ") != 0 || path_end == std::string::npos) {
            return -1; // only pages are asked for through the relay
        }
        std::string agent;
        const size_t at = l.request.find("\r\nUser-Agent: ");
        if (at != std::string::npos) {
            agent = l.request.substr(at + 14, l.request.find("\r\n", at + 2) - at - 14);
        }
        const std::string address = l.origin + l.request.substr(path + 1, path_end - path - 1);
        web_relay_fetch(line, address.c_str(), agent.c_str());
        l.asked = true;
        return static_cast<int>(size);
    }

    void hang_up(int line) override
    {
        if (valid(line)) {
            web_relay_drop(line);
            lines_[line].used = false;
        }
    }

private:
    struct Line {
        bool used = false;
        bool asked = false;
        std::string origin, request;
    };
    bool valid(int line) const { return line >= 0 && line < kLines && lines_[line].used; }

    Line lines_[kLines];
};

} // namespace host
