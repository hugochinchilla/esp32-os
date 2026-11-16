// A program's settings file, C:\.config\<program>.ini (docs/DECISIONS.md):
// [section] headers, key = value lines, ; and # comments. Setting a value
// changes only its line; comments, blank lines and keys nobody knows stay
// as they were. A missing or damaged file just gives the defaults.
#pragma once

#include <cstddef>

#include "core/fs/fs.hpp"

namespace fs {

class Ini {
public:
    static constexpr size_t kMaxText = 2048; // ponytail: plenty for one program's settings

    // Reads the file (or its .tmp copy, if a save was cut short). false when
    // there is none; the settings are then empty and the defaults apply.
    bool load(const Storage &storage, const char *path);
    // Writes the file safely: the text goes to path.tmp first, then takes the
    // old file's place, so a power cut leaves the old or the new settings.
    ports::FsError save(const Storage &storage, const char *path) const;

    // Parses text directly (tests).
    void set_text(const char *text);
    const char *text() const { return text_; }

    bool get(const char *section, const char *key, char *out, size_t capacity) const;
    int get_int(const char *section, const char *key, int fallback) const;
    bool get_bool(const char *section, const char *key, bool fallback) const;
    // false when the text would grow past kMaxText.
    bool set(const char *section, const char *key, const char *value);
    bool set_int(const char *section, const char *key, int value);
    bool set_bool(const char *section, const char *key, bool value);

private:
    // Where the value of section/key starts and ends in text_; false if absent.
    bool find(const char *section, const char *key, size_t &value_start, size_t &value_end,
              size_t &section_end, bool &section_found) const;
    bool replace(size_t at, size_t length, const char *with);

    char text_[kMaxText + 1] = "";
    size_t size_ = 0;
};

} // namespace fs
