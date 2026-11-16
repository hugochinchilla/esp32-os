// Tests for the multi-line text editing widget and the clipboard.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/wm/text_edit.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

using wm::key::Down;
using wm::key::End;
using wm::key::Home;
using wm::key::Left;
using wm::key::Right;
using wm::key::Up;

class EditWindow final : public wm::Window {
public:
    EditWindow() : Window("Edit", {20, 20, 400, 300}, nullptr), edit({0, 0, 392, 272})
    {
        add(edit);
    }
    wm::TextEdit edit;
};

struct Rig {
    host::MemoryDisplay display{640, 480};
    host::MemoryPointer pointer;
    host::FixedClock clock;
    wm::Wm wm{display, pointer, clock};
    EditWindow w;
    std::vector<uint8_t> text = std::vector<uint8_t>(512 * 1024);
    std::vector<uint8_t> undo = std::vector<uint8_t>(64 * 1024);
    std::vector<uint8_t> clip = std::vector<uint8_t>(64 * 1024);

    Rig()
    {
        w.edit.attach(text.data(), text.size(), undo.data(), undo.size());
        wm::clipboard::init(clip.data(), clip.size());
        wm.add(w);
        wm.start();
        wm.set_animations(false);
    }
    void key(uint16_t usage, uint8_t mods = 0)
    {
        const uint16_t mod_keys[2] = {wm::key::LeftShift, wm::key::LeftCtrl};
        for (int i = 0; i < 2; i++) {
            if (mods & (1 << i)) {
                wm.key(mod_keys[i], true);
            }
        }
        wm.key(usage, true);
        wm.key(usage, false);
        for (int i = 0; i < 2; i++) {
            if (mods & (1 << i)) {
                wm.key(mod_keys[i], false);
            }
        }
    }
    std::string contents()
    {
        std::string s;
        for (size_t i = 0; i < w.edit.length(); i++) {
            s += static_cast<char>(w.edit.at(i));
        }
        return s;
    }
    std::string file()
    {
        std::string s(w.edit.file_size(), 0);
        // In odd-sized pieces, to cover the CRLF split.
        size_t off = 0;
        while (off < s.size()) {
            size_t n = w.edit.save_bytes(off, reinterpret_cast<uint8_t *>(&s[off]), 3);
            if (n == 0) {
                break;
            }
            off += n;
        }
        return s;
    }
    void load(const std::string &s)
    {
        w.edit.load(reinterpret_cast<const uint8_t *>(s.data()), s.size());
    }
    bool no_stale_pixels()
    {
        std::vector<uint8_t> before(display.pixels(), display.pixels() + 640 * 480);
        wm.repaint_all();
        return memcmp(before.data(), display.pixels(), before.size()) == 0;
    }
};

static void test_load_save()
{
    Rig rig;
    rig.load("one\r\ntwo\r\n\r\nfour");
    CHECK(rig.contents() == "one\ntwo\n\nfour" && rig.w.edit.crlf());
    CHECK(rig.w.edit.line_count() == 4 && !rig.w.edit.modified());
    CHECK(rig.file() == "one\r\ntwo\r\n\r\nfour" && rig.w.edit.file_size() == 16);
    rig.load("unix\nlines\n");
    CHECK(!rig.w.edit.crlf() && rig.file() == "unix\nlines\n");
    // Too big to fit: refused.
    std::string huge(600 * 1024, 'x');
    CHECK(!rig.w.edit.load(reinterpret_cast<const uint8_t *>(huge.data()), huge.size()));
}

static void test_typing_and_moving()
{
    Rig rig;
    rig.load("");
    rig.wm.text("Hola, año");
    rig.key(wm::key::Enter);
    rig.wm.text("second line");
    CHECK(rig.contents() == "Hola, a\xf1o\nsecond line");
    CHECK(rig.w.edit.modified() && rig.w.edit.line_count() == 2 && rig.w.edit.caret_line() == 1);
    // Up keeps the column; Home and End; words.
    rig.key(Home);
    rig.key(Right, wm::kCtrl);
    CHECK(rig.w.edit.caret() == 10 + 7); // after "second "
    rig.key(Up);
    CHECK(rig.w.edit.caret() == 7); // column 7 of the first line
    rig.key(End);
    CHECK(rig.w.edit.caret() == 9);
    rig.key(Left, wm::kCtrl);
    CHECK(rig.w.edit.caret() == 6);
    // Selection with Shift, typing over it, Backspace and Delete.
    rig.key(End, wm::kShift);
    CHECK(rig.w.edit.selection_start() == 6 && rig.w.edit.selection_end() == 9);
    rig.wm.text("x");
    CHECK(rig.contents() == "Hola, x\nsecond line");
    rig.key(wm::key::Backspace);
    rig.key(Home);
    rig.key(wm::key::Delete);
    CHECK(rig.contents() == "ola, \nsecond line");
    // Tab goes into the text and lines up at 8.
    rig.key(wm::key::Tab);
    CHECK(rig.contents() == "\tola, \nsecond line");
    CHECK(rig.no_stale_pixels());
}

static void test_undo_and_clipboard()
{
    Rig rig;
    rig.load("abc def");
    rig.key(End, wm::kCtrl);
    rig.wm.text(" ghi");
    rig.wm.text("j"); // typing joins the same undo step
    CHECK(rig.contents() == "abc def ghij");
    rig.key(wm::key::A + ('z' - 'a'), wm::kCtrl);
    CHECK(rig.contents() == "abc def");
    rig.key(wm::key::A + ('z' - 'a'), wm::kCtrl); // undo again: redo
    CHECK(rig.contents() == "abc def ghij");
    // Replacing a selection undoes in one step.
    rig.w.edit.select(0, 3);
    rig.wm.text("XYZ");
    CHECK(rig.contents() == "XYZ def ghij");
    rig.w.edit.undo();
    CHECK(rig.contents() == "abc def ghij");

    // Cut, copy, paste through the clipboard.
    rig.w.edit.select(4, 7);
    rig.key(wm::key::A + ('x' - 'a'), wm::kCtrl);
    CHECK(rig.contents() == "abc  ghij");
    CHECK(wm::clipboard::size() == 3 && memcmp(wm::clipboard::data(), "def", 3) == 0);
    rig.key(End, wm::kCtrl);
    rig.key(wm::key::A + ('v' - 'a'), wm::kCtrl);
    CHECK(rig.contents() == "abc  ghijdef");
    rig.key(wm::key::A, wm::kCtrl);
    rig.key(wm::key::A + ('c' - 'a'), wm::kCtrl);
    CHECK(wm::clipboard::size() == 12);
}

static void test_scrolling_and_large_files()
{
    Rig rig;
    // 300 KB: 10,000 lines of 30 characters.
    std::string big;
    for (int i = 0; i < 10000; i++) {
        char line[40];
        snprintf(line, sizeof(line), "line %05d .................\r\n", i);
        big += line;
    }
    rig.load(big);
    CHECK(rig.w.edit.line_count() == 10001);
    CHECK(rig.file() == big);
    const int rows = rig.w.edit.visible_rows();

    // Ctrl+End scrolls to the end; PageUp goes back a screen.
    rig.key(End, wm::kCtrl);
    CHECK(rig.w.edit.caret_line() == 10000 && rig.w.edit.top_line() == 10001 - rows);
    rig.key(wm::key::PageUp);
    CHECK(rig.w.edit.caret_line() == 10000 - (rows - 1));
    // Typing in the middle of a big file.
    rig.w.edit.select(150000, 150000);
    rig.wm.text("ñ");
    CHECK(rig.w.edit.length() == big.size() - 10000 + 1);
    CHECK(rig.w.edit.at(150000) == 0xF1);
    rig.key(wm::key::Enter);
    CHECK(rig.w.edit.line_count() == 10002);
    // Down from the top, column kept across short lines.
    rig.load("long line here\nab\nanother long line");
    rig.w.edit.select(12, 12);
    rig.key(Down);
    CHECK(rig.w.edit.caret() == 17); // end of "ab"
    rig.key(Down);
    CHECK(rig.w.edit.caret() == 18 + 12); // column 12 again
    CHECK(rig.no_stale_pixels());
}

static void test_mouse()
{
    Rig rig;
    rig.load("hello world\nsecond");
    const gfx::Rect c = rig.w.client();
    auto click = [&](int col, int row, bool twice) {
        const int x = c.x + 2 + 2 + col * 6 + 1, y = c.y + 2 + 1 + row * 13 + 5;
        rig.clock.now += 1000000;
        rig.wm.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 0, 0});
        rig.wm.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 1, 0});
        rig.wm.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 0, 0});
        if (twice) {
            rig.clock.now += 100000;
            rig.wm.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 1, 0});
            rig.wm.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 0, 0});
        }
    };
    click(3, 1, false);
    CHECK(rig.w.edit.caret() == 12 + 3 && !rig.w.edit.has_selection());
    click(7, 0, true); // "world"
    CHECK(rig.w.edit.selection_start() == 6 && rig.w.edit.selection_end() == 11);
    // Dragging selects.
    const int x0 = c.x + 4 + 1, y0 = c.y + 3 + 5;
    rig.clock.now += 1000000;
    rig.wm.mouse({true, static_cast<int16_t>(x0), static_cast<int16_t>(y0), 1, 0});
    rig.wm.mouse({true, static_cast<int16_t>(x0 + 6 * 5), static_cast<int16_t>(y0 + 13), 1, 0});
    rig.wm.mouse({true, static_cast<int16_t>(x0 + 6 * 5), static_cast<int16_t>(y0 + 13), 0, 0});
    CHECK(rig.w.edit.selection_start() == 0 && rig.w.edit.selection_end() == 12 + 5);
    CHECK(rig.no_stale_pixels());
}

int main()
{
    test_load_save();
    test_typing_and_moving();
    test_undo_and_clipboard();
    test_scrolling_and_large_files();
    test_mouse();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
