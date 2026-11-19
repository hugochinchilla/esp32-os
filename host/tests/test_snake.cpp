// The snake game: its rules on their own, then the Culebra window driven
// with keys and the clock.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/fs/memory_volume.hpp"
#include "core/games/snake.hpp"
#include "core/prog/prog.hpp"
#include "core/shell/shell.hpp"
#include "../../programs/test_hooks.hpp"
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

using games::Snake;
using Dir = Snake::Dir;
using State = Snake::State;
using Kind = Snake::Kind;

int count(const Snake &g, Kind kind)
{
    int n = 0;
    for (int row = 0; row < Snake::kRows; row++) {
        for (int col = 0; col < Snake::kCols; col++) {
            n += g.at(col, row).kind == kind;
        }
    }
    return n;
}

void test_start()
{
    Snake g;
    g.reset(7);
    CHECK(g.state() == State::Ready);
    CHECK(g.length() == 3 && g.berries() == 0);
    CHECK(g.head_col() == 11 && g.head_row() == 9);
    CHECK(count(g, Kind::Head) == 1 && count(g, Kind::Body) == 2 && count(g, Kind::Berry) == 1);
    // Head joins left; the middle joins both ways; the tail joins right.
    CHECK(g.at(11, 9).links == 8 && g.at(10, 9).links == (2 | 8) && g.at(9, 9).links == 2);
    CHECK(g.changed_count() == -1);
    // Nothing moves until a direction is given.
    CHECK(!g.step() && g.head_col() == 11 && g.changed_count() == 0);

    // The same seed gives the same berry; another seed, in time, another.
    Snake h;
    h.reset(7);
    CHECK(h.berry_col() == g.berry_col() && h.berry_row() == g.berry_row());
    bool differs = false;
    for (uint32_t seed = 8; seed < 12; seed++) {
        h.reset(seed);
        differs = differs || h.berry_col() != g.berry_col() || h.berry_row() != g.berry_row();
    }
    CHECK(differs);
}

void test_moving()
{
    Snake g;
    g.reset(1);
    g.place_berry(0, 0);
    CHECK(g.steer(Dir::Right)); // starts the game
    CHECK(g.state() == State::Playing);
    CHECK(!g.step());
    CHECK(g.head_col() == 12 && g.head_row() == 9 && g.length() == 3);
    CHECK(g.at(9, 9).kind == Kind::Empty && g.at(10, 9).links == 2);
    // Changed: the old tail, the new tail end, the old head, the new head.
    CHECK(g.changed_count() == 4);

    // Turning back on itself is ignored.
    g.steer(Dir::Left);
    g.step();
    CHECK(g.head_col() == 13 && g.heading() == Dir::Right);

    // Two quick turns are both kept, one for each step; a third is dropped.
    g.steer(Dir::Up);
    g.steer(Dir::Left);
    g.steer(Dir::Down);
    g.step();
    CHECK(g.head_col() == 13 && g.head_row() == 8);
    g.step();
    CHECK(g.head_col() == 12 && g.head_row() == 8);
    g.step();
    CHECK(g.head_col() == 11 && g.head_row() == 8 && g.heading() == Dir::Left);
    // Up then down in one step would fold it back: the second is ignored.
    g.steer(Dir::Up);
    g.steer(Dir::Down);
    g.step();
    g.step();
    CHECK(g.head_col() == 11 && g.head_row() == 6);
    // The corner piece joins down and right... one step later it is the bend.
    CHECK(g.at(11, 8).kind == Kind::Empty || g.at(11, 8).links != 0);
}

void test_eating()
{
    Snake g;
    g.reset(3);
    g.place_berry(12, 9);
    g.steer(Dir::Right);
    CHECK(g.step()); // eaten
    CHECK(g.length() == 4 && g.berries() == 1);
    CHECK(g.at(9, 9).kind == Kind::Body); // the tail stayed: longer by one
    CHECK(count(g, Kind::Berry) == 1);
    CHECK(g.at(g.berry_col(), g.berry_row()).kind == Kind::Berry);
    // Changed: old head, new head, the new berry.
    CHECK(g.changed_count() == 3);
    CHECK(g.changed_col(2) == g.berry_col() && g.changed_row(2) == g.berry_row());
    CHECK(count(g, Kind::Head) + count(g, Kind::Body) == 4);
}

void test_walls()
{
    Snake g;
    g.reset(1);
    g.place_berry(0, 0);
    g.steer(Dir::Right);
    for (int i = 0; i < 12; i++) {
        g.step();
    }
    CHECK(g.state() == State::Playing && g.head_col() == 23);
    g.step();
    CHECK(g.state() == State::Over);
    CHECK(g.head_col() == 23 && g.length() == 3 && g.changed_count() == 0);
    // Nothing more happens, whatever is pressed.
    CHECK(!g.steer(Dir::Up) && !g.toggle_pause() && !g.step());
    CHECK(g.state() == State::Over);

    g.reset(1);
    g.place_berry(0, 0);
    g.steer(Dir::Up);
    for (int i = 0; i < 9; i++) {
        g.step();
    }
    CHECK(g.state() == State::Playing && g.head_row() == 0);
    g.step();
    CHECK(g.state() == State::Over);
}

// Grows the snake to `length` along its row, berry by berry.
void grow(Snake &g, int length)
{
    g.steer(Dir::Right);
    while (g.length() < length) {
        g.place_berry(g.head_col() + 1, g.head_row());
        g.step();
    }
    g.place_berry(0, 0);
}

void test_itself()
{
    // Length 5 turning in a tight square: the head runs into its body.
    Snake g;
    g.reset(1);
    grow(g, 5);
    g.steer(Dir::Down);
    g.step();
    g.steer(Dir::Left);
    g.step();
    g.steer(Dir::Up);
    g.step();
    CHECK(g.state() == State::Over);
    CHECK(g.length() == 5 && count(g, Kind::Head) + count(g, Kind::Body) == 5);

    // Length 4 doing the same follows its own tail round, for ever.
    g.reset(1);
    grow(g, 4);
    const Dir round[4] = {Dir::Down, Dir::Left, Dir::Up, Dir::Right};
    for (int i = 0; i < 12; i++) {
        g.steer(round[i % 4]);
        g.step();
        CHECK(g.state() == State::Playing);
        CHECK(count(g, Kind::Head) == 1 && count(g, Kind::Body) == 3);
    }
}

void test_pause()
{
    Snake g;
    g.reset(1);
    CHECK(!g.toggle_pause()); // not started: nothing to pause
    g.steer(Dir::Right);
    CHECK(g.toggle_pause() && g.state() == State::Paused);
    CHECK(!g.step() && g.head_col() == 11);
    CHECK(g.toggle_pause() && g.state() == State::Playing);
    g.pause();
    CHECK(g.steer(Dir::Up) && g.state() == State::Playing); // a direction carries on too
    g.step();
    CHECK(g.head_row() == 8);
}

void test_filling_the_board()
{
    // Round a path that visits every cell (down the first column, then
    // back up row by row), with a berry always just ahead.
    Snake g;
    g.reset(1);
    g.place_berry(12, 9);
    int steps = 0;
    const int last_col = Snake::kCols - 1, last_row = Snake::kRows - 1;
    do {
        const int col = g.head_col(), row = g.head_row();
        Dir d;
        if (col == 0) {
            d = row == last_row ? Dir::Right : Dir::Down;
        } else if (row % 2 == 1) {
            d = col == last_col ? Dir::Up : Dir::Right;
        } else if (row == 0) {
            d = Dir::Left;
        } else {
            d = col == 1 ? Dir::Up : Dir::Left;
        }
        g.steer(d);
        // The berry goes where the head is about to be, when that is free.
        const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
        const int nc = col + dx[static_cast<int>(d)], nr = row + dy[static_cast<int>(d)];
        if (nc >= 0 && nc < Snake::kCols && nr >= 0 && nr < Snake::kRows &&
            g.at(nc, nr).kind == Kind::Empty && g.length() < Snake::kCells - 1) {
            g.place_berry(nc, nr);
        }
        g.step();
        steps++;
    } while (g.state() == State::Playing && steps < 5000);
    CHECK(g.state() == State::Won);
    CHECK(g.length() == Snake::kCells);
    CHECK(count(g, Kind::Empty) == 0 && count(g, Kind::Berry) == 0);
}

/* ---- The window ------------------------------------------------------------- */

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
host::HostMemory memory;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);

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

void wait(uint32_t us)
{
    // In small slices, as the UI loop ticks.
    for (uint32_t done = 0; done < us; done += 10000) {
        clock_.now += 10000;
        desktop.tick();
    }
}

const char *title() { return desktop.active() ? desktop.active()->title() : ""; }

bool no_stale_pixels()
{
    std::string before(reinterpret_cast<char *>(display.pixels()), 640 * 480);
    desktop.repaint_all();
    int x0 = 640, x1 = -1, y0 = 480, y1 = -1;
    for (int i = 0; i < 640 * 480; i++) {
        if (before[i] != static_cast<char>(display.pixels()[i])) {
            x0 = std::min(x0, i % 640), x1 = std::max(x1, i % 640);
            y0 = std::min(y0, i / 640), y1 = std::max(y1, i / 640);
        }
    }
    if (x1 >= 0) {
        printf("stale pixels within x %d..%d, y %d..%d\n", x0, x1, y0, y1);
    }
    return memcmp(before.data(), display.pixels(), before.size()) == 0;
}

constexpr uint32_t kStep = 130000; // Normal speed
constexpr uint16_t kF2 = 0x3B;

// own_task: the machine has tasks, so Culebra's code runs on one of its own
// (backlog item 26) and everything below must go as it does without.
void test_window(bool own_task)
{
    const shell::Program *app = shell::find_program("Culebra");
    CHECK(app != nullptr && app->icon != nullptr && app->built_in == nullptr);
    shell::start_program("Culebra");
    const Snake &g = shell::culebra_game(); // the game is in the window
    CHECK(strcmp(title(), "Culebra") == 0);
    CHECK(g.state() == State::Ready);
    CHECK(prog::runner().has_task(proc::table().find_by_name("Culebra")) == own_task);
    CHECK(prog::runner().tasks() == (own_task ? 1 : 0));

    // Nothing moves, and no timer runs, until an arrow key.
    wait(1000000);
    CHECK(g.head_col() == 11);
    key(wm::key::Up);
    CHECK(g.state() == State::Playing);
    wait(kStep * 3);
    CHECK(g.head_col() == 11 && g.head_row() == 6);
    CHECK(no_stale_pixels());

    // Space pauses and carries on; P too.
    key(wm::key::Space);
    CHECK(g.state() == State::Paused);
    wait(kStep * 3);
    CHECK(g.head_row() == 6);
    CHECK(no_stale_pixels());
    key(0x13); // P
    CHECK(g.state() == State::Playing);
    wait(kStep);
    CHECK(g.head_row() == 5);

    // Each step paints only the cells that changed, each pixel once.
    {
        std::string counts(640 * 480, 0);
        gfx::Canvas::count_writes(reinterpret_cast<uint8_t *>(&counts[0]));
        wait(kStep);
        gfx::Canvas::count_writes(nullptr);
        int touched = 0, most = 0;
        for (char n : counts) {
            touched += n != 0;
            most = n > most ? n : most;
        }
        CHECK(g.head_row() == 4);
        CHECK(touched == 4 * 12 * 12 && most == 1);
    }
    CHECK(no_stale_pixels());

    // Opening a menu pauses the game: nobody is steering.
    key(0x0A, true); // Alt+G
    CHECK(desktop.menu_open());
    wait(kStep * 2);
    CHECK(g.state() == State::Paused && g.head_row() == 4);
    key(wm::key::Escape);
    key(wm::key::Escape);
    CHECK(!desktop.menu_open());

    // So does another window coming to the front (How to play).
    key(wm::key::Left);
    CHECK(g.state() == State::Playing);
    key(0x0B, true); // Alt+H
    key(wm::key::Enter);
    CHECK(strcmp(title(), "How to play") == 0);
    const int col = g.head_col();
    wait(kStep * 3);
    CHECK(g.state() == State::Paused);
    key(wm::key::Enter);
    CHECK(strcmp(title(), "Culebra") == 0);
    CHECK(g.head_col() >= col - 1); // at most the step already due
    CHECK(no_stale_pixels());

    // Into the left wall: game over, and Space starts again.
    key(wm::key::Left);
    wait(kStep * 30);
    CHECK(g.state() == State::Over);
    CHECK(no_stale_pixels());
    key(wm::key::Space);
    CHECK(g.state() == State::Ready && g.length() == 3);

    // F2 starts a new game at any time.
    key(wm::key::Down);
    wait(kStep * 2);
    CHECK(g.head_row() == 11);
    key(kF2);
    CHECK(g.state() == State::Ready && g.head_row() == 9);

    // Speed, from the menu: Fast steps every 80 ms.
    key(0x0A, true); // Alt+G
    key(0x16);       // S: Speed
    key(0x09);       // F: Fast
    CHECK(!desktop.menu_open());
    key(wm::key::Up);
    wait(80000 * 4);
    CHECK(g.head_row() == 5);

    // Closing ends the program and the game with it: nothing is kept in
    // memory. Opened again, it is a new game at the speed last chosen
    // (C:\.config\culebra.ini).
    const int blocks = memory.in_use();
    key(wm::key::F4, true);
    CHECK(strcmp(title(), "Culebra") != 0);
    CHECK(memory.in_use() == blocks - 4); // the window, with the game in it, and its three controls
    CHECK(prog::runner().tasks() == 0);   // and its task
    wait(kStep * 3);
    shell::start_program("Culebra");
    desktop.update(); // a direct call, not an event: paint it
    const Snake &g2 = shell::culebra_game();
    CHECK(g2.state() == State::Ready && g2.head_row() == 9);
    CHECK(no_stale_pixels());
    key(wm::key::Up);
    wait(80000 * 4);
    CHECK(g2.head_row() == 5); // still Fast
    key(wm::key::F4, true);
    CHECK(memory.in_use() == blocks - 4);
}

} // namespace

int main()
{
    test_start();
    test_moving();
    test_eating();
    test_walls();
    test_itself();
    test_pause();
    test_filling_the_board();

    static fs::MemoryVolume ram_disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    test_window(false);

    // The same again with the program on a task of its own: a real thread
    // here. Every event, timer and menu action is handed to it.
    static host::ThreadTasks tasks;
    static host::ThreadEvent events[4];
    static prog::Kit kits[2];
    kits[0] = {&events[0], &events[1]};
    kits[1] = {&events[2], &events[3]};
    storage.remove("C:\\.config\\culebra.ini"); // the speed the first round left
    prog::runner().set_tasks(&tasks, kits, 2);
    test_window(true);
    CHECK(tasks.started == 2 && tasks.ended == 2); // launched twice, both given back
    CHECK(prog::runner().tasks() == 0);
    prog::runner().set_tasks(nullptr, nullptr, 0);
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
