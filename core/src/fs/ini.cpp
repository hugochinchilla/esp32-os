#include "core/fs/ini.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fs {

namespace {

bool space(char c) { return c == ' ' || c == '\t'; }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// a[0..n) equals b, ignoring case.
bool same(const char *a, size_t n, const char *b)
{
    for (size_t i = 0; i < n; i++) {
        if (b[i] == 0 || lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return b[n] == 0;
}

} // namespace

void Ini::set_text(const char *text)
{
    size_ = strnlen(text, kMaxText);
    memcpy(text_, text, size_);
    text_[size_] = 0;
    // A NUL inside a damaged file ends the text there.
    size_ = strlen(text_);
}

bool Ini::load(const Storage &storage, const char *path)
{
    char tmp[kMaxPath];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    size_t got = 0;
    if (storage.read_file(path, text_, kMaxText, got) != ports::FsError::Ok &&
        storage.read_file(tmp, text_, kMaxText, got) != ports::FsError::Ok) {
        text_[0] = 0;
        size_ = 0;
        return false;
    }
    text_[got] = 0;
    size_ = strlen(text_);
    return true;
}

ports::FsError Ini::save(const Storage &storage, const char *path) const
{
    char tmp[kMaxPath];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    ports::FsError e = storage.write_file(tmp, text_, size_);
    if (e != ports::FsError::Ok) {
        return e;
    }
    // ponytail: no rename-over on every drive, so remove then rename; a cut
    // in between leaves the .tmp file, which load() reads.
    e = storage.remove(path);
    if (e != ports::FsError::Ok && e != ports::FsError::NotFound) {
        return e;
    }
    return storage.rename(tmp, path);
}

bool Ini::find(const char *section, const char *key, size_t &value_start, size_t &value_end, size_t &section_end,
               bool &section_found) const
{
    // Keys before the first [section] belong to the top (section "").
    const bool top = section == nullptr || section[0] == 0;
    bool in = top;
    section_found = top;
    section_end = size_;
    for (size_t at = 0; at < size_;) {
        size_t end = at;
        while (end < size_ && text_[end] != '\n') {
            end++;
        }
        size_t s = at, e = end;
        while (s < e && space(text_[s])) {
            s++;
        }
        while (e > s && (space(text_[e - 1]) || text_[e - 1] == '\r')) {
            e--;
        }
        if (s < e && text_[s] == '[') {
            if (in) {
                section_end = at; // the wanted section ends where the next begins
                return false;
            }
            size_t close = s + 1;
            while (close < e && text_[close] != ']') {
                close++;
            }
            in = !top && close < e && same(text_ + s + 1, close - s - 1, section);
            section_found = section_found || in;
        } else if (in && s < e && text_[s] != ';' && text_[s] != '#') {
            size_t eq = s;
            while (eq < e && text_[eq] != '=') {
                eq++;
            }
            size_t k = eq;
            while (k > s && space(text_[k - 1])) {
                k--;
            }
            if (eq < e && same(text_ + s, k - s, key)) {
                size_t v = eq + 1;
                while (v < e && space(text_[v])) {
                    v++;
                }
                value_start = v;
                value_end = e;
                return true;
            }
        }
        at = end < size_ ? end + 1 : size_;
    }
    return false;
}

bool Ini::get(const char *section, const char *key, char *out, size_t capacity) const
{
    size_t vs, ve, se;
    bool sf;
    if (capacity == 0 || !find(section, key, vs, ve, se, sf)) {
        return false;
    }
    const size_t n = ve - vs < capacity - 1 ? ve - vs : capacity - 1;
    memcpy(out, text_ + vs, n);
    out[n] = 0;
    return true;
}

int Ini::get_int(const char *section, const char *key, int fallback) const
{
    char v[24];
    if (!get(section, key, v, sizeof(v))) {
        return fallback;
    }
    char *end = nullptr;
    const long n = strtol(v, &end, 10);
    return (end == v || *end != 0) ? fallback : static_cast<int>(n);
}

bool Ini::get_bool(const char *section, const char *key, bool fallback) const
{
    char v[8];
    if (!get(section, key, v, sizeof(v))) {
        return fallback;
    }
    for (char &c : v) {
        c = lower(c);
    }
    if (strcmp(v, "true") == 0 || strcmp(v, "yes") == 0 || strcmp(v, "on") == 0 || strcmp(v, "1") == 0) {
        return true;
    }
    if (strcmp(v, "false") == 0 || strcmp(v, "no") == 0 || strcmp(v, "off") == 0 || strcmp(v, "0") == 0) {
        return false;
    }
    return fallback;
}

bool Ini::replace(size_t at, size_t length, const char *with)
{
    const size_t n = strlen(with);
    if (size_ - length + n > kMaxText) {
        return false;
    }
    memmove(text_ + at + n, text_ + at + length, size_ - at - length + 1);
    memcpy(text_ + at, with, n);
    size_ = size_ - length + n;
    return true;
}

bool Ini::set(const char *section, const char *key, const char *value)
{
    size_t vs, ve, se;
    bool sf;
    if (find(section, key, vs, ve, se, sf)) {
        return replace(vs, ve - vs, value);
    }
    char line[160];
    const bool newline_needed = se > 0 && text_[se - 1] != '\n';
    if (sf) {
        snprintf(line, sizeof(line), "%s%s = %s\r\n", newline_needed ? "\r\n" : "", key, value);
        return replace(se, 0, line);
    }
    // A new section at the end.
    const bool end_needs_newline = size_ > 0 && text_[size_ - 1] != '\n';
    snprintf(line, sizeof(line), "%s%s[%s]\r\n%s = %s\r\n", end_needs_newline ? "\r\n" : "", size_ > 0 ? "\r\n" : "",
             section, key, value);
    return replace(size_, 0, line);
}

bool Ini::set_int(const char *section, const char *key, int value)
{
    char v[16];
    snprintf(v, sizeof(v), "%d", value);
    return set(section, key, v);
}

bool Ini::set_bool(const char *section, const char *key, bool value)
{
    return set(section, key, value ? "true" : "false");
}

} // namespace fs
