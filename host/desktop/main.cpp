// Chinchilla as a desktop program for Linux: the same core/ the board runs,
// in a window. The window is the monitor, the PC's mouse and keyboard are
// the board's, and a folder of the PC is drive C:.
//
//   chinchilla [--drive FOLDER] [--fast] [--scale N] [--fullscreen]
//
// It is not an emulator of the ESP32: nothing here is the chip, its memory
// limits or its speed. This file is to the PC what firmware/os/main/main.cpp
// is to the board: the adapters behind core/ports.hpp, and the loop.
//
// The same file is the browser version (backlog item 33, built by
// web/CMakeLists.txt with Emscripten, which brings its own SDL2). What
// differs there is marked __EMSCRIPTEN__: the page's canvas is the window,
// C: is kept in the browser's own storage, the browser calls step() once a
// frame, and Restart loads the page again.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "core/init/init.hpp"
#include "core/os/tcp.hpp"
#include "core/fs/fs.hpp"
#include "core/fs/samples.hpp"
#include "core/gfx/art.hpp"
#include "core/shell/boot.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"

#include "../folder_volume.hpp"
#include "../host_adapters.hpp"
#include "screen.hpp"
#ifdef __EMSCRIPTEN__
#include "web_dialer.hpp"
#else
#include "core/net/posix_dialer.hpp"
#endif

namespace {

constexpr int kWidth = 640, kHeight = 480;
constexpr uint32_t kFrameMs = 16; // the window is redrawn at most this often
constexpr uint32_t kForceCloseMs = 3000;

#ifdef __EMSCRIPTEN__
constexpr bool kWeb = true;
// Where C: is in the page's own file system. The browser keeps it under
// this name too (IndexedDB), so it is not a name another page would pick.
constexpr const char *kWebDrive = "/chinchilla-c";

// Makes the folder, ties it to the browser's storage and waits until what
// an earlier visit left there has been read back. From then on every change
// is written back by itself a moment later (autoPersist).
EM_ASYNC_JS(int, web_mount_drive, (const char *path), {
    const where = UTF8ToString(path);
    try {
        FS.mkdir(where);
        FS.mount(IDBFS, {autoPersist: true}, where);
        await new Promise((done, failed) => FS.syncfs(true, (error) => (error ? failed(error) : done())));
        return 1;
    } catch (error) {
        console.error('Chinchilla: C: cannot be kept in this browser', error);
        return 0; // private windows of some browsers: C: lasts until the page closes
    }
});

// The size the canvas has on the page, in the screen's real pixels.
EM_JS(int, web_canvas_width, (), {
    return Math.max(1, Math.round(Module.canvas.clientWidth * (window.devicePixelRatio || 1)));
});
EM_JS(int, web_canvas_height, (), {
    return Math.max(1, Math.round(Module.canvas.clientHeight * (window.devicePixelRatio || 1)));
});
#else
constexpr bool kWeb = false;
#endif

// The pointer is not in the picture: it is mixed in as the picture goes to
// the window, as on the board. This one also says when it has changed.
class WindowPointer : public host::MemoryPointer {
public:
    void set_position(int x, int y) override
    {
        MemoryPointer::set_position(x, y);
        changed = true;
    }
    void set_shape(const video::PointerShape &shape) override
    {
        MemoryPointer::set_shape(shape);
        changed = true;
    }
    void set_visible(bool visible) override
    {
        MemoryPointer::set_visible(visible);
        changed = true;
    }
    bool changed = true;
};

// What the start-up self-test says about the machine.
class PcSystem : public host::HostSystem {
public:
    bool fact(ports::Fact which, uint32_t &value, char *text, size_t capacity) override
    {
        switch (which) {
        case ports::Fact::Processor:
            value = 0;
            snprintf(text, capacity, "this PC (not an ESP32)");
            return true;
        case ports::Fact::Input:
            value = 0;
            snprintf(text, capacity, "the PC's keyboard and mouse");
            return true;
        default:
            return HostSystem::fact(which, value, text, capacity);
        }
    }
};

struct Options {
    std::string drive;
    bool fast = false;
    bool fullscreen = false;
    int scale = 0;            // 0: the largest whole size that fits the desktop
    std::string screenshot;   // tests: save the window here ...
    uint32_t exit_after = 0;  // ... and leave after this many ms
};

class Machine {
public:
    Machine(const Options &options, char **argv)
        : options_(options), argv_(argv), volume_(options.drive, kWeb ? "in the browser" : "PC folder")
    {
    }

    bool open_window();
    void start();
    bool step(); // one round of the loop; false: time to leave
    void close_window();

private:
    void handle(const SDL_Event &e);
    void key(const SDL_KeyboardEvent &e);
    void mouse_at(int window_x, int window_y, int wheel);
    void release_keys();
    void close_asked();
    void draw();
    void present();
    void save_window(const char *path);
    void restart();
    static void boot_idle(void *self);

    Options options_;
    char **argv_;

    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *frame_ = nullptr; // 640x480, the screen
    SDL_Texture *large_ = nullptr; // the screen a whole number of times larger
    int large_scale_ = 0;
    desktop::Fit fit_ = {0, 0, kWidth, kHeight};
    std::vector<uint32_t> rgb_ = std::vector<uint32_t>(kWidth * kHeight);
    uint32_t presented_ms_ = 0;
    bool dirty_ = true;
    bool running_ = true;

    host::MemoryDisplay display_{kWidth, kHeight};
    WindowPointer pointer_;
    host::SteadyClock clock_;
    host::HostMemory memory_;
    PcSystem system_;
    wm::Wm screen_{display_, pointer_, clock_};
    fs::Storage storage_;
    host::FolderVolume volume_;

    bool booting_ = true;   // the start-up screens: input only skips them
    bool skip_boot_ = false;
    uint8_t buttons_ = 0;
    bool down_[256] = {};   // keys held, by USB usage code
    uint32_t refused_ms_ = 0; // when a program last kept the window from closing
    bool refused_ = false;
    int shots_ = 0;
    uint32_t started_ms_ = 0;
};

bool Machine::open_window()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "chinchilla: no display: %s\n", SDL_GetError());
        return false;
    }
    int scale = options_.scale;
    if (scale <= 0 && !kWeb) {
        // The largest whole size, up to three times, that leaves room for
        // the PC's own panels and the window's frame.
        SDL_Rect usable = {0, 0, kWidth, kHeight};
        SDL_GetDisplayUsableBounds(0, &usable);
        scale = 1;
        while (scale < 3 && kWidth * (scale + 1) <= usable.w * 9 / 10 && kHeight * (scale + 1) <= usable.h * 9 / 10) {
            scale++;
        }
    }
    int width = kWidth * scale, height = kHeight * scale;
    uint32_t flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (options_.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
#ifdef __EMSCRIPTEN__
    // The page lays the canvas out (web/shell.html); the window is as many
    // real pixels as that takes, and follows it (step()).
    flags = SDL_WINDOW_RESIZABLE;
    width = web_canvas_width();
    height = web_canvas_height();
#endif
    window_ = SDL_CreateWindow("Chinchilla", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
    if (window_ == nullptr) {
        fprintf(stderr, "chinchilla: no window: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window_, kWidth / 2, kHeight / 2);
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer_ == nullptr) {
        renderer_ = SDL_CreateRenderer(window_, -1, 0); // drawn by the processor
    }
    if (renderer_ == nullptr) {
        fprintf(stderr, "chinchilla: cannot draw in the window: %s\n", SDL_GetError());
        return false;
    }
    frame_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
    if (frame_ == nullptr) {
        fprintf(stderr, "chinchilla: cannot draw in the window: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(frame_, SDL_ScaleModeNearest);

    // The window's icon: the mascot, from the core's own art.
    const gfx::Bitmap &mascot = gfx::mascot_32();
    std::vector<uint32_t> icon(static_cast<size_t>(mascot.width) * mascot.height);
    for (size_t i = 0; i < icon.size(); i++) {
        icon[i] = mascot.pixels[i] == mascot.transparent ? 0 : desktop::colour(mascot.pixels[i]);
    }
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(icon.data(), mascot.width, mascot.height, 32,
                                                              mascot.width * 4, SDL_PIXELFORMAT_ARGB8888);
    if (surface != nullptr) {
        SDL_SetWindowIcon(window_, surface);
        SDL_FreeSurface(surface);
    }
    // Chinchilla draws its own pointer (the arrow, the hourglass, the
    // resize arrows); the PC's is hidden while it is over the window.
    SDL_ShowCursor(SDL_DISABLE);
    started_ms_ = SDL_GetTicks();
    return true;
}

void Machine::close_window()
{
    SDL_DestroyTexture(large_);
    SDL_DestroyTexture(frame_);
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
}

// Draws the screen into the window's back buffer: first a whole number of
// times larger with hard pixel edges, then smoothed down to the size that
// fits. At 1x, 2x, 3x every pixel is a sharp square; in between, the pixels
// keep even sizes instead of some columns coming out wider than others.
void Machine::draw()
{
    desktop::to_rgb(display_.pixels(), kWidth, kHeight, pointer_.state(), rgb_.data());
    SDL_UpdateTexture(frame_, nullptr, rgb_.data(), kWidth * 4);
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer_, &w, &h);
    fit_ = desktop::fit(w, h, kWidth, kHeight);
    const int scale = desktop::whole_scale(fit_, kWidth, kHeight);
    if (scale != large_scale_) {
        SDL_DestroyTexture(large_);
        large_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, kWidth * scale,
                                   kHeight * scale);
        if (large_ != nullptr) {
            SDL_SetTextureScaleMode(large_, SDL_ScaleModeLinear);
        }
        large_scale_ = scale;
    }
    SDL_Texture *source = frame_; // if the larger copy cannot be made: hard edges only
    if (large_ != nullptr && SDL_SetRenderTarget(renderer_, large_) == 0) {
        SDL_RenderCopy(renderer_, frame_, nullptr, nullptr);
        SDL_SetRenderTarget(renderer_, nullptr);
        source = large_;
    }
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    const SDL_Rect where = {fit_.x, fit_.y, fit_.w, fit_.h};
    if (fit_.w > 0 && fit_.h > 0) {
        SDL_RenderCopy(renderer_, source, nullptr, &where);
    }
}

void Machine::present()
{
    draw();
    SDL_RenderPresent(renderer_);
    presented_ms_ = SDL_GetTicks();
    dirty_ = false;
    pointer_.changed = false;
}

// The window as it is shown, as a BMP file (which Chinchilla's own Picture
// viewer opens).
void Machine::save_window(const char *path)
{
    draw();
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer_, &w, &h);
    SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormat(0, w, h, 24, SDL_PIXELFORMAT_BGR24);
    if (shot != nullptr && SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_BGR24, shot->pixels, shot->pitch) == 0 &&
        SDL_SaveBMP(shot, path) == 0) {
        fprintf(stderr, "chinchilla: window saved as %s\n", path);
    } else {
        fprintf(stderr, "chinchilla: could not save %s: %s\n", path, SDL_GetError());
    }
    SDL_FreeSurface(shot);
    dirty_ = true; // the back buffer was drawn but not shown
}

void Machine::release_keys()
{
    for (int usage = 0; usage < 256; usage++) {
        if (down_[usage]) {
            down_[usage] = false;
            screen_.key(static_cast<uint16_t>(usage), false);
        }
    }
}

void Machine::key(const SDL_KeyboardEvent &e)
{
    const bool pressed = e.state == SDL_PRESSED;
    // Two keys belong to the window, not to Chinchilla.
    if (!kWeb && e.keysym.scancode == SDL_SCANCODE_F11) {
        if (pressed && e.repeat == 0) {
            const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
            SDL_SetWindowFullscreen(window_, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        return;
    }
    if (!kWeb && e.keysym.scancode == SDL_SCANCODE_F12) {
        if (pressed && e.repeat == 0) {
            // The first number not used yet in the current folder.
            char name[40];
            do {
                snprintf(name, sizeof(name), "chinchilla-%03d.bmp", ++shots_);
            } while (access(name, F_OK) == 0 && shots_ < 999);
            save_window(name);
        }
        return;
    }
    if (booting_) {
        skip_boot_ |= pressed;
        return;
    }
    // SDL numbers the keys as USB keyboards do (HID usage codes), which is
    // what the core takes: the key's place on the keyboard, not its letter.
    // The core turns keys into characters itself (a US layout for now) and
    // repeats a held key itself, so the PC's repeats are dropped.
    int usage = e.keysym.scancode;
    if (usage <= 0 || usage >= 256 || e.repeat != 0) {
        return;
    }
    // Linux keeps Ctrl+Alt+Del for itself. Ctrl+Alt+End stands in for it,
    // as in remote-desktop programs.
    if (usage == SDL_SCANCODE_END && (e.keysym.mod & KMOD_CTRL) != 0 && (e.keysym.mod & KMOD_ALT) != 0) {
        usage = SDL_SCANCODE_DELETE;
    }
    if (!pressed && usage == SDL_SCANCODE_END && down_[SDL_SCANCODE_DELETE] && !down_[SDL_SCANCODE_END]) {
        usage = SDL_SCANCODE_DELETE; // the release of that stand-in
    }
    if (down_[usage] == pressed) {
        return;
    }
    down_[usage] = pressed;
    screen_.key(static_cast<uint16_t>(usage), pressed);
}

void Machine::mouse_at(int window_x, int window_y, int wheel)
{
    // Mouse positions come in window units; the picture is laid out in the
    // renderer's pixels (they differ on high-density screens).
    int ww = 0, wh = 0, ow = 0, oh = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    SDL_GetRendererOutputSize(renderer_, &ow, &oh);
    if (ww > 0 && wh > 0) {
        window_x = window_x * ow / ww;
        window_y = window_y * oh / wh;
    }
    int x = 0, y = 0;
    desktop::to_screen(desktop::fit(ow, oh, kWidth, kHeight), window_x, window_y, kWidth, kHeight, x, y);
    if (booting_) {
        skip_boot_ |= buttons_ != 0;
        return;
    }
    wheel = wheel > 127 ? 127 : (wheel < -127 ? -127 : wheel);
    screen_.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons_, static_cast<int8_t>(wheel)});
}

// The window's close button (or the PC's Alt+F4) is Shut Down: every program
// is asked to close, so the Editor can still ask about unsaved text. If one
// does, the window stays; closing it again within a few seconds leaves
// anyway.
void Machine::close_asked()
{
    const uint32_t now = SDL_GetTicks();
    if (booting_ || shell::powered_down() || (refused_ && now - refused_ms_ < kForceCloseMs)) {
        running_ = false;
        return;
    }
    release_keys();
    if (shell::power_down()) {
        running_ = false;
        return;
    }
    screen_.tick();
    refused_ = true;
    refused_ms_ = now;
    fprintf(stderr, "chinchilla: a program has a question open. Answer it, or close the window again to leave now.\n");
}

void Machine::handle(const SDL_Event &e)
{
    switch (e.type) {
    case SDL_QUIT:
        if (!kWeb) { // a page that closes asks nobody
            close_asked();
        }
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        key(e.key);
        break;
    case SDL_MOUSEMOTION:
        mouse_at(e.motion.x, e.motion.y, 0);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        const uint8_t bit = e.button.button == SDL_BUTTON_LEFT     ? 1
                            : e.button.button == SDL_BUTTON_RIGHT  ? 2
                            : e.button.button == SDL_BUTTON_MIDDLE ? 4
                                                                   : 0;
        buttons_ = e.type == SDL_MOUSEBUTTONDOWN ? (buttons_ | bit) : (buttons_ & ~bit);
        // A drag that leaves the window keeps following the mouse.
        SDL_CaptureMouse(buttons_ != 0 ? SDL_TRUE : SDL_FALSE);
        mouse_at(e.button.x, e.button.y, 0);
        break;
    }
    case SDL_MOUSEWHEEL: {
        int x = 0, y = 0;
        SDL_GetMouseState(&x, &y);
        mouse_at(x, y, e.wheel.y);
        break;
    }
    case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && !booting_) {
            // Keys let go while another window has the keyboard never
            // arrive, and Alt or Ctrl would stay held.
            release_keys();
        }
        dirty_ = true;
        break;
    default:
        break;
    }
}

// While the start-up screens show: keep the window alive. They draw straight
// to the screen and wait by calling this over and over.
void Machine::boot_idle(void *self)
{
    Machine &m = *static_cast<Machine *>(self);
    SDL_Event e;
    while (SDL_PollEvent(&e) != 0) {
        m.handle(e);
    }
    if (!m.running_) {
        m.close_window();
        exit(0);
    }
    if (SDL_GetTicks() - m.presented_ms_ >= kFrameMs) {
        m.present();
    }
#ifdef __EMSCRIPTEN__
    // Hands the processor back to the browser, which cannot show a picture
    // or deliver a key while the program keeps it (this is what Asyncify
    // is built in for).
    emscripten_sleep(4);
#else
    SDL_Delay(1);
#endif
}

void Machine::start()
{
    // C: is the folder. A new one gets the sample files, as a newly
    // formatted flash does on the board.
    storage_.mount('C', volume_);
    ports::DirEntry first;
    if (storage_.entry("C:\\", 0, first) == ports::FsError::NotFound) {
        fs::add_samples(storage_, 'C');
    }

    // The start-up screens, as on the board, with the settings of
    // C:\.config\boot.ini; a key or a click skips them. There is no
    // monitor to wait for.
    shell::BootSettings boot = shell::boot_settings(storage_);
    static shell::BootIo io{display_, system_, memory_, clock_, &storage_, &Machine::boot_idle, this, nullptr};
    io.video_start_us = clock_.micros();
    io.monitor_wait_ms = 0;
    io.splash_ms = boot.splash_ms;
    io.pace_percent = boot.pace_percent;
    io.hold_s = boot.hold_s;
    io.skip = [](void *self) { return static_cast<Machine *>(self)->skip_boot_; };
    pointer_.set_visible(false);
    if (boot.fastboot || options_.fast) {
        shell::run_quiet_checks(io);
    } else {
        shell::run_self_test(io);
        shell::show_splash(io);
    }
    booting_ = false;
    buttons_ = 0;
    pointer_.set_visible(true);

    // No Wi-Fi: the Network panel says it is not available. The clock is
    // the PC's.
    // Programs reach the internet all the same (os::tcp): through the PC's
    // own sockets, or in a web page through the relay (web_dialer.hpp).
#ifdef __EMSCRIPTEN__
    static host::WebDialer dialer;
#else
    static net::PosixDialer dialer;
#endif
    os::install_dialer(&dialer);
    // init is PID 1 and starts the desktop from C:\.config\startup.txt, as
    // on the board.
    static init::Init init_process(proc::table());
    init_process.set_run(
        [](const char *what, char *why, size_t capacity, void *) { return shell::run_text(what, "C:\\", why, capacity); },
        nullptr);
    init_process.add(
        "desktop",
        [](void *self) {
            auto *m = static_cast<Machine *>(self);
            shell::start(m->screen_, m->system_, m->storage_, m->memory_);
            return true;
        },
        this, true);
    // The start-up file is the board's, and it starts inetd, the network
    // services. There is no network here, so this inetd serves nothing
    // (the owner's choice, 2026-11-17): the line succeeds, and the process
    // list shows an inetd that End Task or kill ends at once.
    init_process.add(
        "inetd",
        [](void *) {
            const proc::Pid pid = proc::table().spawn_service("inetd");
            if (pid == proc::kNone) {
                return false;
            }
            proc::table().set_stop(
                pid, [](proc::Pid row, proc::Signal, void *) { proc::table().exit(row); }, nullptr);
            return true;
        },
        nullptr);
    shell::startup_trouble(init_process.boot(storage_));
    screen_.set_damage_listener([](const gfx::Rect &, void *self) { static_cast<Machine *>(self)->dirty_ = true; },
                                this);
    const time_t now = time(nullptr);
    tm local = {};
    localtime_r(&now, &local);
    screen_.set_time(local.tm_hour, local.tm_min, local.tm_sec);
    screen_.start();
    dirty_ = true;
}

// Start > Shut Down > Restart: the program starts again from the top, as
// the board does.
void Machine::restart()
{
#ifdef __EMSCRIPTEN__
    (void)argv_;
    emscripten_cancel_main_loop();
    EM_ASM({ setTimeout(() => location.reload(), 300); }); // after C: has been written back
#else
    close_window();
    execv("/proc/self/exe", argv_);
    perror("chinchilla: restart");
    exit(1);
#endif
}

// One round, as the board's UI loop: sleep until there is input, the
// desktop's next timer is due or the window needs drawing; take the input;
// let the desktop run its timers and paint; show the result.
bool Machine::step()
{
#ifdef __EMSCRIPTEN__
    // The browser calls this once for every frame it shows, so nothing
    // here waits. The window follows the canvas when the page changes size.
    int have_w = 0, have_h = 0;
    SDL_GetWindowSize(window_, &have_w, &have_h);
    const int want_w = web_canvas_width(), want_h = web_canvas_height();
    if (want_w != have_w || want_h != have_h) {
        SDL_SetWindowSize(window_, want_w, want_h);
        dirty_ = true;
    }
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        handle(event);
    }
    screen_.tick();
    if (system_.reboot_requested) {
        system_.reboot_requested = false;
        restart();
        return false;
    }
    if (dirty_ || pointer_.changed) {
        present();
    }
    return true;
#endif
    uint32_t wait_ms = screen_.idle_us() / 1000;
    wait_ms = wait_ms > 1000 ? 1000 : wait_ms;
    if (dirty_ || pointer_.changed) {
        const uint32_t since = SDL_GetTicks() - presented_ms_;
        const uint32_t until_frame = since >= kFrameMs ? 0 : kFrameMs - since;
        wait_ms = until_frame < wait_ms ? until_frame : wait_ms;
    }
    if (options_.exit_after != 0) {
        wait_ms = wait_ms > 10 ? 10 : wait_ms;
    }
    SDL_Event e;
    if (wait_ms == 0 ? SDL_PollEvent(&e) != 0 : SDL_WaitEventTimeout(&e, static_cast<int>(wait_ms)) != 0) {
        do {
            handle(e);
        } while (SDL_PollEvent(&e) != 0);
    }
    screen_.tick();
    if (system_.reboot_requested) {
        restart();
    }
    if ((dirty_ || pointer_.changed) && SDL_GetTicks() - presented_ms_ >= kFrameMs) {
        present();
    }
    if (options_.exit_after != 0 && SDL_GetTicks() - started_ms_ >= options_.exit_after) {
        if (!options_.screenshot.empty()) {
            save_window(options_.screenshot.c_str());
        }
        return false;
    }
    return running_;
}

// The folder that is C: when none is given: where Linux programs keep their
// own files.
std::string default_drive()
{
    const char *data = getenv("XDG_DATA_HOME");
    if (data != nullptr && data[0] == '/') {
        return std::string(data) + "/chinchilla/C";
    }
    const char *home = getenv("HOME");
    return std::string(home != nullptr ? home : ".") + "/.local/share/chinchilla/C";
}

bool make_folders(const std::string &path)
{
    for (size_t i = 1; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            if (mkdir(path.substr(0, i).c_str(), 0755) != 0 && errno != EEXIST) {
                return false;
            }
        }
    }
    struct stat s;
    return stat(path.c_str(), &s) == 0 && S_ISDIR(s.st_mode);
}

void usage()
{
    printf("Chinchilla %s as a desktop program.\n\n"
           "  chinchilla [options]\n\n"
           "  --drive FOLDER   the folder of this PC that is drive C:\n"
           "                   (default: %s)\n"
           "  --fast           no start-up screens\n"
           "  --scale N        open the window N times 640x480 (default: what fits)\n"
           "  --fullscreen     start on the whole screen\n"
           "  --help           this text\n\n"
           "In the window: F11 whole screen and back, F12 saves a picture of the\n"
           "window in the current folder, Ctrl+Alt+End is Ctrl+Alt+Del, Ctrl+Esc\n"
           "opens the Start menu. Closing the window shuts Chinchilla down.\n",
           shell::kVersion, default_drive().c_str());
}

} // namespace

int main(int argc, char **argv)
{
    Options options;
#ifdef __EMSCRIPTEN__
    options.drive = std::string(kWebDrive) + "/C";
    web_mount_drive(kWebDrive);
    argc = 1; // the page has no command line
#endif
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        const bool more = i + 1 < argc;
        if (arg == "--drive" && more) {
            options.drive = argv[++i];
        } else if (arg == "--fast") {
            options.fast = true;
        } else if (arg == "--fullscreen") {
            options.fullscreen = true;
        } else if (arg == "--scale" && more) {
            options.scale = atoi(argv[++i]);
        } else if (arg == "--screenshot" && more) { // for the tests
            options.screenshot = argv[++i];
        } else if (arg == "--exit-after" && more) { // for the tests, in ms
            options.exit_after = static_cast<uint32_t>(atoi(argv[++i]));
        } else {
            usage();
            return arg == "--help" || arg == "-h" ? 0 : 2;
        }
    }
    if (options.drive.empty()) {
        options.drive = default_drive();
    }
    if (!make_folders(options.drive)) {
        fprintf(stderr, "chinchilla: %s cannot be used as drive C: (%s)\n", options.drive.c_str(), strerror(errno));
        return 1;
    }
    static Machine machine(options, argv);
    if (!machine.open_window()) {
        return 1;
    }
    machine.start();
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg([](void *m) { static_cast<Machine *>(m)->step(); }, &machine, 0, true);
#endif
    while (machine.step()) {
    }
    machine.close_window();
    return 0;
}
