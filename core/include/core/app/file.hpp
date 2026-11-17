// What a program file begins with (backlog item 27): a `.app` is this
// header and, after it, the program's code as the machine's loader takes
// it. The header says what the system wants to know without loading the
// program: its name, its pictures and how much stack its task asks for.
//
// A program writes it with APP_INFO (core/app/app.hpp); the build copies it
// from the compiled program to the front of the file
// (tools/build_programs.sh). The layout is the same on the board and on the
// PC; nothing older than it is understood.
#pragma once

#include <cstdint>

namespace app {

struct FileInfo {
    static constexpr int kTitleSize = 32;
    char magic[8];    // kFileMagic
    uint32_t bytes;   // of this header: where the code starts in the file
    uint32_t stack;   // of its task, in bytes; 0: the least there is
    char title[kTitleSize]; // the program's name: its process, its menu entry
    uint8_t clear;    // the colour of the pictures that is seen through
    uint8_t has16;    // 0: no small picture (the system's own stands in)
    uint8_t has32;    // 0: no large picture
    uint8_t many;     // 0: one copy runs at a time (opened again, its window comes forward)
    uint8_t icon16[16 * 16]; // RRRGGGBB, a row after another
    uint8_t icon32[32 * 32];
};
static_assert(sizeof(FileInfo) == 1332, "the header's layout is part of the file format");

constexpr char kFileMagic[8] = {'C', 'H', 'I', 'N', 'A', 'P', 'P', '1'};

constexpr bool is_program(const FileInfo &info)
{
    for (int i = 0; i < 8; i++) {
        if (info.magic[i] != kFileMagic[i]) {
            return false;
        }
    }
    return info.bytes == sizeof(FileInfo);
}

// A picture drawn as rows of text, as the system's own are: each character
// is the colour its Ink gives it; a character with no Ink is seen through.
struct Ink {
    char key;
    uint8_t colour; // gfx::rgb(r, g, b)
};
constexpr uint8_t kClearInk = 0xE3; // gfx::rgb(7, 0, 3): magenta, never drawn

namespace detail {
template <int S, int N>
constexpr void paint(uint8_t *out, const char *const (&rows)[S], const Ink (&inks)[N])
{
    for (int r = 0; r < S; r++) {
        for (int q = 0; q < S; q++) {
            uint8_t v = kClearInk;
            for (int i = 0; i < N; i++) {
                if (rows[r][q] == inks[i].key) {
                    v = inks[i].colour;
                }
            }
            out[r * S + q] = v;
        }
    }
}

constexpr FileInfo named(const char *title, uint32_t stack)
{
    FileInfo info{};
    for (int i = 0; i < 8; i++) {
        info.magic[i] = kFileMagic[i];
    }
    info.bytes = sizeof(FileInfo);
    info.stack = stack;
    for (int i = 0; i < FileInfo::kTitleSize - 1 && title[i] != 0; i++) {
        info.title[i] = title[i];
    }
    info.clear = kClearInk;
    return info;
}
} // namespace detail

// The header of a program with no picture of its own.
constexpr FileInfo file_info(const char *title, uint32_t stack) { return detail::named(title, stack); }
// With a small picture (16 rows of 16).
template <int N>
constexpr FileInfo file_info(const char *title, uint32_t stack, const char *const (&small)[16], const Ink (&inks)[N])
{
    FileInfo info = detail::named(title, stack);
    detail::paint(info.icon16, small, inks);
    info.has16 = 1;
    return info;
}
// With a small and a large one (32 rows of 32), each in its own inks.
template <int N, int M>
constexpr FileInfo file_info(const char *title, uint32_t stack, const char *const (&small)[16],
                             const Ink (&small_inks)[N], const char *const (&large)[32], const Ink (&large_inks)[M])
{
    FileInfo info = file_info(title, stack, small, small_inks);
    detail::paint(info.icon32, large, large_inks);
    info.has32 = 1;
    return info;
}
// For a program that may run several copies at a time:
//   APP_INFO(app::many(app::file_info(...)));
constexpr FileInfo many(FileInfo info)
{
    info.many = 1;
    return info;
}

} // namespace app
