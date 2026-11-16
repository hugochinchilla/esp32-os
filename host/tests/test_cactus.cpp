// Cactus Patch: the rules (core/games/cactus.hpp), then the window driven
// with the mouse and the keyboard. Given a folder, it writes the game's
// reference pictures there instead (see tools/check_pictures.py).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/fs/memory_volume.hpp"
#include "core/games/cactus.hpp"
#include "core/shell/shell.hpp"
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

namespace {

using games::CactusField;
using State = CactusField::State;
using Tile = CactusField::Tile;

/* ---- The rules -------------------------------------------------------------- */

int count(const CactusField &f, Tile what)
{
    int n = 0;
    for (int r = 0; r < f.rows(); r++) {
        for (int c = 0; c < f.cols(); c++) {
            n += f.tile(c, r) == what;
        }
    }
    return n;
}

int cacti_in(const CactusField &f)
{
    int n = 0;
    for (int r = 0; r < f.rows(); r++) {
        for (int c = 0; c < f.cols(); c++) {
            n += f.has_cactus(c, r);
        }
    }
    return n;
}

void test_first_tile_is_safe()
{
    const int sizes[3][3] = {{9, 9, 10}, {16, 16, 40}, {30, 16, 99}};
    for (uint32_t seed = 0; seed < 300; seed++) {
        const int *s = sizes[seed % 3];
        CactusField f;
        f.reset(s[0], s[1], s[2]);
        CHECK(f.state() == State::Ready && cacti_in(f) == 0);
        const int col = static_cast<int>(seed * 7 % static_cast<uint32_t>(s[0]));
        const int row = static_cast<int>(seed * 5 % static_cast<uint32_t>(s[1]));
        CHECK(f.open(col, row, seed));
        CHECK(f.state() == State::Playing || f.state() == State::Won);
        CHECK(cacti_in(f) == s[2]);
        // Nothing on the tile or next to it, so it opens an area.
        CHECK(f.tile(col, row) == Tile::Open0);
        // Every open tile shows the number of cacti around it.
        for (int r = 0; r < f.rows(); r++) {
            for (int c = 0; c < f.cols(); c++) {
                if (f.tile(c, r) > Tile::Open8) {
                    continue;
                }
                int n = 0;
                for (int dr = -1; dr <= 1; dr++) {
                    for (int dc = -1; dc <= 1; dc++) {
                        n += f.has_cactus(c + dc, r + dr);
                    }
                }
                CHECK(!f.has_cactus(c, r) && static_cast<int>(f.tile(c, r)) == n);
            }
        }
    }
    // The same seed gives the same field; another seed, another one.
    CactusField a, b, c;
    a.open(4, 4, 1234);
    b.open(4, 4, 1234);
    c.open(4, 4, 1235);
    bool same = true, differs = false;
    for (int i = 0; i < 81; i++) {
        same = same && a.has_cactus(i % 9, i / 9) == b.has_cactus(i % 9, i / 9);
        differs = differs || a.has_cactus(i % 9, i / 9) != c.has_cactus(i % 9, i / 9);
    }
    CHECK(same && differs);

    // A crowded field: only the first tile itself is kept clear.
    CactusField tight;
    tight.reset(3, 3, 100);
    CHECK(tight.cacti() == 8);
    CHECK(tight.open(1, 1, 5) && tight.state() == State::Won);
    CHECK(tight.tile(1, 1) == Tile::Open8 && count(tight, Tile::Marked) == 8);
    CactusField one;
    one.reset(1, 1, 5);
    CHECK(one.cacti() == 0 && one.open(0, 0, 1) && one.state() == State::Won);
}

void test_play()
{
    // 5 x 4, cacti at a (0,0) and b (4,3):
    //   a 1 . . .
    //   1 1 . . .
    //   . . . 1 1
    //   . . . 1 b
    CactusField f;
    f.reset(5, 4, 0);
    const uint16_t where[] = {0, 19};
    f.plant(where, 2);
    CHECK(f.state() == State::Playing && f.cacti() == 2 && f.unmarked() == 2);

    // Markers: on covered tiles only, and a marked tile does not open.
    CHECK(f.toggle_marker(2, 0) && f.tile(2, 0) == Tile::Marked && f.unmarked() == 1);
    CHECK(!f.open(2, 0, 0) && f.tile(2, 0) == Tile::Marked);
    CHECK(f.toggle_marker(2, 0) && f.tile(2, 0) == Tile::Covered && f.unmarked() == 2);
    CHECK(!f.toggle_marker(-1, 0) && !f.toggle_marker(5, 0) && !f.open(0, 4, 0));

    // A number opens alone; an empty tile opens its whole area, up to and
    // including the numbers around it, but not across a marker.
    CHECK(f.open(1, 0, 0) && f.tile(1, 0) == Tile::Open1);
    CHECK(count(f, Tile::Covered) == 19);
    f.toggle_marker(4, 0);
    CHECK(f.open(2, 1, 0));
    CHECK(f.tile(2, 1) == Tile::Open0 && f.tile(1, 1) == Tile::Open1 && f.tile(3, 3) == Tile::Open1);
    CHECK(f.tile(0, 0) == Tile::Covered && f.tile(4, 3) == Tile::Covered);
    CHECK(f.tile(4, 0) == Tile::Marked);
    CHECK(count(f, Tile::Covered) == 2 && f.state() == State::Playing);
    CHECK(!f.open(2, 1, 0)); // already open
    CHECK(!f.toggle_marker(2, 1));

    // The last safe tile wins, and the cacti get their markers.
    f.toggle_marker(4, 0);
    CHECK(f.open(4, 0, 0) && f.state() == State::Won && f.over());
    CHECK(f.tile(0, 0) == Tile::Marked && f.tile(4, 3) == Tile::Marked && f.unmarked() == 0);
    CHECK(!f.open(0, 0, 0) && !f.toggle_marker(0, 0) && !f.open_around(1, 0));

    // Losing: the cactus stepped on, the others, and wrong markers show.
    f.reset(5, 4, 0);
    f.plant(where, 2);
    f.toggle_marker(2, 2); // wrong
    f.toggle_marker(4, 3); // right
    CHECK(f.open(0, 0, 0) && f.state() == State::Lost);
    CHECK(f.tile(0, 0) == Tile::CactusHit && f.tile(2, 2) == Tile::WrongMarker);
    CHECK(f.tile(4, 3) == Tile::Marked && f.tile(1, 1) == Tile::Covered);
    f.reset(5, 4, 0);
    f.plant(where, 2);
    f.open(4, 3, 0);
    CHECK(f.tile(4, 3) == Tile::CactusHit && f.tile(0, 0) == Tile::Cactus);
}

void test_open_around()
{
    // 3 x 3 with cacti in the top corners: the middle of the top row is 2.
    //   a 2 b
    //   1 2 1
    //   . . .
    CactusField f;
    f.reset(3, 3, 0);
    const uint16_t where[] = {0, 2};
    f.plant(where, 2);
    f.open(1, 0, 0);
    CHECK(f.tile(1, 0) == Tile::Open2);
    CHECK(!f.open_around(1, 0)); // no markers yet
    CHECK(!f.open_around(1, 1)); // not open
    f.toggle_marker(0, 0);
    CHECK(!f.open_around(1, 0)); // one of two
    f.toggle_marker(2, 0);
    CHECK(f.open_around(1, 0));
    CHECK(f.tile(0, 1) == Tile::Open1 && f.tile(1, 1) == Tile::Open2 && f.tile(2, 1) == Tile::Open1);
    CHECK(f.state() == State::Playing && f.tile(1, 2) == Tile::Covered);
    CHECK(!f.open_around(1, 0)); // nothing left to open
    CHECK(f.open(1, 2, 0) && f.state() == State::Won);

    // With a marker in the wrong place it steps on the cactus.
    f.reset(3, 3, 0);
    f.plant(where, 2);
    f.open(1, 0, 0);
    f.toggle_marker(0, 0);
    f.toggle_marker(0, 1);
    CHECK(f.open_around(1, 0) && f.state() == State::Lost);
    CHECK(f.tile(2, 0) == Tile::CactusHit && f.tile(0, 1) == Tile::WrongMarker);
    CHECK(f.tile(1, 1) == Tile::Open2); // the safe ones opened all the same
}

/* ---- The window ------------------------------------------------------------- */

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
host::HostMemory memory;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);

void mouse(int x, int y, uint8_t buttons)
{
    clock_.now += 300000;
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
}

wm::Window *game()
{
    for (int z = 0; z < desktop.window_count(); z++) {
        if (strcmp(desktop.window_at(z)->title(), "Cactus Patch") == 0) {
            return desktop.window_at(z);
        }
    }
    return nullptr;
}

// The middle of a tile, on the screen.
void tile_xy(int col, int row, int &x, int &y)
{
    const gfx::Rect c = game()->client();
    x = c.x + 10 + col * 16 + 8;
    y = c.y + 44 + row * 16 + 8;
}

void click_tile(int col, int row, uint8_t button = 1)
{
    int x, y;
    tile_xy(col, row, x, y);
    mouse(x, y, 0);
    mouse(x, y, button);
    mouse(x, y, 0);
}

void key(uint16_t usage, bool alt = false)
{
    if (alt) {
        desktop.key(wm::key::LeftAlt, true);
    }
    desktop.key(usage, true);
    desktop.key(usage, false);
    if (alt) {
        desktop.key(wm::key::LeftAlt, false);
    }
}

void letter(char c, bool alt = false) { key(static_cast<uint16_t>(wm::key::A + (c - 'a')), alt); }

void wait_seconds(int n)
{
    for (int i = 0; i < n * 4; i++) {
        clock_.now += 250000;
        desktop.tick();
    }
}

bool no_stale_pixels()
{
    std::string before(reinterpret_cast<char *>(display.pixels()), 640 * 480);
    desktop.repaint_all();
    return memcmp(before.data(), display.pixels(), before.size()) == 0;
}

void start_desktop()
{
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    constexpr size_t kRamDisk = 256 * 1024;
    static fs::MemoryVolume disk(memory.allocate(kRamDisk), kRamDisk, "RAM disk");
    storage.mount('C', disk);
    desktop.set_animations(false);
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_time(9, 41, 0);
}

void test_window()
{
    start_desktop();
    const shell::App *app = shell::find_app("Cactus Patch");
    CHECK(app != nullptr && app->icon != nullptr);
    shell::launch(*app);
    desktop.update();
    CHECK(game() != nullptr && desktop.active() == game());
    CactusField &f = shell::cactus_patch_field();
    CHECK(f.cols() == 9 && f.rows() == 9 && f.cacti() == 10 && f.state() == State::Ready);
    CHECK(game()->frame().w == 172 && game()->frame().h == 244);
    CHECK(no_stale_pixels());

    // Right click marks before and during a game; the clock waits for the
    // first tile opened.
    click_tile(0, 0, 2);
    CHECK(f.tile(0, 0) == Tile::Marked && f.unmarked() == 9 && f.state() == State::Ready);
    wait_seconds(2);
    CHECK(shell::cactus_patch_seconds() == 0);
    click_tile(0, 0); // marked: stays shut
    CHECK(f.state() == State::Ready);
    click_tile(0, 0, 2);
    CHECK(f.tile(0, 0) == Tile::Covered && f.unmarked() == 10);

    // While the button is held the tile under the pointer looks open, and
    // letting go elsewhere opens that other tile.
    int x, y, x2, y2;
    tile_xy(4, 4, x, y);
    tile_xy(5, 4, x2, y2);
    mouse(x, y, 0);
    mouse(x, y, 1);
    CHECK(f.state() == State::Ready);
    CHECK(display.pixels()[y * 640 + x] == gfx::rgb(7, 6, 2)); // sand
    CHECK(no_stale_pixels());
    mouse(x2, y2, 1);
    CHECK(display.pixels()[y * 640 + x] == gfx::kGrey);
    CHECK(no_stale_pixels());
    mouse(x2, y2, 0);
    CHECK(f.state() == State::Playing && f.tile(5, 4) == Tile::Open0);
    CHECK(no_stale_pixels());
    wait_seconds(3);
    CHECK(shell::cactus_patch_seconds() == 3);
    CHECK(no_stale_pixels());

    // A marker repaints its tile and the counter, each pixel once.
    int cc = -1, cr = -1;
    for (int i = 0; i < 81 && cc < 0; i++) {
        if (f.tile(i % 9, i / 9) == Tile::Covered) {
            cc = i % 9;
            cr = i / 9;
        }
    }
    CHECK(cc >= 0);
    tile_xy(cc, cr, x, y);
    mouse(x, y, 0);
    {
        std::vector<uint8_t> counts(640 * 480, 0);
        gfx::Canvas::count_writes(counts.data());
        mouse(x, y, 2);
        gfx::Canvas::count_writes(nullptr);
        int touched = 0, most = 0;
        for (uint8_t n : counts) {
            touched += n != 0;
            most = n > most ? n : most;
        }
        CHECK(touched == 16 * 16 + 52 * 20 && most == 1);
    }
    mouse(x, y, 0);
    CHECK(f.tile(cc, cr) == Tile::Marked);
    CHECK(no_stale_pixels());

    // The whole window paints each pixel once too.
    {
        std::vector<uint8_t> counts(640 * 480, 0);
        game()->invalidate();
        gfx::Canvas::count_writes(counts.data());
        desktop.update();
        gfx::Canvas::count_writes(nullptr);
        const gfx::Rect c = game()->client();
        int most = 0, missed = 0;
        for (int py = c.y; py < c.bottom(); py++) {
            for (int px = c.x; px < c.right(); px++) {
                const int n = counts[py * 640 + px];
                most = n > most ? n : most;
                missed += n == 0;
            }
        }
        CHECK(most == 1 && missed == 0);
    }

    // F2 and the face button start again; the clock goes back to zero.
    key(0x3B);
    CHECK(f.state() == State::Ready && shell::cactus_patch_seconds() == 0 && f.unmarked() == 10);
    wait_seconds(2);
    CHECK(shell::cactus_patch_seconds() == 0);
    click_tile(2, 2);
    CHECK(f.state() == State::Playing);
    const gfx::Rect c = game()->client();
    mouse(c.x + c.w / 2, c.y + 20, 0);
    mouse(c.x + c.w / 2, c.y + 20, 1);
    CHECK(f.state() == State::Playing && no_stale_pixels());
    mouse(c.x + c.w / 2, c.y + 20, 0);
    CHECK(f.state() == State::Ready && no_stale_pixels());

    // Losing stops the clock, and nothing more can be opened or marked.
    const uint16_t where[] = {0, 80};
    f.plant(where, 2);
    shell::cactus_patch_refresh();
    wait_seconds(2);
    CHECK(shell::cactus_patch_seconds() == 2);
    click_tile(0, 0);
    CHECK(f.state() == State::Lost);
    wait_seconds(2);
    CHECK(shell::cactus_patch_seconds() == 2);
    click_tile(4, 4);
    click_tile(5, 5, 2);
    CHECK(f.tile(4, 4) == Tile::Covered && f.tile(5, 5) == Tile::Covered);
    CHECK(no_stale_pixels());

    // Clicking a number with its markers placed opens the rest.
    key(0x3B);
    const uint16_t corners[] = {0, 2};
    f.plant(corners, 2);
    shell::cactus_patch_refresh();
    click_tile(1, 0);
    click_tile(0, 0, 2);
    click_tile(2, 0, 2);
    click_tile(1, 0);
    CHECK(f.tile(1, 1) == Tile::Open2 && f.state() == State::Playing);
    click_tile(4, 4);
    CHECK(f.state() == State::Won && f.unmarked() == 0);
    CHECK(no_stale_pixels());

    // Game > Medium and Large: a new field and a window to fit, kept on the
    // screen.
    letter('g', true);
    letter('m');
    CHECK(f.cols() == 16 && f.rows() == 16 && f.cacti() == 40 && f.state() == State::Ready);
    CHECK(game()->frame().w == 284 && game()->frame().h == 356);
    CHECK(no_stale_pixels());
    click_tile(15, 15);
    CHECK(f.state() == State::Playing && f.tile(15, 15) == Tile::Open0);
    desktop.move(*game(), 400, 200);
    letter('g', true);
    letter('l');
    CHECK(f.cols() == 30 && f.cacti() == 99);
    const gfx::Rect big = game()->frame();
    CHECK(big.w == 508 && big.h == 356 && big.right() <= 640 && big.bottom() <= desktop.work_area().h);
    CHECK(no_stale_pixels());
    click_tile(29, 0);
    CHECK(f.tile(29, 0) == Tile::Open0);

    // A click on a window behind does not reach the game; closing ends it.
    wait_seconds(1);
    CHECK(shell::cactus_patch_seconds() == 1);
    letter('g', true);
    letter('c');
    CHECK(game() == nullptr);
    wait_seconds(2);
    shell::launch(*app);
    CHECK(game() != nullptr && f.state() == State::Ready && shell::cactus_patch_seconds() == 0);
    CHECK(f.cols() == 30); // the size is kept
    letter('g', true);
    letter('s');
    CHECK(f.cols() == 9 && game()->frame().w == 172);
    CHECK(no_stale_pixels());
}

/* ---- Reference pictures ----------------------------------------------------- */

int write_pictures(const std::string &folder)
{
    auto save = [&](const char *name) {
        desktop.update();
        const std::string path = folder + "/" + name + ".raw";
        FILE *file = fopen(path.c_str(), "wb");
        if (file == nullptr || fwrite(display.pixels(), 1, 640 * 480, file) != 640 * 480) {
            printf("cannot write %s\n", path.c_str());
            failures++;
        }
        if (file != nullptr) {
            fclose(file);
        }
    };
    start_desktop();
    // Only the game on the desktop.
    while (desktop.window_count() > 0) {
        desktop.close(*desktop.window_at(0));
    }
    shell::launch(*shell::find_app("Cactus Patch"));
    CactusField &f = shell::cactus_patch_field();
    save("40-cactus-new-game");

    // A game under way: numbers, markers, and a tile held down.
    const uint16_t patch[] = {3, 9, 13, 21, 26, 38, 44, 58, 66, 79};
    f.plant(patch, 10);
    shell::cactus_patch_refresh();
    click_tile(8, 0);
    click_tile(0, 8);
    click_tile(5, 6);
    click_tile(3, 0, 2);
    click_tile(0, 1, 2);
    click_tile(8, 2, 2);
    click_tile(0, 0, 2); // a wrong one
    wait_seconds(47);
    int x, y;
    tile_xy(6, 8, x, y);
    mouse(x, y, 0);
    mouse(x, y, 1);
    save("41-cactus-playing");
    tile_xy(4, 1, x, y);
    mouse(x, y, 1);
    mouse(x, y, 0); // let go on a cactus
    save("42-cactus-lost");

    // Won, on the large field, with every number from 1 to 8 on show.
    key(0x3B);
    key(0x0A, true); // Alt+G
    key(0x0F);       // L
    static uint16_t ring[64];
    int n = 0;
    // Around tile (2, 2): 8 cacti. Then rows of fewer, to show 7 down to 1.
    const int centres[8][2] = {{2, 2}, {6, 2}, {10, 2}, {14, 2}, {18, 2}, {22, 2}, {26, 2}, {2, 6}};
    for (int k = 0; k < 8; k++) {
        int placed = 0;
        for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
                if ((dc != 0 || dr != 0) && placed < 8 - k) {
                    ring[n++] = static_cast<uint16_t>((centres[k][1] + dr) * 30 + centres[k][0] + dc);
                    placed++;
                }
            }
        }
    }
    f.plant(ring, n);
    shell::cactus_patch_refresh();
    wait_seconds(128);
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 30; c++) {
            if (!f.has_cactus(c, r)) {
                click_tile(c, r);
            }
        }
    }
    save("43-cactus-won-large");

    // The Game menu.
    key(0x0A, true);
    save("44-cactus-game-menu");
    key(wm::key::Escape);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc > 1) {
        return write_pictures(argv[1]);
    }
    test_first_tile_is_safe();
    test_play();
    test_open_around();
    test_window();
    if (failures != 0) {
        printf("%d checks failed\n", failures);
        return 1;
    }
    printf("cactus: all checks passed\n");
    return 0;
}
