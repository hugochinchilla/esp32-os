// The Defragmenter: a nostalgic show. Its disk is made up (see
// core/shell/defrag.hpp); no drive is read or written.
#include <cstdio>

#include "core/gfx/art.hpp"
#include "core/shell/defrag.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"
#include "meter.hpp"

namespace shell {

/* ---- The pretend disk ----------------------------------------------------------- */

uint32_t DefragModel::random()
{
    // xorshift32: small, and the same on the board and the PC.
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
}

void DefragModel::touch(int cell)
{
    if (changed_count_ < kMaxChanged) {
        changed_[changed_count_++] = cell;
    }
}

int DefragModel::find(int file, int piece) const
{
    for (int i = 0; i < kCells; i++) {
        if (owner_[i] == file && piece_[i] == piece) {
            return i;
        }
    }
    return -1;
}

void DefragModel::scramble(uint32_t seed)
{
    seed_ = seed * 2654435761u + 0x9E3779B9u;
    if (seed_ == 0) {
        seed_ = 1;
    }
    // Files of 2 to 29 blocks until the disk is about 60 % full.
    constexpr int kTarget = kCells * 60 / 100;
    struct Piece {
        uint8_t file; // 0: free blocks
        uint8_t length;
        uint16_t first;
    };
    static Piece pieces[kCells]; // ponytail: one UI task, one list
    int count = 0;
    files_ = 0;
    used_ = 0;
    while (files_ < kMaxFiles) {
        const int size = 2 + static_cast<int>(random() % 28);
        if (used_ + size > kTarget) {
            break;
        }
        files_++;
        start_[files_] = static_cast<uint16_t>(used_);
        // Each file in fragments of 1 to 5 blocks.
        for (int at = 0; at < size;) {
            int n = 1 + static_cast<int>(random() % 5);
            n = at + n > size ? size - at : n;
            pieces[count++] = {static_cast<uint8_t>(files_), static_cast<uint8_t>(n),
                               static_cast<uint16_t>(at)};
            at += n;
        }
        used_ += size;
    }
    start_[files_ + 1] = static_cast<uint16_t>(used_);
    // The free space in runs of 1 to 4 blocks.
    for (int left = kCells - used_; left > 0;) {
        int n = 1 + static_cast<int>(random() % 4);
        n = n > left ? left : n;
        pieces[count++] = {0, static_cast<uint8_t>(n), 0};
        left -= n;
    }
    // Shuffle the fragments and lay them out one after another.
    for (int i = count - 1; i > 0; i--) {
        const int j = static_cast<int>(random() % static_cast<uint32_t>(i + 1));
        const Piece t = pieces[i];
        pieces[i] = pieces[j];
        pieces[j] = t;
    }
    int cell = 0;
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < pieces[i].length; k++, cell++) {
            owner_[cell] = pieces[i].file;
            piece_[cell] = static_cast<uint16_t>(pieces[i].first + k);
            mark_[cell] = Mark::None;
        }
    }
    placed_ = 0;
    phase_ = Phase::Pick;
    moving_file_ = 0;
    moves_ = 0;
    changed_count_ = 0;
}

bool DefragModel::tidy() const
{
    int cell = 0;
    for (int f = 1; f <= files_; f++) {
        for (int k = 0; k < start_[f + 1] - start_[f]; k++, cell++) {
            if (owner_[cell] != f || piece_[cell] != k) {
                return false;
            }
        }
    }
    for (; cell < kCells; cell++) {
        if (owner_[cell] != 0) {
            return false;
        }
    }
    return true;
}

bool DefragModel::step()
{
    changed_count_ = 0;
    switch (phase_) {
    case Phase::Pick: {
        // Skip what already sits in its place.
        int f = 1;
        while (placed_ < used_) {
            while (placed_ >= start_[f + 1]) {
                f++;
            }
            if (owner_[placed_] != f || piece_[placed_] != placed_ - start_[f]) {
                break;
            }
            placed_++;
        }
        if (placed_ == used_) {
            moving_file_ = 0;
            return false;
        }
        const int want = placed_ - start_[f];
        if (owner_[placed_] != 0) {
            // Something else is in the way: it goes to the last free block.
            from_ = placed_;
            to_ = kCells - 1;
            while (owner_[to_] != 0) {
                to_--;
            }
            run_ = 1;
            moving_file_ = owner_[from_];
        } else {
            // Bring the next blocks of the file here, as many as follow each
            // other and have room.
            from_ = find(f, want);
            to_ = placed_;
            run_ = 1;
            while (run_ < kMaxRun && to_ + run_ < start_[f + 1] && owner_[to_ + run_] == 0 &&
                   from_ + run_ < kCells && owner_[from_ + run_] == f &&
                   piece_[from_ + run_] == want + run_) {
                run_++;
            }
            moving_file_ = f;
        }
        for (int i = 0; i < run_; i++) {
            mark_[from_ + i] = Mark::Reading;
            touch(from_ + i);
        }
        phase_ = Phase::Read;
        return true;
    }
    case Phase::Read:
        for (int i = 0; i < run_; i++) {
            mark_[to_ + i] = Mark::Writing;
            touch(to_ + i);
        }
        phase_ = Phase::Write;
        return true;
    case Phase::Write:
        for (int i = 0; i < run_; i++) {
            owner_[to_ + i] = owner_[from_ + i];
            piece_[to_ + i] = piece_[from_ + i];
            owner_[from_ + i] = 0;
            mark_[from_ + i] = Mark::None;
            touch(from_ + i);
            touch(to_ + i);
        }
        moves_++;
        phase_ = Phase::Settle;
        return true;
    case Phase::Settle:
        for (int i = 0; i < run_; i++) {
            mark_[to_ + i] = Mark::None;
            touch(to_ + i);
        }
        if (to_ == placed_) {
            placed_ += run_;
        }
        phase_ = Phase::Pick;
        return true;
    }
    return false;
}

/* ---- The window ----------------------------------------------------------------- */

namespace {

constexpr int kPitch = 8; // a block is 7x7 with a 1-pixel gap right and below
constexpr gfx::Colour kReading = gfx::rgb(7, 7, 0);
constexpr gfx::Colour kWriting = gfx::rgb(7, 0, 0);
constexpr gfx::Colour kFree = gfx::kWhite;
// One colour per file, in turn: scattered colours gather into bands.
constexpr gfx::Colour kFileColours[] = {
    gfx::rgb(1, 2, 3), gfx::rgb(0, 5, 2), gfx::rgb(4, 1, 3), gfx::rgb(1, 6, 0), gfx::rgb(0, 0, 2),
    gfx::rgb(7, 3, 3), gfx::rgb(4, 4, 0), gfx::rgb(2, 6, 3), gfx::rgb(0, 3, 0), gfx::rgb(3, 3, 3),
};
constexpr int kFileColourCount = sizeof(kFileColours) / sizeof(kFileColours[0]);

gfx::Colour file_colour(int file)
{
    return file == 0 ? kFree : kFileColours[(file - 1) % kFileColourCount];
}

// One block at (x, y): 8x8 pixels with its gap, each pixel written once.
void paint_block(gfx::Canvas &c, int x, int y, gfx::Colour colour, bool framed)
{
    if (framed) {
        c.fill({x, y, 7, 1}, gfx::kBlack);
        c.fill({x, y + 6, 7, 1}, gfx::kBlack);
        c.fill({x, y + 1, 1, 5}, gfx::kBlack);
        c.fill({x + 6, y + 1, 1, 5}, gfx::kBlack);
        c.fill({x + 1, y + 1, 5, 5}, colour);
    } else {
        c.fill({x, y, 7, 7}, colour);
    }
    c.fill({x + 7, y, 1, kPitch}, gfx::kGrey);
    c.fill({x, y + 7, 7, 1}, gfx::kGrey);
}

class MapView : public wm::Widget {
public:
    MapView(int x, int y, const DefragModel &disk)
        : Widget({x, y, DefragModel::kColumns * kPitch + 4, DefragModel::kRows * kPitch + 4}),
          disk_(disk)
    {
    }

    void paint(gfx::Canvas &c) override
    {
        c.sunken({0, 0, rect.w, rect.h});
        for (int cell = 0; cell < DefragModel::kCells; cell++) {
            const DefragModel::Mark m = disk_.mark(cell);
            const gfx::Colour colour = m == DefragModel::Mark::Reading   ? kReading
                                       : m == DefragModel::Mark::Writing ? kWriting
                                                                         : file_colour(disk_.file(cell));
            paint_block(c, 2 + cell % DefragModel::kColumns * kPitch,
                        2 + cell / DefragModel::kColumns * kPitch, colour, m != DefragModel::Mark::None);
        }
    }

    void block_changed(int cell)
    {
        invalidate_part({2 + cell % DefragModel::kColumns * kPitch,
                         2 + cell / DefragModel::kColumns * kPitch, kPitch, kPitch});
    }

private:
    const DefragModel &disk_;
};

// What the colours mean.
class Legend : public wm::Widget {
public:
    explicit Legend(const gfx::Rect &r) : Widget(r) {}

    void paint(gfx::Canvas &c) override
    {
        c.fill({0, 0, rect.w, rect.h}, gfx::kGrey);
        int x = 0;
        const int y = (rect.h - 9) / 2;
        const auto entry = [&](const char *label, gfx::Colour a, gfx::Colour b, bool framed) {
            c.fill({x, y, 9, 9}, gfx::kDarkGrey);
            if (framed) {
                c.fill({x + 1, y + 1, 7, 7}, gfx::kBlack);
                c.fill({x + 2, y + 2, 5, 5}, a);
            } else {
                c.fill({x + 1, y + 1, 4, 7}, a);
                c.fill({x + 5, y + 1, 3, 7}, b);
            }
            x = c.text(x + 13, (rect.h - 12) / 2, label, gfx::kBlack) + 14;
        };
        entry("Free", kFree, kFree, false);
        entry("Files", kFileColours[0], kFileColours[1], false);
        entry("Reading", kReading, kReading, true);
        entry("Writing", kWriting, kWriting, true);
    }
};

class DefragWindow : public wm::Window {
public:
    static constexpr int kMapW = DefragModel::kColumns * kPitch + 4;
    static constexpr int kMapH = DefragModel::kRows * kPitch + 4;
    static constexpr int kClientW = kMapW + 16;
    static constexpr int kButtonsY = kMapH + 104;
    static constexpr int kClientH = kButtonsY + 23 + 8;

    DefragWindow()
        : Window("Defragmenter", {0, 0, kClientW + 2 * kBorder, kClientH + kClientTop + kBorder},
                 &gfx::drive_icon_16()),
          about_({8, 6, kMapW, 14}, "A pretend disk, tidied for old times' sake. No drive is touched."),
          map_(8, 24, disk_),
          status_({8, kMapH + 30, kMapW, 14}, status_text_),
          meter_({8, kMapH + 48, kMapW - 40, 16}),
          percent_({kMapW - 26, kMapH + 49, 34, 14}, percent_text_),
          legend_({8, kMapH + 72, kMapW, 16}),
          start_({8, kButtonsY, 75, 23}, "&Start", &DefragWindow::start_pressed, this),
          scramble_({89, kButtonsY, 75, 23}, "S&cramble", &DefragWindow::scramble_pressed, this),
          close_({kClientW - 75 - 8, kButtonsY, 75, 23}, "Close", &DefragWindow::close_pressed, this)
    {
        set_dialog(true); // fixed size, only the close button
        add(start_);
        add(scramble_);
        add(close_);
        add(about_);
        add(map_);
        add(status_);
        add(meter_);
        add(percent_);
        add(legend_);
        start_.set_default(true);
        set_default(&start_);
        show_state();
    }

    const DefragModel &disk() const { return disk_; }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return Window::key(k);
    }

    // Closing pauses; the map is kept.
    void closed() override
    {
        pause();
        show_state();
    }

private:
    static void start_pressed(wm::Button &, void *context)
    {
        auto *w = static_cast<DefragWindow *>(context);
        if (w->timer_ != 0) {
            w->pause();
        } else {
            if (w->disk_.done()) {
                w->disk_.scramble(++w->seed_);
                w->map_.invalidate();
            }
            w->timer_ = shell::manager().start_timer(kDefragStepUs, kDefragStepUs, &DefragWindow::tick, w);
            w->started_ = true;
        }
        w->show_state();
    }

    static void scramble_pressed(wm::Button &, void *context)
    {
        auto *w = static_cast<DefragWindow *>(context);
        w->pause();
        w->disk_.scramble(++w->seed_);
        w->started_ = false;
        w->map_.invalidate();
        w->show_state();
    }

    static void close_pressed(wm::Button &, void *context)
    {
        static_cast<DefragWindow *>(context)->close();
    }

    static void tick(void *context)
    {
        auto *w = static_cast<DefragWindow *>(context);
        const bool more = w->disk_.step();
        for (int i = 0; i < w->disk_.changed_count(); i++) {
            w->map_.block_changed(w->disk_.changed(i));
        }
        if (!more) {
            w->pause();
        }
        w->show_state();
    }

    void pause()
    {
        if (timer_ != 0) {
            shell::manager().stop_timer(timer_);
        }
        timer_ = 0;
    }

    // The status line, the meter and the Start button, when they change.
    void show_state()
    {
        char text[sizeof(status_text_)];
        const char *button = "&Start";
        if (disk_.done()) {
            snprintf(text, sizeof(text), "Done: %d files in order after %u moves. Nothing was written.",
                     disk_.file_count(), static_cast<unsigned>(disk_.moves()));
        } else if (timer_ != 0) {
            button = "&Pause";
            if (disk_.current_file() != 0) {
                snprintf(text, sizeof(text), "Moving file %d of %d...", disk_.current_file(),
                         disk_.file_count());
            } else {
                snprintf(text, sizeof(text), "Looking at the disk...");
            }
        } else if (started_) {
            button = "&Resume";
            snprintf(text, sizeof(text), "Paused.");
        } else {
            snprintf(text, sizeof(text), "%d files in pieces. Press Start to tidy them.",
                     disk_.file_count());
        }
        bool same = true;
        for (size_t i = 0; i < sizeof(text) && same; i++) {
            same = text[i] == status_text_[i];
            if (text[i] == 0) {
                break;
            }
        }
        if (!same) {
            snprintf(status_text_, sizeof(status_text_), "%s", text);
            status_.invalidate();
        }
        if (button != button_) {
            button_ = button;
            start_.set_label(button);
        }
        if (disk_.percent() != shown_percent_) {
            shown_percent_ = disk_.percent();
            snprintf(percent_text_, sizeof(percent_text_), "%3d%%", shown_percent_);
            percent_.invalidate();
            meter_.set(static_cast<uint32_t>(disk_.placed()), static_cast<uint32_t>(disk_.used()));
        }
    }

    DefragModel disk_;
    uint32_t seed_ = 1;
    int timer_ = 0;
    bool started_ = false;
    int shown_percent_ = -1;
    const char *button_ = nullptr;
    char status_text_[72] = "";
    char percent_text_[8] = "";

    wm::Label about_;
    MapView map_;
    wm::Label status_;
    Meter meter_;
    wm::Label percent_;
    Legend legend_;
    wm::Button start_, scramble_, close_;
};

DefragWindow &defrag_window()
{
    static DefragWindow window;
    return window;
}

} // namespace

void open_defragmenter()
{
    const AsProgram as("Defragmenter");
    DefragWindow &w = defrag_window();
    if (!w.open()) {
        w.set_frame(centred(manager(), w.frame().w, w.frame().h));
    }
    show(w);
}

const DefragModel &defragmenter_disk() { return defrag_window().disk(); }

} // namespace shell
