// Shutting down. The board has no soft power switch, so Shut Down ends, as
// on machines of the time, on a screen that says the power can go. The
// drawing is our own: the mascot asleep under a night sky.
#include "core/gfx/font.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

bool g_down = false;

constexpr gfx::Colour kNight = gfx::rgb(0, 0, 1);
constexpr gfx::Colour kHint = gfx::rgb(4, 5, 3);

// The mascot of gfx::mascot_16() with its eyes shut.
constexpr const char *kAsleep[16] = {
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG",
    "GPPPGGLLLLGGPPPG", ".GGGLLLLLLLLGGG.", "..GLLLLLLLLLLG..", "..GLLLLLLLLLLG..",
    "..GKKKLLLLKKKG..", "..GLLLLNNLLLLG..", "D.GLLLLNNLLLLG.D", ".DDGLLLLLLLLGDD.",
    "D..GGLLLLLLGG..D", "....GGGGGGGG....", "................", "................",
};

void asleep(gfx::Canvas &c, int x, int y, int s)
{
    for (int r = 0; r < 16; r++) {
        for (int q = 0; q < 16; q++) {
            gfx::Colour col;
            switch (kAsleep[r][q]) {
            case 'G':
            case 'D': col = gfx::rgb(3, 3, 2); break; // lighter than on the desktop: the sky is dark
            case 'L': col = gfx::kWhite; break;
            case 'P': col = gfx::rgb(7, 4, 2); break;
            case 'K': col = gfx::kBlack; break;
            case 'N': col = gfx::rgb(7, 3, 2); break;
            default: continue;
            }
            c.fill({x + q * s, y + r * s, s, s}, col);
        }
    }
}

// Text in the interface font, every pixel `s` times as large.
void big_text(gfx::Canvas &c, int x, int y, const char *text, int s, gfx::Colour col)
{
    const gfx::Font &f = gfx::kSpleen6x12;
    while (const uint8_t code = gfx::next_latin1(text)) {
        const uint8_t *g = f.glyph(code);
        for (int row = 0; row < f.height; row++) {
            for (int q = 0; q < f.width; q++) {
                if (g[row] & (0x80 >> q)) {
                    c.fill({x + q * s, y + row * s, s, s}, col);
                }
            }
        }
        x += f.width * s;
    }
}

int big_width(const char *text, int s) { return gfx::Canvas::text_width(text) * s; }

void paint_screen(gfx::Canvas &screen, void *)
{
    // Drawn for 640 x 480; on a larger screen it stands in the middle of
    // the night sky's colour.
    const int w = 640, h = 480;
    const gfx::Rect work = manager().work_area();
    const int screen_w = work.w, screen_h = work.h + wm::Wm::kTaskbarHeight;
    screen.fill({0, 0, screen_w, screen_h}, kNight);
    gfx::Canvas c = screen.sub({(screen_w - w) / 2, (screen_h - h) / 2, w, h});
    // Stars: always the same sky.
    uint32_t seed = 96;
    for (int i = 0; i < 70; i++) {
        seed = seed * 1664525u + 1013904223u;
        const int x = static_cast<int>((seed >> 8) % w), y = static_cast<int>((seed >> 20) % 250);
        const bool bright = i % 5 == 0;
        c.fill({x, y, 1, 1}, bright ? gfx::kWhite : gfx::rgb(4, 4, 2));
        if (i % 10 == 0) {
            c.fill({x - 1, y, 3, 1}, gfx::kWhite);
            c.fill({x, y - 1, 1, 3}, gfx::kWhite);
        }
    }
    constexpr int kScale = 9;
    const int mx = (w - 16 * kScale) / 2, my = 96;
    asleep(c, mx, my, kScale);
    big_text(c, mx + 150, my + 22, "z", 1, gfx::kWhite);
    big_text(c, mx + 162, my - 2, "z", 2, gfx::kWhite);
    big_text(c, mx + 182, my - 38, "Z", 3, gfx::kWhite);

    const char *line = "It is safe to power down the system now.";
    big_text(c, (w - big_width(line, 2)) / 2, 290, line, 2, gfx::kWhite);
    const char *hint = "Unplug the board, or press Ctrl+Alt+Del to start Chinchilla again.";
    c.text((w - gfx::Canvas::text_width(hint)) / 2, 336, hint, kHint);
}

} // namespace

bool powered_down() { return g_down; }

bool power_down()
{
    if (g_down) {
        return true;
    }
    wm::Wm &m = manager();
    // Every program is asked to close, as its close button would. One that
    // has a dialog open, or asks about unsaved changes, calls it off: the
    // question stays on screen, and Shut Down can be chosen again after it.
    for (int i = 0; i < m.taskbar_count(); i++) {
        wm::Window *w = m.taskbar_window(i);
        if (m.blocker(*w) != nullptr || w->locked()) {
            m.activate(*w); // brings its dialog forward; a locked one is still saving
            return false;
        }
    }
    // Those open now, oldest first, by their numbers: the list changes as
    // they close, and a question one of them asks is not asked to close.
    int newest = 0;
    for (int i = 0; i < m.taskbar_count(); i++) {
        newest = m.taskbar_window(i)->id() > newest ? m.taskbar_window(i)->id() : newest;
    }
    for (int asked = 0;;) {
        wm::Window *next = nullptr;
        for (int i = 0; i < m.taskbar_count(); i++) {
            wm::Window *w = m.taskbar_window(i);
            if (w->id() > asked && w->id() <= newest && (next == nullptr || w->id() < next->id())) {
                next = w;
            }
        }
        if (next == nullptr) {
            break;
        }
        asked = next->id();
        // Its "save changes?" question is its own window, not the desktop's.
        const proc::Scope as(&proc::table(), next->owner());
        if (next->close_requested()) {
            m.close(*next);
        }
    }
    if (m.window_count() != 0) {
        return false;
    }
    g_down = true;
    m.take_over(&paint_screen, nullptr);
    // The closing melody (backlog item 59), over the last screen.
    os::play_sound(sound::Id::Close);
    return true;
}

// Ctrl+Alt+Del: the Task Manager; on the power-down screen, a restart.
void attention_key(void *)
{
    if (g_down) {
        system_port().reboot();
    } else {
        open_task_manager();
    }
}

} // namespace shell
