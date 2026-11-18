#include "core/www/url.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace www {

namespace {

// The length of "scheme:" at the start, 0 if there is none. A drive letter
// ("C:") is not a scheme.
size_t scheme_length(const char *url)
{
    size_t i = 0;
    while (isalpha(static_cast<unsigned char>(url[i])) || (i > 0 && (isdigit(static_cast<unsigned char>(url[i])) ||
                                                                      url[i] == '+' || url[i] == '-' || url[i] == '.'))) {
        i++;
    }
    return i >= 2 && url[i] == ':' ? i + 1 : 0;
}

bool is_drive_path(const char *text)
{
    return isalpha(static_cast<unsigned char>(text[0])) && text[1] == ':' &&
           (text[2] == '\\' || text[2] == '/' || text[2] == 0);
}

class Out {
public:
    Out(char *out, size_t capacity) : out_(out), capacity_(capacity)
    {
        if (capacity_ > 0) {
            out_[0] = 0;
        }
    }
    void add(const char *text, size_t length)
    {
        if (length_ + length + 1 > capacity_) {
            ok_ = false;
            return;
        }
        memcpy(out_ + length_, text, length);
        length_ += length;
        out_[length_] = 0;
    }
    void add(const char *text) { add(text, strlen(text)); }
    bool ok() const { return ok_; }
    size_t length() const { return length_; }
    void cut(size_t length)
    {
        length_ = length;
        out_[length_] = 0;
    }

private:
    char *out_;
    size_t capacity_;
    size_t length_ = 0;
    bool ok_ = true;
};

// Takes "." and ".." out of the path that starts at out[from] (a '/').
void tidy_path(char *out, size_t from)
{
    char *path = out + from;
    // The query is not part of the path.
    char *end = path + strcspn(path, "?");
    char tail[kMaxUrl];
    snprintf(tail, sizeof(tail), "%s", end);
    *end = 0;

    char *write = path;
    const char *read = path;
    while (*read != 0) {
        // read is at a '/'.
        const char *next = strchr(read + 1, '/');
        const size_t length = next != nullptr ? static_cast<size_t>(next - read - 1) : strlen(read + 1);
        const char *segment = read + 1;
        if (length == 1 && segment[0] == '.') {
            if (next == nullptr) {
                *write++ = '/';
            }
        } else if (length == 2 && segment[0] == '.' && segment[1] == '.') {
            while (write > path && *--write != '/') {
            }
            if (next == nullptr) {
                *write++ = '/';
            }
        } else {
            memmove(write, read, length + 1);
            write += length + 1;
        }
        read = next != nullptr ? next : read + 1 + length;
    }
    if (write == path) {
        *write++ = '/';
    }
    *write = 0;
    memmove(write, tail, strlen(tail) + 1);
}

} // namespace

Scheme scheme_of(const char *url)
{
    const size_t length = scheme_length(url);
    if (length == 0) {
        return Scheme::None;
    }
    if (strncasecmp(url, "http:", 5) == 0) {
        return Scheme::Http;
    }
    if (strncasecmp(url, "https:", 6) == 0) {
        return Scheme::Https;
    }
    if (strncasecmp(url, "file:", 5) == 0) {
        return Scheme::File;
    }
    if (strncasecmp(url, "about:", 6) == 0) {
        return Scheme::About;
    }
    return Scheme::Other;
}

bool from_typed(const char *typed, char *out, size_t capacity)
{
    while (*typed == ' ') {
        typed++;
    }
    size_t length = strlen(typed);
    while (length > 0 && typed[length - 1] == ' ') {
        length--;
    }
    Out o(out, capacity);
    if (is_drive_path(typed)) {
        o.add("file:///");
        for (size_t i = 0; i < length; i++) {
            const char c = typed[i] == '\\' ? '/' : typed[i];
            o.add(&c, 1);
        }
        return o.ok();
    }
    char text[kMaxUrl];
    if (length >= sizeof(text)) {
        return false;
    }
    memcpy(text, typed, length);
    text[length] = 0;
    if (scheme_length(text) != 0) {
        // "localhost:8080" has a colon and no scheme.
        const char *after = strchr(text, ':') + 1;
        if (!isdigit(static_cast<unsigned char>(*after))) {
            o.add(text);
            if (scheme_of(text) == Scheme::Http && strchr(text + 7, '/') == nullptr && strncmp(text + 5, "//", 2) == 0) {
                o.add("/");
            }
            return o.ok();
        }
    }
    if (text[0] == 0) {
        return false;
    }
    o.add("http://");
    o.add(text);
    if (strchr(text, '/') == nullptr) {
        o.add("/");
    }
    return o.ok();
}

bool resolve(const char *base, const char *reference, char *out, size_t capacity)
{
    while (*reference == ' ' || *reference == '\n' || *reference == '\t' || *reference == '\r') {
        reference++;
    }
    size_t length = strcspn(reference, "#");
    while (length > 0 && (reference[length - 1] == ' ' || reference[length - 1] == '\n' ||
                          reference[length - 1] == '\r' || reference[length - 1] == '\t')) {
        length--;
    }
    char ref[kMaxUrl];
    if (length >= sizeof(ref)) {
        return false;
    }
    // No spaces inside an address: old pages have them, servers do not.
    size_t n = 0;
    for (size_t i = 0; i < length; i++) {
        if (reference[i] == ' ') {
            if (n + 3 >= sizeof(ref)) {
                return false;
            }
            memcpy(ref + n, "%20", 3);
            n += 3;
        } else if (reference[i] != '\n' && reference[i] != '\r' && reference[i] != '\t') {
            if (n + 1 >= sizeof(ref)) {
                return false;
            }
            ref[n++] = reference[i];
        }
    }
    ref[n] = 0;

    Out o(out, capacity);
    if (scheme_length(ref) != 0) {
        o.add(ref);
        if (scheme_of(ref) == Scheme::Http && strncmp(ref + 5, "//", 2) == 0 && strchr(ref + 7, '/') == nullptr) {
            // "http://host?x" keeps its query after the added slash.
            const size_t host_end = 7 + strcspn(ref + 7, "?");
            o.cut(host_end);
            o.add("/");
            o.add(ref + host_end);
        }
        return o.ok();
    }
    // The base without what follows a '#'.
    const size_t base_length = strcspn(base, "#");
    const size_t base_scheme = scheme_length(base);
    if (base_scheme == 0) {
        return false;
    }
    if (scheme_of(base) == Scheme::About) {
        return false; // nothing is beside an about: page
    }
    if (ref[0] == '/' && ref[1] == '/') {
        o.add(base, base_scheme);
        o.add(ref);
        return o.ok();
    }
    // Where the path starts in the base: after "scheme://host".
    size_t root = base_scheme;
    if (strncmp(base + root, "//", 2) == 0) {
        root += 2;
        root += strcspn(base + root, "/?");
    }
    if (root > base_length) {
        root = base_length;
    }
    if (ref[0] == 0) {
        o.add(base, base_length);
        return o.ok();
    }
    if (ref[0] == '/') {
        o.add(base, root);
        o.add(ref);
    } else if (ref[0] == '?') {
        const size_t path_end = root + strcspn(base + root, "?#");
        o.add(base, path_end < base_length ? path_end : base_length);
        if (path_end == root) {
            o.add("/");
        }
        o.add(ref);
        return o.ok();
    } else {
        // Beside the base's last name.
        size_t path_end = root + strcspn(base + root, "?#");
        if (path_end > base_length) {
            path_end = base_length;
        }
        size_t folder = path_end;
        while (folder > root && base[folder - 1] != '/') {
            folder--;
        }
        o.add(base, folder);
        if (folder == root) {
            o.add("/");
        }
        o.add(ref);
    }
    if (o.ok()) {
        tidy_path(out, root);
    }
    return o.ok();
}

bool split_http(const char *url, char *host, size_t host_capacity, int &port, const char *&path)
{
    if (scheme_of(url) != Scheme::Http || strncmp(url + 5, "//", 2) != 0) {
        return false;
    }
    const char *start = url + 7;
    const size_t end = strcspn(start, "/?");
    // "user@host" is not asked for.
    const char *at = static_cast<const char *>(memchr(start, '@', end));
    const char *name = at != nullptr ? at + 1 : start;
    size_t length = end - static_cast<size_t>(name - start);
    port = 80;
    const char *colon = static_cast<const char *>(memchr(name, ':', length));
    if (colon != nullptr) {
        port = 0;
        for (const char *p = colon + 1; p < name + length; p++) {
            if (!isdigit(static_cast<unsigned char>(*p)) || port > 6553) {
                return false;
            }
            port = port * 10 + (*p - '0');
        }
        if (port == 0 || port > 65535) {
            return false;
        }
        length = static_cast<size_t>(colon - name);
    }
    if (length == 0 || length + 1 > host_capacity) {
        return false;
    }
    memcpy(host, name, length);
    host[length] = 0;
    path = start + end;
    return true;
}

bool file_path(const char *url, char *out, size_t capacity)
{
    if (scheme_of(url) != Scheme::File) {
        return false;
    }
    const char *p = url + 5;
    while (*p == '/') {
        p++;
    }
    if (!is_drive_path(p)) {
        return false;
    }
    Out o(out, capacity);
    for (; *p != 0 && *p != '?' && *p != '#'; p++) {
        char c = *p == '/' ? '\\' : *p;
        if (c == '%' && isxdigit(static_cast<unsigned char>(p[1])) && isxdigit(static_cast<unsigned char>(p[2]))) {
            const char hex[3] = {p[1], p[2], 0};
            c = static_cast<char>(strtol(hex, nullptr, 16));
            p += 2;
        }
        o.add(&c, 1);
    }
    return o.ok();
}

size_t append_escaped(char *out, size_t length, size_t capacity, const char *text)
{
    static const char kHex[] = "0123456789ABCDEF";
    for (; *text != 0; text++) {
        const unsigned char c = static_cast<unsigned char>(*text);
        const bool plain = isalnum(c) || c == '-' || c == '_' || c == '.' || c == '*';
        const size_t need = plain || c == ' ' ? 1 : 3;
        if (length + need + 1 > capacity) {
            return capacity;
        }
        if (plain) {
            out[length++] = static_cast<char>(c);
        } else if (c == ' ') {
            out[length++] = '+';
        } else {
            out[length++] = '%';
            out[length++] = kHex[c >> 4];
            out[length++] = kHex[c & 15];
        }
        out[length] = 0;
    }
    return length;
}

} // namespace www
