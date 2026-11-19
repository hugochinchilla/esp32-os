// Doom on Chinchilla (backlog item 81): the program.
//
// The game is id Software's engine of 1993 as doomgeneric has it
// (third_party/doomgeneric, GPL 2), built into this one program file with
// port.c, which stands in for the C library and for the machine. This file
// is the Chinchilla side: a small window, the whole screen borrowed from the
// system (os::ui::take_screen), the timer that runs the engine, and the OS
// calls behind what the engine asks (files, memory, the clock).
//
// The game's data is not part of it: a WAD file beside the program, the
// shareware DOOM1.WAD or the player's own.
//
// The engine is under the GNU General Public License, version 2, and so is
// this program as a whole (third_party/doomgeneric/LICENSE).
// docs/journal/2026-11-19-doom.md has the story.
#include <cstdio>
#include <cstring>

#include "core/app/app.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"

#define PORT_ITSELF 1
#include "port.h"

namespace {

constexpr int kDoomW = 320, kDoomH = 200;
// A tic of the game is 1/35 s: the engine is called that often, and makes a
// picture each time if the machine keeps up.
constexpr uint32_t kTicUs = 1000000 / 35;
constexpr uint32_t kReportUs = 10 * 1000000; // how often the log gets a line of figures

// Where the game's data is looked for, in this order.
constexpr const char *kWads[] = {
    "C:\\Programs\\DOOM1.WAD", "C:\\Programs\\DOOM.WAD",  "C:\\Programs\\DOOM2.WAD",
    "C:\\Programs\\FREEDOOM1.WAD", "C:\\Programs\\FREEDOOM2.WAD",
};

// Its picture: a horned face, red on black. Drawn for this project.
constexpr const char *kSmall[16] = {
    "................", ".W............W.", ".WW..........WW.", ".KWW.KKKKKK.WWK.",
    "..KWKRRRRRRKWK..", "..KRRRRRRRRRRK..", ".KRRYYRRRRYYRRK.", ".KRRKYRRRRYKRRK.",
    ".KRRRRRRRRRRRRK.", ".KRRRRRDDRRRRRK.", ".KRRRRRRRRRRRRK.", "..KRWKWKKWKWRK..",
    "..KRKWKWWKWKRK..", "...KRRRRRRRRK...", "....KKKKKKKK....", "................",
};
constexpr app::Ink kInks[] = {
    {'K', gfx::kBlack},
    {'W', gfx::kWhite},
    {'R', gfx::rgb(6, 0, 0)},
    {'D', gfx::rgb(3, 0, 0)},
    {'Y', gfx::rgb(7, 6, 0)},
};

class DoomWindow;
// The one window, for the engine's calls, which carry no context. Set when
// the window is made and cleared when it has closed.
DoomWindow *g_window = nullptr;

class DoomWindow : public app::Window {
public:
    static constexpr int kClientW = 300, kClientH = 96;

    explicit DoomWindow(const char *wad)
        : line1_(label({12, 12, kClientW - 24, 14}, wad != nullptr ? "Starting Doom..." : "Doom has no game data.")),
          line2_(label({12, 30, kClientW - 24, 14}, wad != nullptr ? "" : "Copy DOOM1.WAD to C:\\Programs")),
          line3_(label({12, 46, kClientW - 24, 14}, wad != nullptr ? "" : "and start Doom again.")),
          play_(button({kClientW - 12 - 2 * 90 - 8, kClientH - 12 - 23, 90, 23}, "&Play")),
          quit_(button({kClientW - 12 - 90, kClientH - 12 - 23, 90, 23}, "&Quit"))
    {
        g_window = this;
        set_default(wad != nullptr ? play_ : quit_);
        play_.set_enabled(false);
        if (wad != nullptr) {
            snprintf(wad_, sizeof(wad_), "%s", wad);
            // Once the window is on the screen: the engine takes seconds to start.
            timer_ = os::start_timer(100000, 0, &DoomWindow::start, this);
        }
    }

    void command(app::Control which) override
    {
        if (which == quit_) {
            close();
        } else if (which == play_ && started_ && !playing_) {
            play();
        }
    }

    // While the game has the screen every key comes here, as it goes down
    // and as it comes up.
    bool key(const wm::KeyMsg &k) override
    {
        if (!playing_) {
            return false;
        }
        port_key(k.usage, k.pressed);
        return true;
    }

    void mouse_moved(int dx, int dy, int buttons) override
    {
        if (playing_) {
            port_mouse(dx, dy, buttons);
        }
    }

    void closed() override
    {
        stop();
        give_screen_back();
        // All the engine had, wherever it was.
        port_free_all();
        for (int &file : files_) {
            if (file != 0) {
                os::file::close(file);
                file = 0;
            }
        }
        g_window = nullptr;
    }

    // The engine's picture onto the screen: each of its pixels becomes two
    // by two, 640 by 400 in the middle of the screen. The board shows 8
    // levels of red and green and 4 of blue, far fewer than the game's
    // palette asks for, so the four pixels of each square take levels on
    // either side of the colour wanted (an ordered dither that costs
    // nothing: four tables instead of one).
    void picture(const uint8_t *pixels, const uint8_t *palette, bool palette_changed)
    {
        if (!playing_ || screen_.pixels == nullptr) {
            return;
        }
        const uint32_t began = static_cast<uint32_t>(os::now_us());
        if (palette_changed) {
            // How far towards the next level each of the four places leans.
            static constexpr int kLean[4] = {32, 159, 223, 96};
            for (int place = 0; place < 4; place++) {
                for (int i = 0; i < 256; i++) {
                    const int r = (palette[i * 3] * 7 + kLean[place]) / 255;
                    const int g = (palette[i * 3 + 1] * 7 + kLean[place]) / 255;
                    const int b = (palette[i * 3 + 2] * 3 + kLean[place]) / 255;
                    table_[place][i] = gfx::rgb(r, g, b);
                }
            }
        }
        // A line is made in memory that is fast to write and copied out
        // whole: the screen's own memory is slow to write a byte at a time.
        uint8_t upper[kDoomW * 2], lower[kDoomW * 2];
        uint8_t *out = screen_.pixels + top_ * screen_.width + left_;
        if (scale_ == 1) {
            // The coarse picture: a pixel for a pixel, and the same four
            // tables laid over squares of four of the game's own pixels.
            for (int y = 0; y < kDoomH; y++) {
                const uint8_t *in = pixels + y * kDoomW;
                const uint8_t *even = table_[(y & 1) * 2], *odd = table_[(y & 1) * 2 + 1];
                for (int x = 0; x < kDoomW; x += 2) {
                    upper[x] = even[in[x]];
                    upper[x + 1] = odd[in[x + 1]];
                }
                memcpy(out, upper, kDoomW);
                out += screen_.width;
            }
            screen_changed({left_, top_, kDoomW, kDoomH});
            pictures_++;
            picture_us_ += static_cast<uint32_t>(os::now_us()) - began;
            return;
        }
        for (int y = 0; y < kDoomH; y++) {
            const uint8_t *in = pixels + y * kDoomW;
            for (int x = 0; x < kDoomW; x++) {
                const uint8_t c = in[x];
                upper[2 * x] = table_[0][c];
                upper[2 * x + 1] = table_[1][c];
                lower[2 * x] = table_[2][c];
                lower[2 * x + 1] = table_[3][c];
            }
            memcpy(out, upper, sizeof(upper));
            memcpy(out + screen_.width, lower, sizeof(lower));
            out += 2 * screen_.width;
        }
        screen_changed({left_, top_, kDoomW * 2, kDoomH * 2});
        pictures_++;
        picture_us_ += static_cast<uint32_t>(os::now_us()) - began;
    }

    // The engine's files, by the system's handles: two at most at a time,
    // as the system allows a program (the WAD, and a saved game or the
    // settings).
    int open(const char *path, bool write)
    {
        for (int &file : files_) {
            if (file == 0) {
                os::file::Handle handle = os::file::kNoFile;
                if (os::file::open(path, write ? ports::OpenMode::Write : ports::OpenMode::Read, handle) !=
                    ports::FsError::Ok) {
                    return 0;
                }
                file = handle;
                return handle;
            }
        }
        return 0;
    }
    void close_file(int handle)
    {
        for (int &file : files_) {
            if (file == handle && handle != 0) {
                os::file::close(handle);
                file = 0;
            }
        }
    }

private:
    static void start(void *self) { static_cast<DoomWindow *>(self)->start(); }
    static void tick(void *self) { static_cast<DoomWindow *>(self)->tick(); }

    void start()
    {
        timer_ = 0;
        // The screen first: the engine's start-up takes seconds, and its
        // first picture should not land on a desktop.
        started_ = true;
        if (!play()) {
            started_ = false;
            line1_.set_text("Doom cannot have the screen now.");
            return;
        }
        const uint32_t began = static_cast<uint32_t>(os::now_us());
        if (!port_start(wad_)) {
            leave();
            return;
        }
        char text[64];
        snprintf(text, sizeof(text), "doom: started in %u ms",
                 static_cast<unsigned>((static_cast<uint32_t>(os::now_us()) - began) / 1000));
        os::log(text);
        report_at_ = os::now_us() + kReportUs;
    }

    // Takes the screen and lets the engine run. False: the screen is not free.
    bool play()
    {
        // The coarse picture if this screen has one: 320x240 that the
        // screen makes large by itself, so that the engine's 320x200 go
        // out as they are. Otherwise this program makes each pixel four.
        if (!take_screen(screen_, true)) {
            return false;
        }
        playing_ = true;
        scale_ = screen_.width >= kDoomW * 2 && screen_.height >= kDoomH * 2 ? 2 : 1;
        left_ = (screen_.width - kDoomW * scale_) / 2;
        top_ = (screen_.height - kDoomH * scale_) / 2;
        memset(screen_.pixels, 0, static_cast<size_t>(screen_.width) * static_cast<size_t>(screen_.height));
        screen_changed({0, 0, screen_.width, screen_.height});
        port_show_again();
        line1_.set_text("Doom is paused.");
        line2_.set_text("Play goes back to the game.");
        line3_.set_text("In the game, the logo key comes back here.");
        if (timer_ == 0) {
            timer_ = os::start_timer(kTicUs, kTicUs, &DoomWindow::tick, this);
        }
        return true;
    }

    void stop()
    {
        if (timer_ != 0) {
            os::stop_timer(timer_);
            timer_ = 0;
        }
    }

    void tick()
    {
        if (!has_screen()) {
            // The system took the screen back (the logo key, Ctrl+Alt+Del,
            // or the screen changed its size): the game waits, with no key
            // held, behind the window's Play button.
            stop();
            playing_ = false;
            screen_ = {};
            port_release_keys();
            play_.set_enabled(true);
            return;
        }
        const uint32_t began = static_cast<uint32_t>(os::now_us());
        const bool goes_on = port_tick();
        const uint32_t took = static_cast<uint32_t>(os::now_us()) - began;
        tick_us_ += took;
        ticks_++;
        longest_us_ = took > longest_us_ ? took : longest_us_;
        late_ += took > kTicUs ? 1 : 0;
        if (!goes_on) {
            leave();
            return;
        }
        if (os::now_us() >= report_at_) {
            report();
        }
    }

    // What the last ten seconds were like, for whoever reads the log: how
    // many pictures a second out of how many rounds, how long a round took
    // (the longest, and how many took more than the 1/35 s there is), and
    // where the time of a round went: putting the picture on the screen,
    // waiting for the engine's clock, and the rest, which is the game and
    // its drawing.
    void report()
    {
        char text[200];
        const uint32_t n = pictures_ > 0 ? pictures_ : 1, t = ticks_ > 0 ? ticks_ : 1;
        const uint32_t seconds = kReportUs / 1000000;
        const uint32_t round = tick_us_ / t, screen = picture_us_ / t, waited = port_waited_us() / t;
        snprintf(text, sizeof(text),
                 "doom: %u.%u pictures/s in %u.%u rounds/s, a round %u us (longest %u, %u late): %u to the screen "
                 "(%u a picture), %u waiting, %u the game",
                 static_cast<unsigned>(pictures_ / seconds), static_cast<unsigned>(pictures_ * 10 / seconds % 10),
                 static_cast<unsigned>(ticks_ / seconds), static_cast<unsigned>(ticks_ * 10 / seconds % 10),
                 static_cast<unsigned>(round), static_cast<unsigned>(longest_us_), static_cast<unsigned>(late_),
                 static_cast<unsigned>(screen), static_cast<unsigned>(picture_us_ / n), static_cast<unsigned>(waited),
                 static_cast<unsigned>(round > screen + waited ? round - screen - waited : 0));
        os::log(text);
        pictures_ = ticks_ = tick_us_ = picture_us_ = longest_us_ = late_ = 0;
        report_at_ = os::now_us() + kReportUs;
    }

    // The engine has ended: the player quit, or it met an error. The
    // window goes, and with it everything (closed()).
    void leave()
    {
        stop();
        playing_ = false;
        give_screen_back();
        if (const char *error = port_error()) {
            shell::message_box("Doom", error);
        }
        close();
    }

    app::Label line1_, line2_, line3_;
    app::Button play_, quit_;
    char wad_[64] = "";
    int timer_ = 0;
    bool started_ = false; // the engine is in memory
    bool playing_ = false; // and has the screen
    os::ui::Screen screen_;
    int left_ = 0, top_ = 0;
    int scale_ = 2; // 1: the screen is the coarse picture, a pixel for a pixel
    uint8_t table_[4][256] = {};
    int files_[os::file::kMaxOpenEach] = {};
    uint32_t pictures_ = 0, ticks_ = 0, tick_us_ = 0, picture_us_ = 0, longest_us_ = 0, late_ = 0;
    uint64_t report_at_ = 0;
};

// A path as the system takes it. The engine writes '/' between folders,
// and keeps its saved games in a folder beside wherever it was started
// ("./.savegame/doomsav0.dsg"): here that is C:\Documents\Doom.
constexpr char kHome[] = "C:\\Documents\\Doom";

void system_path(const char *path, char *out, size_t capacity)
{
    if (path[0] != 0 && path[1] == ':') {
        snprintf(out, capacity, "%s", path);
    } else {
        if (path[0] == '.' && (path[1] == '/' || path[1] == 0)) {
            path += path[1] == '/' ? 2 : 1;
        }
        if (strncmp(path, ".savegame/", 10) == 0) {
            path += 10;
        }
        snprintf(out, capacity, path[0] != 0 ? "%s\\%s" : "%s", kHome, path);
    }
    for (char *c = out; *c != 0; c++) {
        if (*c == '/') {
            *c = '\\';
        }
    }
}

} // namespace

/* ---- What the engine asks (port.h) ---------------------------------------------- */

extern "C" {

void *sys_allocate(size_t bytes) { return os::allocate(bytes); }
void sys_release(void *block) { os::release(block); }

int sys_open(const char *path, int write)
{
    char name[128];
    system_path(path, name, sizeof(name));
    return g_window != nullptr ? g_window->open(name, write != 0) : 0;
}

long sys_read(int file, void *data, size_t size)
{
    size_t done = 0;
    return os::file::read(file, data, size, done) == ports::FsError::Ok ? static_cast<long>(done) : -1;
}

int sys_write(int file, const void *data, size_t size)
{
    return os::file::write(file, data, size) == ports::FsError::Ok;
}

int sys_seek(int file, uint32_t offset) { return os::file::seek(file, offset) == ports::FsError::Ok; }

void sys_close(int file)
{
    if (g_window != nullptr) {
        g_window->close_file(file);
    }
}

long sys_size(const char *path)
{
    char name[128];
    system_path(path, name, sizeof(name));
    fs::DirEntry entry;
    if (os::file::stat(name, entry) != ports::FsError::Ok || entry.folder) {
        return -1;
    }
    return static_cast<long>(entry.size);
}

int sys_remove(const char *path)
{
    char name[128];
    system_path(path, name, sizeof(name));
    return os::file::remove(name) == ports::FsError::Ok;
}

int sys_rename(const char *from, const char *to)
{
    char a[128], b[128];
    system_path(from, a, sizeof(a));
    system_path(to, b, sizeof(b));
    os::file::remove(b); // as the C library's: what was there goes
    return os::file::rename(a, b) == ports::FsError::Ok;
}

void sys_make_folder(const char *path)
{
    char name[128];
    system_path(path, name, sizeof(name));
    // Without the stroke the engine leaves at the end.
    const size_t length = strlen(name);
    if (length > 3 && name[length - 1] == '\\') {
        name[length - 1] = 0;
    }
    os::file::make_folder(name);
}

uint32_t sys_ms(void) { return static_cast<uint32_t>(os::now_us() / 1000); }
void sys_rest(uint32_t ms) { os::rest(ms); }
uint32_t sys_us() { return static_cast<uint32_t>(os::now_us()); }
void sys_log(const char *text) { os::log(text); }

void sys_picture(const uint8_t *pixels, const uint8_t *palette, int palette_changed)
{
    if (g_window != nullptr) {
        g_window->picture(pixels, palette, palette_changed != 0);
    }
}

} // extern "C"

// 16 KB of stack: the engine's deepest path, drawing a level, has been
// measured nowhere yet (the board will say).
APP_INFO(app::file_info("Doom", 16 * 1024, kSmall, kInks));

APP_MAIN(int, char **)
{
    const char *wad = nullptr;
    for (const char *candidate : kWads) {
        fs::DirEntry entry;
        if (os::file::stat(candidate, entry) == ports::FsError::Ok && !entry.folder) {
            wad = candidate;
            break;
        }
    }
    app::Look look;
    look.title = "Doom";
    look.width = DoomWindow::kClientW;
    look.height = DoomWindow::kClientH;
    look.flags = app::kDialog | app::kClientSize;
    return app::open<DoomWindow>(look, wad) != app::kNone ? 0 : 1;
}
