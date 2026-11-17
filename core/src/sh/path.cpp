#include "core/sh/sh.hpp"

#include <cstring>

namespace sh {
namespace path {

namespace {

bool slash(char c) { return c == '\\' || c == '/'; }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool match_here(const char *pattern, const char *name)
{
    for (; *pattern != 0; pattern++, name++) {
        if (*pattern == '*') {
            while (pattern[1] == '*') {
                pattern++;
            }
            for (const char *rest = name;; rest++) {
                if (match_here(pattern + 1, rest)) {
                    return true;
                }
                if (*rest == 0) {
                    return false;
                }
            }
        }
        if (*name == 0 || (*pattern != '?' && lower(*pattern) != lower(*name))) {
            return false;
        }
    }
    return *name == 0;
}

} // namespace

bool resolve(const char *cwd, const char *typed, char *out, int capacity)
{
    if (capacity < 4) {
        return false;
    }
    const char typed_drive = fs::path::drive(typed);
    const char *rest = typed_drive != 0 ? typed + 2 : typed;
    // With a drive letter, or a slash in front, it counts from the root;
    // otherwise from the folder the session is in.
    const bool from_root = typed_drive != 0 || slash(rest[0]);
    int length;
    if (from_root) {
        out[0] = typed_drive != 0 ? typed_drive : fs::path::drive(cwd);
        out[1] = ':';
        out[2] = '\\';
        out[3] = 0;
        length = 3;
    } else {
        length = static_cast<int>(strlen(cwd));
        if (length >= capacity) {
            return false;
        }
        memcpy(out, cwd, static_cast<size_t>(length) + 1);
    }

    while (*rest != 0) {
        while (slash(*rest)) {
            rest++;
        }
        int size = 0;
        while (rest[size] != 0 && !slash(rest[size])) {
            size++;
        }
        if (size == 0 || (size == 1 && rest[0] == '.')) {
            // nothing, or "here"
        } else if (size == 2 && rest[0] == '.' && rest[1] == '.') {
            // One folder up; the root is its own parent.
            while (length > 3 && out[length - 1] != '\\') {
                length--;
            }
            if (length > 3) {
                length--; // the '\' itself
            }
            out[length] = 0;
        } else {
            const bool separator = length > 3;
            if (length + (separator ? 1 : 0) + size >= capacity) {
                return false;
            }
            if (separator) {
                out[length++] = '\\';
            }
            memcpy(out + length, rest, static_cast<size_t>(size));
            length += size;
            out[length] = 0;
        }
        rest += size;
    }
    return true;
}

bool match(const char *pattern, const char *name)
{
    if (name[0] == '.' && pattern[0] != '.') {
        return false;
    }
    return match_here(pattern, name);
}

bool has_wildcard(const char *text) { return strchr(text, '*') != nullptr || strchr(text, '?') != nullptr; }

bool same(const char *a, const char *b)
{
    for (; *a != 0 && *b != 0; a++, b++) {
        if (lower(*a) != lower(*b)) {
            return false;
        }
    }
    return *a == *b;
}

} // namespace path
} // namespace sh
