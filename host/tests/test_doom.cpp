// Doom as a program file (backlog item 81), on the PC: doom.app is loaded
// as the board loads it, borrows the whole screen and runs its engine from
// a timer. Without the game's data (the WAD is nobody's to put in this
// repository) only the window that says so is checked. With the shareware
// DOOM1.WAD the test starts a game, walks, hands the screen back and takes
// it again, and leaves; pictures of the screen are saved on the way.
//
//   test_doom doom.app                       # no game data
//   test_doom doom.app DOOM1.WAD [folder]    # a game; pictures as .ppm
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/fs/memory_volume.hpp"
#include "core/os/os.hpp"
#include "core/prog/prog.hpp"
#include "core/proc/table.hpp"
#include "core/shell/shell.hpp"
#include "core/sys/log.hpp"
#include "core/wm/wm.hpp"
#include "../dl_loader.hpp"
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

// A clock that goes on by itself, a little with every look at it: the
// engine waits for the clock in loops of its own, and the test must give
// the same pictures on every PC.
class WalkingClock : public ports::Clock {
public:
    uint32_t micros() override { return now += 200; }
    uint32_t now = 0;
};

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
WalkingClock clock_;
host::HostSystem system_;
host::HostMemory memory;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);
host::DlLoader loader;
char log_room[16384];

void wait(uint32_t us)
{
    clock_.now += us;
    desktop.tick();
}

// A number of the game's tics, each a turn of the timer.
void tics(int count)
{
    for (int i = 0; i < count; i++) {
        wait(1000000 / 35);
    }
}

void press(uint16_t usage)
{
    desktop.key(usage, true);
    tics(2);
    desktop.key(usage, false);
    tics(2);
}

const char *title() { return desktop.active() != nullptr ? desktop.active()->title() : ""; }

// The screen as a picture file anything opens: the board's 256 colours
// spread to 8 bits each.
void save(const std::string &folder, const char *name)
{
    if (folder.empty()) {
        return;
    }
    std::ofstream out(folder + "/" + name + ".ppm", std::ios::binary);
    out << "P6\n" << display.width() << " " << display.height() << "\n255\n";
    const uint8_t *p = display.pixels();
    for (int i = 0; i < display.width() * display.height(); i++) {
        const uint8_t c = p[i];
        const char rgb[3] = {static_cast<char>((c >> 5) * 255 / 7), static_cast<char>(((c >> 2) & 7) * 255 / 7),
                             static_cast<char>((c & 3) * 255 / 3)};
        out.write(rgb, 3);
    }
}

// How many different colours the middle of the screen shows: a game's
// picture has many, a black or plain screen one.
int colours()
{
    bool seen[256] = {};
    int count = 0;
    const uint8_t *p = display.pixels();
    for (int y = 100; y < 380; y += 3) {
        for (int x = 20; x < 620; x += 3) {
            if (!seen[p[y * display.width() + x]]) {
                seen[p[y * display.width() + x]] = true;
                count++;
            }
        }
    }
    return count;
}

uint32_t screen_sum()
{
    uint32_t sum = 0;
    const uint8_t *p = display.pixels();
    for (int i = 0; i < display.width() * display.height(); i++) {
        sum = sum * 31 + p[i];
    }
    return sum;
}

void print_log()
{
    uint64_t from = sys::log().oldest();
    char text[512];
    size_t n;
    while ((n = sys::log().read(from, text, sizeof(text))) > 0) {
        fwrite(text, 1, n, stdout);
    }
}

std::vector<char> read_all(const char *path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        printf("usage: test_doom doom.app [DOOM1.WAD [folder for pictures]]\n");
        return 2;
    }
    const std::vector<char> program = read_all(argv[1]);
    CHECK(!program.empty());
    const std::vector<char> wad = argc > 2 ? read_all(argv[2]) : std::vector<char>();
    const std::string pictures = argc > 3 ? argv[3] : "";
    if (argc > 2 && wad.empty()) {
        printf("%s: not there, or empty\n", argv[2]);
        return 2;
    }

    sys::log().set_room(log_room, sizeof(log_room));
    static fs::MemoryVolume disk(memory.allocate(8 << 20), 8 << 20, "RAM disk");
    storage.mount('C', disk);
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_animations(false);
    proc::Table &t = proc::table();
    const proc::Scope as(&t, t.find_by_name("desktop"));
    wait(10000);
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
        wait(10000);
    }
    wait(2100000);
    shell::set_loader(&loader);
    storage.make_folder("C:\\Programs");
    CHECK(storage.write_file("C:\\Programs\\doom.app", program.data(), program.size()) == ports::FsError::Ok);
    const int processes = t.count();
    const uint32_t blocks = os::blocks_out();
    char why[120] = "";

    // What the file says of itself.
    static app::FileInfo info;
    CHECK(shell::program_info("C:\\Programs\\doom.app", info));
    CHECK(strcmp(info.title, "Doom") == 0 && info.stack == 16 * 1024 && info.has16 == 1 && info.many == 0);
    CHECK(program.size() <= shell::kMaxProgramBytes);

    // Without the game's data: a window that says what to do, and nothing
    // is taken.
    CHECK(shell::run_file("C:\\Programs\\doom.app", why, sizeof(why)));
    wait(200000);
    CHECK(loader.loaded == 1 && strcmp(title(), "Doom") == 0 && desktop.screen_lent_to() == nullptr);
    save(pictures, "doom-0-no-data");
    desktop.key(wm::key::Enter, true); // Quit is the default button then
    desktop.key(wm::key::Enter, false);
    wait(10000);
    wait(200000);
    CHECK(loader.loaded == 0 && t.count() == processes && os::blocks_out() == blocks);

    if (!wad.empty()) {
        CHECK(storage.write_file("C:\\Programs\\DOOM1.WAD", wad.data(), wad.size()) == ports::FsError::Ok);
        const uint8_t desktop_corner = display.pixels()[5 * 640 + 5];
        CHECK(shell::run_file("C:\\Programs\\doom.app", why, sizeof(why)));
        wait(10000);
        CHECK(strcmp(title(), "Doom") == 0 && desktop.screen_lent_to() == nullptr);
        // A moment later the engine starts, with the whole screen.
        wait(200000);
        CHECK(desktop.screen_lent_to() == desktop.active() && desktop.active() != nullptr);
        tics(40);
        CHECK(colours() > 20); // the title picture
        save(pictures, "doom-1-title");
        // Nothing of the desktop is painted meanwhile: the bars above and
        // below the game's picture stay black.
        CHECK(display.pixels()[5 * 640 + 5] == 0 && display.pixels()[475 * 640 + 600] == 0);

        // The menu, and a new game: Enter four times (the menu, New Game,
        // the episode, the skill).
        press(wm::key::Escape);
        tics(5);
        save(pictures, "doom-2-menu");
        press(wm::key::Enter);
        press(wm::key::Enter);
        press(wm::key::Enter);
        tics(80); // the screen melts into the level
        save(pictures, "doom-3-level");
        const uint32_t standing = screen_sum();
        CHECK(colours() > 20);
        // Keys reach the game as they are, modifiers too: forward with the
        // arrow, a shot with Ctrl.
        desktop.key(wm::key::Up, true);
        tics(30);
        desktop.key(wm::key::LeftCtrl, true);
        tics(6);
        save(pictures, "doom-4-walking");
        desktop.key(wm::key::LeftCtrl, false);
        desktop.key(wm::key::Up, false);
        CHECK(screen_sum() != standing);
        // W, A, S and D are the arrows and Q and E step sideways: each
        // moves the picture.
        for (const uint16_t usage : {0x1A, 0x16, 0x04, 0x07, 0x14, 0x08}) {
            const uint32_t before = screen_sum();
            desktop.key(usage, true);
            tics(12);
            desktop.key(usage, false);
            tics(2);
            CHECK(screen_sum() != before);
        }
        // The mouse turns the player.
        const uint32_t before_turn = screen_sum();
        for (int i = 0; i < 10; i++) {
            ports::MouseEvent move = {};
            move.x = 30;
            desktop.mouse(move);
            tics(1);
        }
        CHECK(screen_sum() != before_turn);
        save(pictures, "doom-5-turned");

        // A saved game is a file in C:\\Documents\\Doom: F2, the first
        // place, a name, Enter.
        press(0x3B); // F2
        press(wm::key::Enter);
        press(0x1A); // W: a letter here, not a step forward
        press(wm::key::Enter);
        tics(5);
        fs::DirEntry saved;
        CHECK(storage.stat("C:\\Documents\\Doom\\doomsav0.dsg", saved) == ports::FsError::Ok && saved.size > 1000);

        // Ctrl+Alt+Del is the way out that is always there: the screen is
        // the desktop's again, the Task Manager opens, and the game waits.
        desktop.key(wm::key::LeftCtrl, true);
        desktop.key(wm::key::LeftAlt, true);
        desktop.key(wm::key::Delete, true);
        desktop.key(wm::key::Delete, false);
        desktop.key(wm::key::LeftAlt, false);
        desktop.key(wm::key::LeftCtrl, false);
        tics(3);
        CHECK(desktop.screen_lent_to() == nullptr);
        CHECK(display.pixels()[5 * 640 + 5] != 0); // the desktop's colour
        save(pictures, "doom-6-desktop");
        // The Task Manager away; Doom's window has a Play button now.
        if (strcmp(title(), "Doom") != 0 && desktop.active() != nullptr) {
            desktop.close(*desktop.active());
            wait(10000);
        }
        CHECK(strcmp(title(), "Doom") == 0);
        const uint32_t paused = screen_sum();
        tics(20);
        CHECK(screen_sum() == paused); // nothing runs while it waits
        desktop.key(wm::key::Enter, true); // Play, the default button
        desktop.key(wm::key::Enter, false);
        tics(3);
        CHECK(desktop.screen_lent_to() == desktop.active() && desktop.active() != nullptr);
        CHECK(colours() > 20);
        save(pictures, "doom-7-back");

        // The logo key does the same and puts the window away: its
        // taskbar button brings it back, and Play the game.
        {
            wm::Window *game = desktop.active();
            desktop.key(wm::key::LeftGui, true);
            desktop.key(wm::key::LeftGui, false);
            tics(3);
            CHECK(desktop.screen_lent_to() == nullptr && game->state() == wm::WindowState::Minimized);
            CHECK(!desktop.menu_open() && display.pixels()[5 * 640 + 5] == desktop_corner);
            save(pictures, "doom-7b-logo-key");
            desktop.restore(*game);
            tics(2);
            desktop.key(wm::key::Enter, true);
            desktop.key(wm::key::Enter, false);
            tics(3);
            CHECK(desktop.screen_lent_to() == game && colours() > 20);
        }

        // Leaving by the game's own menu: Escape, up to Quit Game, Enter, Y.
        press(wm::key::Escape);
        press(wm::key::Up);
        press(wm::key::Enter);
        save(pictures, "doom-8-quit");
        press(0x1C); // Y
        wait(200000);
        save(pictures, "doom-8b-left");
        wait(200000);
        CHECK(desktop.screen_lent_to() == nullptr);
        CHECK(loader.loaded == 0 && t.count() == processes && os::blocks_out() == blocks);
        CHECK(display.pixels()[5 * 640 + 5] == desktop_corner); // the desktop again

        // Once more, and closed from outside in the middle of the game
        // (End Task): everything goes all the same.
        CHECK(shell::run_file("C:\\Programs\\doom.app", why, sizeof(why)));
        wait(10000);
        wait(200000);
        tics(20);
        CHECK(desktop.screen_lent_to() != nullptr);
        CHECK(os::signal(t.find_by_name("Doom"), proc::Signal::Kill) == proc::Sent::Ok);
        wait(10000);
        wait(200000);
        CHECK(desktop.screen_lent_to() == nullptr);
        CHECK(loader.loaded == 0 && t.count() == processes && os::blocks_out() == blocks);
        save(pictures, "doom-9-ended");
        CHECK(display.pixels()[5 * 640 + 5] == desktop_corner);

        // On a screen that has the coarse picture (the board's 320x240,
        // which the monitor's signal makes large) the game takes it and
        // draws a pixel for a pixel, 320x200 in the middle of it.
        display.offer_coarse();
        CHECK(shell::run_file("C:\\Programs\\doom.app", why, sizeof(why)));
        wait(10000);
        wait(200000);
        tics(40);
        CHECK(desktop.screen_lent_to() != nullptr && display.coarse());
        CHECK(display.width() == 320 && display.height() == 240);
        CHECK(desktop.screen_width() == 640 && desktop.screen_height() == 480); // the desktop keeps its size
        {
            bool seen[256] = {};
            int count = 0, outside = 0;
            const uint8_t *p = display.pixels();
            for (int y = 0; y < 240; y++) {
                for (int x = 0; x < 320; x++) {
                    const uint8_t c = p[y * 320 + x];
                    if (y < 20 || y >= 220) {
                        outside += c != 0;
                    } else if (!seen[c]) {
                        seen[c] = true;
                        count++;
                    }
                }
            }
            CHECK(count > 20 && outside == 0); // the title picture, black above and below
        }
        save(pictures, "doom-10-coarse");
        // F12 goes round the looks: 640x240, the screen as it is with
        // every pixel made four, and round again.
        const auto f12 = [] {
            desktop.key(0x45, true);
            desktop.key(0x45, false);
            wait(10000);
            tics(3);
        };
        f12();
        CHECK(desktop.screen_lent_to() != nullptr && display.coarse() && display.width() == 640 &&
              display.height() == 240);
        CHECK(display.pixels()[30 * 640 + 300] != 0 || display.pixels()[120 * 640 + 300] != 0);
        save(pictures, "doom-10b2-tall");
        f12();
        CHECK(desktop.screen_lent_to() != nullptr && !display.coarse() && display.width() == 640);
        CHECK(colours() > 20 && display.pixels()[5 * 640 + 5] == 0);
        save(pictures, "doom-10c-fourfold");
        f12();
        CHECK(desktop.screen_lent_to() != nullptr && display.coarse() && display.width() == 320);
        // The look is remembered: left at 640x240, closed, started again.
        f12();
        CHECK(display.coarse() && display.width() == 640);
        CHECK(os::signal(t.find_by_name("Doom"), proc::Signal::Term) == proc::Sent::Ok);
        wait(10000);
        wait(200000);
        CHECK(desktop.screen_lent_to() == nullptr && t.count() == processes);
        CHECK(shell::run_file("C:\\Programs\\doom.app", why, sizeof(why)));
        wait(10000);
        wait(200000);
        tics(10);
        CHECK(desktop.screen_lent_to() != nullptr && display.coarse() && display.width() == 640);
        f12();
        f12();
        CHECK(display.coarse() && display.width() == 320);
        // The logo key: the desktop is back at its own size, painted.
        desktop.key(wm::key::LeftGui, true);
        desktop.key(wm::key::LeftGui, false);
        wait(10000);
        wait(200000);
        CHECK(desktop.screen_lent_to() == nullptr && !display.coarse() && display.width() == 640);
        CHECK(display.pixels()[5 * 640 + 5] == desktop_corner);
        save(pictures, "doom-11-desktop-after-coarse");
        CHECK(os::signal(t.find_by_name("Doom"), proc::Signal::Kill) == proc::Sent::Ok);
        wait(10000);
        wait(200000);
        CHECK(loader.loaded == 0 && t.count() == processes && os::blocks_out() == blocks);
    }
    if (failures != 0 || !pictures.empty()) {
        print_log();
    }
    printf(failures == 0 ? "doom: all checks passed\n" : "doom: %d checks FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
