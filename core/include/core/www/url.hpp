// Web addresses for the browser (core/www): telling the kinds apart, making
// a link's address whole against the page it is on, and what a form sends.
// Text in, text out; nothing here touches the network.
#pragma once

#include <cstddef>
#include <cstdint>

namespace www {

// The longest address the browser keeps. FrogFind's links carry a whole
// address inside them, so this is generous.
constexpr size_t kMaxUrl = 600;

enum class Scheme : uint8_t {
    None,  // no scheme: a relative address
    Http,
    Https, // known, and not spoken: there is no TLS
    File,  // file:///C:/docs/page.htm
    About, // about:home and the browser's own pages
    Other, // mailto:, ftp:, javascript:...
};

Scheme scheme_of(const char *url);

// What was typed in the address bar, as an address: "frogfind.com" becomes
// "http://frogfind.com/", "C:\docs\a.htm" becomes "file:///C:/docs/a.htm".
// False if it does not fit.
bool from_typed(const char *typed, char *out, size_t capacity);

// A link's address made whole against the address of its page: another
// site, "/from/the/root", "beside.htm", "../up.htm", "?query". What follows
// a '#' is dropped (places inside a page are not followed). False if it
// does not fit.
bool resolve(const char *base, const char *reference, char *out, size_t capacity);

// The parts of an http address. `path` points into `url`, at the '/' (or
// at its end: then the path is "/"). False if it is not one, or the host
// does not fit.
bool split_http(const char *url, char *host, size_t host_capacity, int &port, const char *&path);

// The path of a file address, in the system's form ("C:\docs\a.htm").
bool file_path(const char *url, char *out, size_t capacity);

// Appends `text` as a form sends it: letters and digits as they are, a
// space as '+', the rest as %XX. Returns the new length, or `capacity` if
// it did not fit (the text is cut and still ends in a zero).
size_t append_escaped(char *out, size_t length, size_t capacity, const char *text);

} // namespace www
