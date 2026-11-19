// Shutting down. The board has no soft power switch, so Shut Down ends, as
// on machines of the time, on a screen that says the power can go. The
// drawing is our own: the mascot asleep under a night sky.
#include "core/gfx/font.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

// Shut Down is a sequence (backlog item 60, docs/decisions/
// shut-down-sequence.md), each stage on the desktop's timer:
//   Closing  every program is asked to close and may ask its own question
//            first; the system waits for the answers, and ends what is
//            still open when kCloseWaitUs have passed.
//   Desktop  the windows are gone: what is left, the icons and the taskbar,
//            fades into the plain desktop.
//   Black    the desktop fades to black.
//   Rising   the last screen comes up out of the black.
//   Down     it stays, and Ctrl+Alt+Del starts the system again.
enum class Stage : uint8_t { Running, Closing, Desktop, Black, Rising, Down };

constexpr uint32_t kPollUs = 100000;        // how often the closing programs are looked at
constexpr uint32_t kForcedWaitUs = 2000000; // what force has not ended by then is left behind
constexpr uint32_t kFadeStepUs = 60000;     // one level of colour
constexpr int kLevels = 7;                  // red and green have eight levels: seven steps end to end
constexpr int kRestSteps = 5;               // the plain desktop stays this many steps before it darkens
constexpr int kMaxStayed = 8;

struct Sequence {
    Stage stage = Stage::Running;
    int timer = 0;
    uint64_t deadline = 0; // Closing: when force is used; after it, when to go on regardless
    bool forced = false;
    bool first = true;     // Closing: nothing has been asked yet
    int stayed[kMaxStayed] = {}; // windows that were asked and are still open (0: free)
    int step = 0;
    int level = kLevels;   // how far up the last screen is, of kLevels
};
Sequence g_seq;

// `c` at `level` sevenths of its brightness.
gfx::Colour lit(gfx::Colour c)
{
    const unsigned k = static_cast<unsigned>(g_seq.level);
    if (k >= kLevels) {
        return c;
    }
    const unsigned r = c >> 5, g = (c >> 2) & 7, b = c & 3;
    return gfx::rgb((r * k + 3) / kLevels, (g * k + 3) / kLevels, (b * k + 3) / kLevels);
}

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
            c.fill({x + q * s, y + r * s, s, s}, lit(col));
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

// Where the drawing stands on the screen. It is drawn for 640 x 480; on a
// larger screen it stands in the middle of the black.
constexpr int kDrawingW = 640, kDrawingH = 480;
gfx::Rect drawing()
{
    const wm::Wm &m = manager();
    const gfx::Rect work = m.work_area();
    const int screen_w = work.w, screen_h = work.h + wm::Wm::kTaskbarHeight;
    return {(screen_w - kDrawingW) / 2, (screen_h - kDrawingH) / 2, kDrawingW, kDrawingH};
}

void paint_screen(gfx::Canvas &screen, void *)
{
    const int w = kDrawingW;
    const gfx::Rect at = drawing();
    screen.fill({0, 0, at.w + 2 * at.x, at.h + 2 * at.y}, gfx::kBlack);
    gfx::Canvas c = screen.sub(at);
    const gfx::Colour white = lit(gfx::kWhite);
    // Stars: always the same sky.
    uint32_t seed = 96;
    for (int i = 0; i < 70; i++) {
        seed = seed * 1664525u + 1013904223u;
        const int x = static_cast<int>((seed >> 8) % w), y = static_cast<int>((seed >> 20) % 250);
        const bool bright = i % 5 == 0;
        c.fill({x, y, 1, 1}, bright ? white : lit(gfx::rgb(4, 4, 2)));
        if (i % 10 == 0) {
            c.fill({x - 1, y, 3, 1}, white);
            c.fill({x, y - 1, 1, 3}, white);
        }
    }
    constexpr int kScale = 9;
    const int mx = (w - 16 * kScale) / 2, my = 96;
    asleep(c, mx, my, kScale);
    big_text(c, mx + 150, my + 22, "z", 1, white);
    big_text(c, mx + 162, my - 2, "z", 2, white);
    big_text(c, mx + 182, my - 38, "Z", 3, white);

    const char *line = "It is safe to power down the system now.";
    big_text(c, (w - big_width(line, 2)) / 2, 290, line, 2, white);
    const char *hint = "Unplug the board, or press Ctrl+Alt+Del to start Chinchilla again.";
    c.text((w - gfx::Canvas::text_width(hint)) / 2, 336, hint, lit(kHint));
}

void step(void *);

// The timer runs as the desktop: whoever chose Shut Down (its dialog) is
// one of the programs that close.
void run_every(uint32_t period_us, uint32_t first_us)
{
    wm::Wm &m = manager();
    m.stop_timer(g_seq.timer);
    const proc::Scope as(&proc::table(), m.desktop_pid());
    g_seq.timer = m.start_timer(first_us, period_us, &step, nullptr);
}

void down()
{
    wm::Wm &m = manager();
    m.stop_timer(g_seq.timer);
    g_seq.timer = 0;
    g_seq.stage = Stage::Down;
    g_seq.level = kLevels;
}

// The windows are gone, or given up on: the picture is kept as it is and
// fades from here. The closing melody (backlog item 59) starts with it.
void fade()
{
    wm::Wm &m = manager();
    m.hold_screen();
    os::play_sound(sound::Id::Close);
    g_seq.stage = Stage::Desktop;
    g_seq.step = 0;
    run_every(kFadeStepUs, kFadeStepUs);
    if (g_seq.timer == 0) {
        // No timer to be had: the last screen at once.
        down();
        m.take_over(&paint_screen, nullptr);
    }
}

bool top_level(const wm::Window &w) { return !(w.modal() && w.modal_owner() != nullptr); }
bool busy(wm::Wm &m, const wm::Window &w) { return m.blocker(w) != nullptr || w.locked(); }

// Ends what is still open: KILL for a program, which the window server acts
// on at its next safe point; a window nobody may signal is closed here.
void force()
{
    wm::Wm &m = manager();
    for (int tries = 2 * m.window_count() + 2, z = m.window_count() - 1; z >= 0 && tries > 0; tries--) {
        wm::Window *w = m.window_at(z);
        if (proc::table().signal(w->owner(), proc::Signal::Kill) == proc::Sent::Ok) {
            z--;
        } else {
            m.close(*w);
            z = m.window_count() - 1;
        }
    }
}

// Closing: looks at the programs once. Those that can be asked are asked;
// one with a question open is waited for; one that was asked, has no
// question open and is still there has said no, and Shut Down is off.
void closing()
{
    wm::Wm &m = manager();
    Sequence &q = g_seq;
    const bool first = q.first;
    q.first = false;
    if (m.window_count() == 0) {
        fade();
        return;
    }
    if (m.now_us() >= q.deadline) {
        if (q.forced) {
            fade();
        } else {
            force();
            q.forced = true;
            q.deadline = m.now_us() + kForcedWaitUs;
        }
        return;
    }
    if (q.forced) {
        return;
    }
    for (int &id : q.stayed) {
        const wm::Window *w = id != 0 ? m.window(id) : nullptr;
        if (w == nullptr) {
            id = 0;
        } else if (!busy(m, *w)) {
            // Cancel in "Save changes?", or a program that just stays.
            m.stop_timer(q.timer);
            q = Sequence();
            return;
        }
    }
    // Oldest first, by their numbers: the list changes as they close.
    for (int asked = 0;;) {
        wm::Window *next = nullptr;
        for (int z = 0; z < m.window_count(); z++) {
            wm::Window *w = m.window_at(z);
            if (top_level(*w) && w->id() > asked && (next == nullptr || w->id() < next->id())) {
                next = w;
            }
        }
        if (next == nullptr) {
            break;
        }
        asked = next->id();
        int *slot = nullptr;
        bool known = false;
        for (int &id : q.stayed) {
            known = known || id == asked;
            slot = id == 0 && slot == nullptr ? &id : slot;
        }
        if (known || slot == nullptr) {
            continue; // its question is open; or eight are, and this one waits its turn
        }
        if (busy(m, *next)) {
            // A dialog of its own was open already, or it is saving: asked
            // when that is over. The dialog comes forward once.
            if (first) {
                m.activate(*next);
            }
            continue;
        }
        // As a click on its close button: it comes to the front, so that a
        // question it asks is its own window's.
        m.activate(*next);
        const proc::Scope as(&proc::table(), next->owner());
        if (next->close_requested()) {
            m.close(*next);
        } else {
            *slot = asked;
        }
    }
}

void step(void *)
{
    wm::Wm &m = manager();
    Sequence &q = g_seq;
    switch (q.stage) {
    case Stage::Closing: closing(); break;
    case Stage::Desktop:
        // Blue has four levels to the others' eight: it moves on the even steps.
        q.step++;
        if (q.step <= kLevels) {
            m.fade_step(true, gfx::kBlack, q.step % 2 == 0);
        } else if (q.step >= kLevels + kRestSteps) {
            q.stage = Stage::Black;
            q.step = 0;
        }
        break;
    case Stage::Black:
        q.step++;
        m.fade_step(false, gfx::kBlack, q.step % 2 == 0);
        if (q.step >= kLevels) {
            q.stage = Stage::Rising;
            q.level = 0;
            m.take_over(&paint_screen, nullptr);
        }
        break;
    case Stage::Rising: {
        q.level++;
        // Not the whole screen: the lines the drawing has.
        const gfx::Rect at = drawing();
        m.invalidate({at.x, at.y, at.w, 352});
        if (q.level >= kLevels) {
            down();
        }
        break;
    }
    default: break;
    }
}

} // namespace

bool powered_down() { return g_seq.stage == Stage::Down; }
bool shutting_down() { return g_seq.stage != Stage::Running && g_seq.stage != Stage::Down; }

bool power_down()
{
    if (g_seq.stage != Stage::Running) {
        return true;
    }
    wm::Wm &m = manager();
    g_seq = Sequence();
    g_seq.stage = Stage::Closing;
    g_seq.deadline = m.now_us() + kCloseWaitUs;
    // The first look is the timer's too, at once: it runs as the desktop,
    // not inside the program that asked.
    run_every(kPollUs, 1);
    if (g_seq.timer == 0) {
        // No timer to wait with: what is open is ended, and the last screen
        // shows at once.
        force();
        down();
        m.take_over(&paint_screen, nullptr);
        os::play_sound(sound::Id::Close);
        return true;
    }
    return true;
}

// Ctrl+Alt+Del: the Task Manager; on the power-down screen, a restart;
// nothing while the picture fades.
void attention_key(void *)
{
    if (g_seq.stage == Stage::Down) {
        system_port().reboot();
    } else if (!manager().taken_over()) {
        open_task_manager();
    }
}

} // namespace shell
