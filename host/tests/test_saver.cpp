// The screensavers: each animation leaves no pixels behind and draws the
// same frames from the same seed; the window manager starts one after the
// wait, hides the pointer, and gives the desktop back on input.
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/fs/memory_volume.hpp"
#include "core/saver/saver.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
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

constexpr int kW = 640, kH = 480;
using Bytes = std::vector<uint8_t>;

int lit(const Bytes &b)
{
    int n = 0;
    for (uint8_t v : b) {
        n += v != 0;
    }
    return n;
}

// Runs an animation in a buffer of exactly the screen's size (the address
// sanitizer catches a write outside it).
void test_animation(int index)
{
    saver::Animation &a = saver::animation(index);
    Bytes screen(kW * kH, 0), fresh(kW * kH, 0), first;
    const saver::Screen s = {screen.data(), kW, kH}, f = {fresh.data(), kW, kH};
    for (int run = 0; run < 2; run++) {
        std::fill(screen.begin(), screen.end(), 0);
        a.start(s, 12345);
        int most = 0;
        for (int frame = 1; frame <= 900; frame++) { // half a minute
            a.step(s);
            if (frame % 50 == 0 || frame < 20) {
                // What the steps left is the frame drawn from nothing.
                a.redraw(f);
                if (screen != fresh) {
                    printf("%s: stale pixels at frame %d\n", a.name(), frame);
                    failures++;
                    return;
                }
                most = lit(screen) > most ? lit(screen) : most;
                CHECK(lit(screen) > 50);
            }
        }
        // Mostly black: it is a screensaver.
        CHECK(most < kW * kH / 4);
        if (run == 0) {
            first = screen;
        } else {
            CHECK(screen == first); // the same seed gives the same frames
        }
    }
    a.start(s, 777);
    std::fill(screen.begin(), screen.end(), 0);
    a.start(s, 777);
    for (int frame = 0; frame < 900; frame++) {
        a.step(s);
    }
    CHECK(screen != first); // another seed, another run
}

// A remote screen hears about every pixel that changes, and not about the
// whole screen each frame.
void test_reports_changes()
{
    for (int i = 0; i < saver::kCount; i++) {
        Bytes screen(kW * kH, 0), told(kW * kH, 0);
        saver::Screen s = {screen.data(), kW, kH,
                           [](const gfx::Rect &r, void *context) {
                               auto &grid = *static_cast<Bytes *>(context);
                               for (int y = r.y; y < r.bottom(); y++) {
                                   for (int x = r.x; x < r.right(); x++) {
                                       if (x >= 0 && y >= 0 && x < kW && y < kH) {
                                           grid[y * kW + x] = 1;
                                       }
                                   }
                               }
                           },
                           &told};
        saver::animation(i).start(s, 4242);
        long reported = 0;
        for (int frame = 0; frame < 300; frame++) {
            const Bytes before = screen;
            std::fill(told.begin(), told.end(), 0);
            saver::animation(i).step(s);
            for (int p = 0; p < kW * kH; p++) {
                if (screen[p] != before[p] && !told[p]) {
                    printf("%s: frame %d changed a pixel it did not report\n", saver::animation(i).name(), frame);
                    failures++;
                    return;
                }
                reported += told[p];
            }
        }
        CHECK(reported / 300 < kW * kH / 8);
    }
}

// A small screen: nothing assumes 640x480.
void test_small_screen()
{
    for (int i = 0; i < saver::kCount; i++) {
        Bytes screen(200 * 120, 0);
        const saver::Screen s = {screen.data(), 200, 120};
        saver::animation(i).start(s, 99);
        for (int frame = 0; frame < 300; frame++) {
            saver::animation(i).step(s);
        }
        CHECK(lit(screen) > 0);
    }
}

/* ---- With the window manager ---------------------------------------------------- */

host::MemoryDisplay display(kW, kH);
host::MemoryPointer pointer;
host::FixedClock clock_;
wm::Wm desktop(display, pointer, clock_);

class Probe : public wm::Window {
public:
    Probe() : Window("Probe", {100, 100, 200, 120}, nullptr) {}
    bool key(const wm::KeyMsg &k) override
    {
        keys += k.pressed;
        return true;
    }
    void mouse(const wm::MouseMsg &m) override { presses += m.kind == wm::MouseKind::Down; }
    int keys = 0, presses = 0;
};

class CountingSaver : public wm::ScreenSaver {
public:
    void begin(uint8_t *pixels, int width, int height, uint32_t) override
    {
        begun++;
        memset(pixels, 0, static_cast<size_t>(width) * height);
    }
    void tick(uint64_t) override { ticks++; }
    void end(uint64_t) override { ended++; }
    int begun = 0, ticks = 0, ended = 0;
};

Bytes shot() { return Bytes(display.pixels(), display.pixels() + kW * kH); }

void wait(uint32_t seconds)
{
    for (uint32_t i = 0; i < seconds * 10; i++) {
        clock_.now += 100000;
        desktop.tick();
    }
}

void mouse(int x, int y, uint8_t buttons)
{
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
}

void test_idle_and_wake()
{
    static Probe probe;
    static CountingSaver saver;
    desktop.start();
    desktop.set_animations(false);
    desktop.add(probe);
    mouse(300, 300, 0);
    desktop.set_time(9, 0, 0);
    const Bytes before = shot();

    // No saver set: nothing ever starts.
    wait(700);
    CHECK(!desktop.screen_saver_active());

    desktop.set_screen_saver(&saver, 60);
    wait(59);
    CHECK(!desktop.screen_saver_active());
    // Input starts the wait again.
    desktop.key(wm::key::A, true);
    desktop.key(wm::key::A, false);
    CHECK(probe.keys == 1);
    wait(59);
    CHECK(!desktop.screen_saver_active() && saver.begun == 0);
    wait(2);
    CHECK(desktop.screen_saver_active() && saver.begun == 1);
    CHECK(!pointer.state().visible);
    CHECK(shot() != before);

    // While it shows the desktop paints nothing (the clock's minute passes).
    const Bytes black = shot();
    const int ticks = saver.ticks;
    wait(120);
    CHECK(saver.ticks > ticks);
    CHECK(shot() == black);

    // A nudge of the mouse and a key let go do not wake it.
    mouse(302, 301, 0);
    desktop.key(wm::key::A, false);
    CHECK(desktop.screen_saver_active());

    // A key press does, and the window does not get the key.
    desktop.key(wm::key::A, true);
    CHECK(!desktop.screen_saver_active() && saver.ended == 1);
    CHECK(probe.keys == 1);
    CHECK(pointer.state().visible);
    desktop.key(wm::key::A, false);
    mouse(300, 300, 0);
    desktop.set_time(9, 0, 0); // the taskbar clock as it was
    CHECK(shot() == before);   // the whole desktop is back

    // It starts again after the wait; a real movement wakes it.
    wait(61);
    CHECK(desktop.screen_saver_active());
    mouse(320, 300, 0);
    CHECK(!desktop.screen_saver_active());
    CHECK(pointer.state().x == 320);

    // A click wakes it and is not a click on the window under it.
    wait(61);
    CHECK(desktop.screen_saver_active());
    const gfx::Rect c = probe.client();
    mouse(c.x + 10, c.y + 10, 1);
    CHECK(!desktop.screen_saver_active());
    mouse(c.x + 12, c.y + 10, 1); // still held
    mouse(c.x + 12, c.y + 10, 0);
    CHECK(probe.presses == 0);
    mouse(c.x + 12, c.y + 10, 1); // the next click is real
    mouse(c.x + 12, c.y + 10, 0);
    CHECK(probe.presses == 1);

    // The wheel wakes it too, and scrolls nothing.
    wait(61);
    CHECK(desktop.screen_saver_active());
    desktop.mouse({true, static_cast<int16_t>(c.x + 12), static_cast<int16_t>(c.y + 10), 0, 1});
    CHECK(!desktop.screen_saver_active());

    // Not while a button is held (a drag left alone).
    mouse(c.x + 12, c.y + 10, 1);
    wait(120);
    CHECK(!desktop.screen_saver_active());
    mouse(c.x + 12, c.y + 10, 0);

    // A full repaint asked for by the debug link ends it too.
    desktop.start_screen_saver();
    CHECK(desktop.screen_saver_active());
    desktop.repaint_all();
    CHECK(!desktop.screen_saver_active());

    // Wait 0: never by itself.
    desktop.set_screen_saver(&saver, 0);
    wait(700);
    CHECK(!desktop.screen_saver_active());
    desktop.set_screen_saver(nullptr, 0);
    desktop.close(probe);
}

// The real thing through the shell: 5 minutes by default, Flying
// Chinchillas, 30 frames a second, and the dialog changes both settings.
void test_shell()
{
    static host::HostSystem system;
    static host::HostMemory memory;
    static fs::Storage storage;
    static fs::MemoryVolume disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', disk);
    shell::start(desktop, system, storage, memory);
    desktop.repaint_all();
    mouse(5, 5, 0);
    mouse(300, 300, 0);

    wait(shell::kScreenSaverMinutes * 60 - 1);
    CHECK(!desktop.screen_saver_active());
    wait(2);
    CHECK(desktop.screen_saver_active());
    // Frames come every 1/30 s, whatever the tick rate.
    Bytes last = shot();
    int frames = 0;
    for (int i = 0; i < 3000; i++) { // 3 s in 1 ms ticks
        clock_.now += 1000;
        desktop.tick();
        if (shot() != last) {
            frames++;
            last = shot();
        }
    }
    CHECK(frames >= 88 && frames <= 91);
    desktop.key(wm::key::Space, true);
    desktop.key(wm::key::Space, false);
    CHECK(!desktop.screen_saver_active());

    // The dialog: Starfield, 2 minutes.
    shell::screen_saver_dialog();
    CHECK(desktop.active() != nullptr && strcmp(desktop.active()->title(), "Screen Saver") == 0);
    desktop.key(wm::key::Home, true); // the list has the focus: (None)
    desktop.key(wm::key::Home, false);
    desktop.key(wm::key::Down, true);
    desktop.key(wm::key::Down, false);
    desktop.key(wm::key::Tab, true);
    desktop.key(wm::key::Tab, false);
    desktop.key(wm::key::A, true); // Ctrl+A is not there: End, then erase
    desktop.key(wm::key::A, false);
    desktop.key(wm::key::Backspace, true);
    desktop.key(wm::key::Backspace, false);
    desktop.key(wm::key::Backspace, true);
    desktop.key(wm::key::Backspace, false);
    desktop.text("2");
    desktop.key(wm::key::Enter, true);
    desktop.key(wm::key::Enter, false);
    CHECK(desktop.active() == nullptr || strcmp(desktop.active()->title(), "Screen Saver") != 0);
    wait(119);
    CHECK(!desktop.screen_saver_active());
    wait(2);
    CHECK(desktop.screen_saver_active());
    // Stars: few lit pixels, none of the mascot's pink.
    const Bytes stars = shot();
    CHECK(lit(stars) > 100 && lit(stars) < 2500);
    desktop.key(wm::key::Space, true);
    desktop.key(wm::key::Space, false);

    // OK wrote the settings as a text file.
    char ini[256] = "";
    size_t size = 0;
    CHECK(storage.read_file("C:\\.config\\screensaver.ini", ini, sizeof(ini) - 1, size) == ports::FsError::Ok);
    CHECK(strstr(ini, "[saver]") != nullptr && strstr(ini, "saver = starfield") != nullptr &&
          strstr(ini, "wait_minutes = 2") != nullptr);
    // And a start reads them: Curves after 1 minute.
    const char other[] = "; mine\n[saver]\nsaver = curves\nwait_minutes = 1\n";
    storage.write_file("C:\\.config\\screensaver.ini", other, sizeof(other) - 1);
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    shell::start(desktop, system, storage, memory);
    wait(61);
    CHECK(desktop.screen_saver_active());
    CHECK(lit(shot()) > 2500); // not the stars
    desktop.key(wm::key::Space, true);
    desktop.key(wm::key::Space, false);

    // (None): never again.
    shell::screen_saver_dialog();
    desktop.key(wm::key::Home, true);
    desktop.key(wm::key::Home, false);
    desktop.key(wm::key::Enter, true);
    desktop.key(wm::key::Enter, false);
    wait(3600);
    CHECK(!desktop.screen_saver_active());
}

} // namespace

int main()
{
    for (int i = 0; i < saver::kCount; i++) {
        test_animation(i);
    }
    test_small_screen();
    test_reports_changes();
    test_idle_and_wake();
    test_shell();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
