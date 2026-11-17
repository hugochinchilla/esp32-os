// Cactus Patch: a tile-clearing puzzle. Under the tiles of a patch of sand
// some cacti are hidden; numbers say how many are next to a tile. The rules
// are in core/games/cactus.hpp; this is the window: the status row (cacti
// left, the mascot's face, seconds), the field and the menus. All of its
// pictures are original, drawn here as text rows.
#include "core/gfx/art.hpp"
#include <cstdio>
#include <cstring>

#include "core/games/cactus.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

using games::CactusField;
using gfx::Colour;
using gfx::kBlack;
using gfx::kDarkGrey;
using gfx::kGrey;
using gfx::kWhite;
using gfx::Rect;
using gfx::rgb;
using Tile = CactusField::Tile;

/* ---- Pictures --------------------------------------------------------------- */

constexpr int kClear = rgb(7, 0, 3); // magenta: never used in the pictures

struct Ink {
    char key;
    Colour colour;
};

// A square picture drawn as text rows, worked out by the compiler so it
// sits in flash. Characters not in `inks` are clear.
template <int S> struct Pixels {
    uint8_t p[S * S] = {};
};

template <int S, int N> constexpr Pixels<S> draw(const char *const (&rows)[S], const Ink (&inks)[N])
{
    Pixels<S> out;
    for (int r = 0; r < S; r++) {
        for (int q = 0; q < S; q++) {
            uint8_t v = kClear;
            for (int i = 0; i < N; i++) {
                if (rows[r][q] == inks[i].key) {
                    v = inks[i].colour;
                }
            }
            out.p[r * S + q] = v;
        }
    }
    return out;
}

constexpr Colour kSand = rgb(7, 6, 2);       // open ground
constexpr Colour kHitGround = rgb(7, 2, 1);  // under the cactus stepped on
constexpr Colour kLine = rgb(5, 4, 1);       // between open tiles

constexpr Ink kCactusInks[] = {{'G', rgb(0, 4, 0)}, {'L', rgb(3, 7, 1)}, {'B', rgb(4, 2, 0)},
                               {'R', rgb(6, 2, 0)}, {'T', rgb(4, 1, 0)}};

// Pictures inside a tile are 12x12.
constexpr const char *kCactus[12] = {
    ".....GG.....", "....GLGG....", "....GLGG.G..", ".G..GLGG.GG.", ".GG.GLGG.GG.", ".GG.GLGGGGG.",
    ".GGGGLGGGG..", "..GGGLGG....", "....GLGG....", "....GLGG....", "...BBBBBB...", "............",
};
constexpr Pixels<12> kCactusPixels = draw(kCactus, kCactusInks);

// The marker: an orange pennant on a stake.
constexpr const char *kMarker[12] = {
    "....K.......", "....KOO.....", "....KOOOO...", "....KOOOOOO.", "....KOOOO...", "....KOO.....",
    "....K.......", "....K.......", "....K.......", "...KKK......", "..KKKKK.....", "............",
};
constexpr Ink kMarkerInks[] = {{'K', kBlack}, {'O', rgb(7, 3, 0)}};
constexpr Pixels<12> kMarkerPixels = draw(kMarker, kMarkerInks);

// A marker that was wrong: the same, crossed out.
constexpr Pixels<12> crossed(Pixels<12> p)
{
    for (int i = 1; i < 11; i++) {
        p.p[i * 12 + i] = rgb(6, 0, 0);
        p.p[i * 12 + (11 - i)] = rgb(6, 0, 0);
    }
    return p;
}
constexpr Pixels<12> kWrongPixels = crossed(kMarkerPixels);

constexpr const char *kClock[12] = {
    "............", "...KKKKKK...", "..KYYYYYYK..", ".KYYYKYYYYK.", ".KYYYKYYYYK.", ".KYYYKYYYYK.",
    ".KYYYKKKYYK.", ".KYYYYYYYYK.", ".KYYYYYYYYK.", "..KYYYYYYK..", "...KKKKKK...", "............",
};
constexpr Ink kClockInks[] = {{'K', kBlack}, {'Y', rgb(7, 7, 1)}};
constexpr Pixels<12> kClockPixels = draw(kClock, kClockInks);

// The mascot's face in four moods (the plain one is the OS logo's drawing).
constexpr Ink kFaceInks[] = {{'G', kDarkGrey}, {'L', kWhite}, {'P', rgb(7, 4, 2)},
                             {'K', kBlack},    {'N', rgb(7, 3, 2)}, {'D', kDarkGrey}};
constexpr const char *kFaceCalm[16] = {
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG",
    "GPPPGGLLLLGGPPPG", ".GGGLLLLLLLLGGG.", "..GLLLLLLLLLLG..", "..GLKLLLLLLKLG..",
    "..GLKLLLLLLKLG..", "..GLLLLNNLLLLG..", "D.GLLLLNNLLLLG.D", ".DDGLLLLLLLLGDD.",
    "D..GGLLLLLLGG..D", "....GGGGGGGG....", "................", "................",
};
constexpr const char *kFaceCareful[16] = { // tall eyes, a small round mouth
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG",
    "GPPPGGLLLLGGPPPG", ".GGGLLLLLLLLGGG.", "..GLKLLLLLLKLG..", "..GLKLLLLLLKLG..",
    "..GLKLLLLLLKLG..", "..GLLLLNNLLLLG..", "D.GLLLLNNLLLLG.D", ".DDGLLLKKLLLGDD.",
    "D..GGLLKKLLGG..D", "....GGGGGGGG....", "................", "................",
};
constexpr const char *kFaceOuch[16] = { // crossed eyes, a flat mouth
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG",
    "GPPPGGLLLLGGPPPG", ".GGGLLLLLLLLGGG.", "..GKLKLLLLKLKG..", "..GLKLLLLLLKLG..",
    "..GKLKLLLLKLKG..", "..GLLLLNNLLLLG..", "D.GLLLLNNLLLLG.D", ".DDGLLKKKKLLGDD.",
    "D..GGLLLLLLGG..D", "....GGGGGGGG....", "................", "................",
};
constexpr const char *kFaceGlad[16] = { // eyes shut in a smile
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG",
    "GPPPGGLLLLGGPPPG", ".GGGLLLLLLLLGGG.", "..GLKLLLLLLKLG..", "..GKLKLLLLKLKG..",
    "..GLLLLLLLLLLG..", "..GLLLLNNLLLLG..", "D.GLLLLNNLLLLG.D", ".DDGLKLLLLKLGDD.",
    "D..GGLKKKKLGG..D", "....GGGGGGGG....", "................", "................",
};
constexpr Pixels<16> kFacePixels[4] = {draw(kFaceCalm, kFaceInks), draw(kFaceCareful, kFaceInks),
                                       draw(kFaceOuch, kFaceInks), draw(kFaceGlad, kFaceInks)};

const gfx::Bitmap kCactusBitmap = {12, 12, kCactusPixels.p, kClear};
const gfx::Bitmap kMarkerBitmap = {12, 12, kMarkerPixels.p, kClear};
const gfx::Bitmap kWrongBitmap = {12, 12, kWrongPixels.p, kClear};
const gfx::Bitmap kClockBitmap = {12, 12, kClockPixels.p, kClear};
const gfx::Bitmap kFaceBitmaps[4] = {{16, 16, kFacePixels[0].p, kClear},
                                     {16, 16, kFacePixels[1].p, kClear},
                                     {16, 16, kFacePixels[2].p, kClear},
                                     {16, 16, kFacePixels[3].p, kClear}};

// Digits, 10x12 with a blank edge: on open tiles and in the two counters.
// The last one is a minus sign.
constexpr int kDigitW = 10, kDigitH = 12;
const char *const kDigits[11][kDigitH] = {
    {"          ", "  XXXXXX  ", " XXXXXXXX ", " XX    XX ", " XX    XX ", " XX    XX ",
     " XX    XX ", " XX    XX ", " XX    XX ", " XXXXXXXX ", "  XXXXXX  ", "          "},
    {"          ", "    XX    ", "   XXX    ", "  XXXX    ", "    XX    ", "    XX    ",
     "    XX    ", "    XX    ", "    XX    ", "  XXXXXX  ", "  XXXXXX  ", "          "},
    {"          ", "  XXXXXX  ", " XXXXXXXX ", " XX    XX ", "       XX ", "     XXX  ",
     "   XXX    ", "  XXX     ", " XX       ", " XXXXXXXX ", " XXXXXXXX ", "          "},
    {"          ", "  XXXXXX  ", " XXXXXXXX ", "       XX ", "       XX ", "   XXXXX  ",
     "   XXXXX  ", "       XX ", "       XX ", " XXXXXXXX ", "  XXXXXX  ", "          "},
    {"          ", "     XXX  ", "    XXXX  ", "   XX XX  ", "  XX  XX  ", " XX   XX  ",
     " XXXXXXXX ", " XXXXXXXX ", "      XX  ", "      XX  ", "      XX  ", "          "},
    {"          ", " XXXXXXXX ", " XXXXXXXX ", " XX       ", " XX       ", " XXXXXXX  ",
     " XXXXXXXX ", "       XX ", "       XX ", " XXXXXXXX ", " XXXXXXX  ", "          "},
    {"          ", "  XXXXXX  ", " XXXXXXX  ", " XX       ", " XX       ", " XXXXXXX  ",
     " XXXXXXXX ", " XX    XX ", " XX    XX ", " XXXXXXXX ", "  XXXXXX  ", "          "},
    {"          ", " XXXXXXXX ", " XXXXXXXX ", "       XX ", "      XX  ", "     XX   ",
     "     XX   ", "    XX    ", "    XX    ", "    XX    ", "    XX    ", "          "},
    {"          ", "  XXXXXX  ", " XXXXXXXX ", " XX    XX ", " XX    XX ", "  XXXXXX  ",
     "  XXXXXX  ", " XX    XX ", " XX    XX ", " XXXXXXXX ", "  XXXXXX  ", "          "},
    {"          ", "  XXXXXX  ", " XXXXXXXX ", " XX    XX ", " XX    XX ", " XXXXXXXX ",
     "  XXXXXXX ", "       XX ", "       XX ", "  XXXXXXX ", "  XXXXXX  ", "          "},
    {"          ", "          ", "          ", "          ", "          ", " XXXXXXXX ",
     " XXXXXXXX ", "          ", "          ", "          ", "          ", "          "},
};
constexpr int kMinus = 10;

// The colour of each number on open ground (index 1 to 8).
constexpr Colour kNumberColours[9] = {
    kBlack,       rgb(0, 3, 3), rgb(5, 2, 0), rgb(5, 0, 3), rgb(0, 0, 2),
    rgb(6, 0, 0), rgb(0, 3, 0), kBlack,       kDarkGrey,
};

/* ---- The window ------------------------------------------------------------- */

struct Level {
    int cols, rows, cacti;
};
constexpr Level kLevels[3] = {{9, 9, 10}, {16, 16, 40}, {30, 16, 99}};
constexpr const char *kLevelKeys[3] = {"small", "medium", "large"}; // in cactus.ini

constexpr uint16_t kF2 = 0x3B;

const char kHowToPlay[] =
    "Cacti hide under some of these tiles.\n"
    "Left click opens a tile. Its number says how\n"
    "many of the eight tiles around it hide one.\n"
    "Right click plants a marker on a tile you\n"
    "think hides a cactus. Clicking a number with\n"
    "all its markers placed opens the rest.\n"
    "Open every safe tile to win. F2 starts again.";

class CactusWindow : public wm::Window {
public:
    static constexpr int kTile = 16;
    static constexpr int kMargin = 8;
    static constexpr int kFaceSize = 26;
    static constexpr int kCounterW = 52, kCounterH = 20;
    static constexpr int kFieldTop = kMargin + kFaceSize + kMargin; // the sunken frame
    static constexpr int kMaxSeconds = 999;
    enum Face : uint8_t { kCalm, kCareful, kOuch, kGlad };

    CactusWindow() : Window("Cactus Patch", {0, 0, 200, 200}, &gfx::cactus_icon_16())
    {
        set_resizable(false);
        game_items_[0] = {"&New game", &CactusWindow::new_game_action, this, nullptr, nullptr, 0};
        game_items_[1] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        game_items_[2] = {"&Small", &CactusWindow::pick_small, this, nullptr, nullptr, 0};
        game_items_[3] = {"&Medium", &CactusWindow::pick_medium, this, nullptr, nullptr, 0};
        game_items_[4] = {"&Large", &CactusWindow::pick_large, this, nullptr, nullptr, 0};
        game_items_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        game_items_[6] = {"&Close", &CactusWindow::close_action, this, nullptr, nullptr, 0};
        help_items_[0] = {"&How to play", &CactusWindow::how_to_play, nullptr, nullptr, nullptr, 0};
        help_items_[1] = {"&About Chinchilla", &CactusWindow::about, nullptr, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
        // The size is remembered in C:\.config\cactus.ini: the window itself
        // is gone once it closes.
        int level = 0;
        char name[16];
        if (load_settings("cactus", settings_) && settings_.get("game", "size", name, sizeof(name))) {
            for (int i = 0; i < 3; i++) {
                if (strcmp(name, kLevelKeys[i]) == 0) {
                    level = i;
                }
            }
        }
        saved_level_ = level;
        set_level(level);
        place();
    }

    // Before the window opens: where it goes on the screen.
    void place()
    {
        const Rect f = frame();
        set_frame(centred(shell::manager(), f.w, f.h));
    }

    void paint(gfx::Canvas &c) override
    {
        const Rect all = {0, 0, client().w, client().h};
        // What needs painting, in client coordinates: tiles outside it are
        // skipped, so a change to one tile costs one tile.
        const Rect wanted = c.limit().offset(-c.origin_x(), -c.origin_y());

        // The face colour where nothing else paints: each pixel once.
        static gfx::RectList background; // one UI task, so one list will do
        background.set(all);
        background.subtract(counter_rect(0));
        background.subtract(counter_rect(1));
        background.subtract(face_rect());
        background.subtract(field_frame());
        for (int i = 0; i < background.size(); i++) {
            c.fill(background[i], kGrey);
        }

        paint_counter(c, counter_rect(0), kCactusBitmap, field_.unmarked());
        paint_counter(c, counter_rect(1), kClockBitmap, seconds_);
        paint_face(c);

        const Rect frame = field_frame();
        c.sunken(frame);
        for (int row = 0; row < field_.rows(); row++) {
            for (int col = 0; col < field_.cols(); col++) {
                if (tile_rect(col, row).overlaps(wanted)) {
                    paint_tile(c, col, row);
                }
            }
        }
    }

    void mouse(const wm::MouseMsg &m) override
    {
        const int tile = tile_at(m.x, m.y);
        switch (m.kind) {
        case wm::MouseKind::Down:
            if (face_rect().contains(m.x, m.y)) {
                face_held_ = true;
                face_inside_ = true;
            } else if (tile >= 0 && !field_.over()) {
                probing_ = true;
                held_ = tile;
            }
            break;
        case wm::MouseKind::Move:
            if (face_held_) {
                face_inside_ = face_rect().contains(m.x, m.y);
            } else if (probing_) {
                held_ = tile; // the pressed tile follows the pointer
            }
            break;
        case wm::MouseKind::Up:
            if (face_held_) {
                face_held_ = false;
                if (face_inside_) {
                    new_game();
                }
            } else if (probing_) {
                probing_ = false;
                held_ = -1;
                if (tile >= 0) {
                    const int col = tile % field_.cols(), row = tile / field_.cols();
                    if (field_.tile(col, row) <= Tile::Open8) {
                        field_.open_around(col, row);
                    } else {
                        field_.open(col, row, seed());
                    }
                }
            }
            break;
        }
        sync();
    }

    void right_click(int x, int y) override
    {
        const int tile = tile_at(x, y);
        if (tile >= 0 && !probing_) {
            field_.toggle_marker(tile % field_.cols(), tile / field_.cols());
            sync();
        }
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == kF2) {
            new_game();
            return true;
        }
        return Window::key(k);
    }

    void closed() override
    {
        // Closing ends the program: the next opening is a new game, of the
        // size last chosen.
        new_game();
        if (level_ != saved_level_) {
            saved_level_ = level_;
            settings_.set("game", "size", kLevelKeys[level_]);
            save_settings("cactus", settings_);
        }
    }

    // Tests and the reference pictures.
    CactusField &field() { return field_; }
    int seconds() const { return seconds_; }
    int level() const { return level_; }
    Rect tile_rect(int col, int row) const
    {
        return {kMargin + 2 + col * kTile, kFieldTop + 2 + row * kTile, kTile, kTile};
    }
    Rect face_rect() const { return {(client().w - kFaceSize) / 2, kMargin, kFaceSize, kFaceSize}; }
    void refresh() { sync(); }

private:
    Rect counter_rect(int which) const
    {
        const int y = kMargin + (kFaceSize - kCounterH) / 2;
        return {which == 0 ? kMargin : client().w - kMargin - kCounterW, y, kCounterW, kCounterH};
    }
    Rect field_frame() const
    {
        return {kMargin, kFieldTop, field_.cols() * kTile + 4, field_.rows() * kTile + 4};
    }
    int tile_at(int x, int y) const
    {
        const int fx = x - (kMargin + 2), fy = y - (kFieldTop + 2);
        if (fx < 0 || fy < 0 || fx >= field_.cols() * kTile || fy >= field_.rows() * kTile) {
            return -1;
        }
        return (fy / kTile) * field_.cols() + fx / kTile;
    }

    // What a tile shows now: its Tile, or kPressed while the button is held
    // on a covered one.
    static constexpr uint8_t kPressed = 0xFF;
    uint8_t look(int col, int row) const
    {
        const Tile t = field_.tile(col, row);
        if (t == Tile::Covered && held_ == row * field_.cols() + col) {
            return kPressed;
        }
        return static_cast<uint8_t>(t);
    }
    Face face() const
    {
        if (field_.state() == CactusField::State::Lost) {
            return kOuch;
        }
        if (field_.state() == CactusField::State::Won) {
            return kGlad;
        }
        return probing_ ? kCareful : kCalm;
    }
    bool face_down() const { return face_held_ && face_inside_; }

    void paint_tile(gfx::Canvas &c, int col, int row)
    {
        const Rect r = tile_rect(col, row);
        const uint8_t shown = look(col, row);
        const Rect picture = {r.x + 2, r.y + 2, 12, 12};
        if (shown == static_cast<uint8_t>(Tile::Covered) || shown == static_cast<uint8_t>(Tile::Marked)) {
            c.button_frame(r, gfx::ButtonLook::Raised);
            if (shown == static_cast<uint8_t>(Tile::Marked)) {
                c.blit_opaque(kMarkerBitmap, picture.x, picture.y, kGrey);
            } else {
                c.fill(picture, kGrey);
            }
            return;
        }
        // Open ground: a line along the top and the left, sand inside.
        c.hline(r.x, r.y, kTile, kLine);
        c.vline(r.x, r.y + 1, kTile - 1, kLine);
        const Rect inside = {r.x + 1, r.y + 1, kTile - 1, kTile - 1};
        const Colour ground = shown == static_cast<uint8_t>(Tile::CactusHit) ? kHitGround : kSand;
        if (shown >= 1 && shown <= 8) {
            const Rect digit = {r.x + 3, r.y + 2, kDigitW, kDigitH};
            c.pattern_opaque(digit.x, digit.y, kDigits[shown], kDigitH, kNumberColours[shown], ground);
            c.fill_around(inside, digit, ground);
            return;
        }
        const gfx::Bitmap *bitmap = nullptr;
        if (shown == static_cast<uint8_t>(Tile::Cactus) || shown == static_cast<uint8_t>(Tile::CactusHit)) {
            bitmap = &kCactusBitmap;
        } else if (shown == static_cast<uint8_t>(Tile::WrongMarker)) {
            bitmap = &kWrongBitmap;
        }
        if (bitmap == nullptr) {
            c.fill(inside, ground); // nothing near, or a tile held down
            return;
        }
        c.blit_opaque(*bitmap, picture.x, picture.y, ground);
        c.fill_around(inside, picture, ground);
    }

    // A sunken white box: a small picture, then three digits.
    void paint_counter(gfx::Canvas &c, const Rect &r, const gfx::Bitmap &picture, int value)
    {
        c.sunken(r);
        const Rect inside = r.inset(2);
        const int y = inside.y + 2;
        c.fill({inside.x, inside.y, inside.w, 2}, kWhite);
        c.fill({inside.x, y + kDigitH, inside.w, 2}, kWhite);
        c.fill({inside.x, y, 2, kDigitH}, kWhite);
        c.blit_opaque(picture, inside.x + 2, y, kWhite);
        c.fill({inside.x + 14, y, 2, kDigitH}, kWhite);
        int digits[3];
        if (value < 0) {
            const int v = value < -99 ? 99 : -value;
            digits[0] = kMinus;
            digits[1] = v / 10;
            digits[2] = v % 10;
        } else {
            const int v = value > 999 ? 999 : value;
            digits[0] = v / 100;
            digits[1] = v / 10 % 10;
            digits[2] = v % 10;
        }
        for (int i = 0; i < 3; i++) {
            c.pattern_opaque(inside.x + 16 + i * kDigitW, y, kDigits[digits[i]], kDigitH, kBlack, kWhite);
        }
        c.fill({inside.x + 16 + 3 * kDigitW, y, 2, kDigitH}, kWhite);
    }

    void paint_face(gfx::Canvas &c)
    {
        const Rect r = face_rect();
        const bool down = face_down();
        c.button_frame(r, down ? gfx::ButtonLook::Pressed : gfx::ButtonLook::Raised);
        const Rect inside = r.inset(2);
        const Rect picture = {inside.x + 3 + (down ? 1 : 0), inside.y + 4 + (down ? 1 : 0), 16, 16};
        c.blit_opaque(kFaceBitmaps[face()], picture.x, picture.y, kGrey);
        c.fill_around(inside, picture, kGrey);
    }

    // Asks for whatever looks different from what was last shown to be
    // painted, and keeps the seconds timer in step with the game.
    void sync()
    {
        const bool playing = field_.state() == CactusField::State::Playing;
        if (playing && timer_ == 0 && open()) {
            timer_ = shell::manager().start_timer(1000000, 1000000, &CactusWindow::second, this);
        } else if (!playing && timer_ != 0) {
            shell::manager().stop_timer(timer_);
            timer_ = 0;
        }

        Rect changed;
        int count = 0;
        for (int row = 0; row < field_.rows(); row++) {
            for (int col = 0; col < field_.cols(); col++) {
                const int i = row * field_.cols() + col;
                const uint8_t now = look(col, row);
                if (now != shown_[i]) {
                    shown_[i] = now;
                    const Rect r = tile_rect(col, row);
                    changed = gfx::bounding(changed, r);
                    if (++count <= kSeparate) {
                        separate_[count - 1] = r;
                    }
                }
            }
        }
        if (count > kSeparate) {
            invalidate_client(changed); // a large opening: one rectangle
        } else {
            for (int i = 0; i < count; i++) {
                invalidate_client(separate_[i]);
            }
        }
        const uint8_t face_now = static_cast<uint8_t>(face() | (face_down() ? 0x80 : 0));
        if (face_now != shown_face_) {
            shown_face_ = face_now;
            invalidate_client(face_rect());
        }
        if (field_.unmarked() != shown_unmarked_) {
            shown_unmarked_ = field_.unmarked();
            invalidate_client(counter_rect(0));
        }
        if (seconds_ != shown_seconds_) {
            shown_seconds_ = seconds_;
            invalidate_client(counter_rect(1));
        }
    }

    void new_game()
    {
        const Level &l = kLevels[level_];
        field_.reset(l.cols, l.rows, l.cacti);
        seconds_ = 0;
        probing_ = false;
        held_ = -1;
        games_++;
        sync();
    }

    void set_level(int level)
    {
        level_ = level;
        for (int i = 0; i < 3; i++) {
            game_items_[2 + i].flags = i == level ? wm::kMenuChecked : 0;
        }
        new_game();
        const Level &l = kLevels[level];
        const int w = 2 * kMargin + l.cols * kTile + 4 + 2 * kBorder;
        const int h = kFieldTop + l.rows * kTile + 4 + kMargin + kClientTop + kMenuBarHeight + kBorder;
        if (!open()) {
            set_frame({frame().x, frame().y, w, h});
            return;
        }
        // Stay where the window is, but inside the screen.
        const Rect area = shell::manager().work_area();
        int x = frame().x, y = frame().y;
        x = x + w > area.w ? area.w - w : x;
        y = y + h > area.h ? area.h - h : y;
        shell::manager().resize(*this, {x < 0 ? 0 : x, y < 0 ? 0 : y, w, h});
    }

    // A different field each game, and each time of day.
    uint32_t seed() const
    {
        return static_cast<uint32_t>(shell::manager().now_us()) ^ (static_cast<uint32_t>(games_) * 0x9E3779B9u);
    }

    static void second(void *context)
    {
        auto *w = static_cast<CactusWindow *>(context);
        if (w->seconds_ < kMaxSeconds) {
            w->seconds_++;
            w->sync();
        }
    }

    static void new_game_action(void *context) { static_cast<CactusWindow *>(context)->new_game(); }
    static void pick_small(void *context) { static_cast<CactusWindow *>(context)->set_level(0); }
    static void pick_medium(void *context) { static_cast<CactusWindow *>(context)->set_level(1); }
    static void pick_large(void *context) { static_cast<CactusWindow *>(context)->set_level(2); }
    static void close_action(void *context) { static_cast<CactusWindow *>(context)->close(); }
    static void how_to_play(void *) { message_box("How to play", kHowToPlay); }
    static void about(void *)
    {
        if (const App *a = find_app("About Chinchilla")) {
            launch(*a);
        }
    }

    static constexpr int kSeparate = 16; // more changed tiles than this are painted as one area

    CactusField field_;
    int level_ = 0;
    int saved_level_ = 0;
    fs::Ini settings_; // C:\.config\cactus.ini
    int seconds_ = 0;
    int games_ = 0;
    int timer_ = 0;
    bool probing_ = false; // the left button is held on the field
    int held_ = -1;        // the tile under it
    bool face_held_ = false, face_inside_ = false;

    // What the screen shows, to repaint only what changed.
    uint8_t shown_[CactusField::kMaxTiles] = {};
    uint8_t shown_face_ = 0;
    int shown_unmarked_ = 0;
    int shown_seconds_ = 0;
    Rect separate_[kSeparate];

    wm::MenuItem game_items_[7];
    wm::MenuItem help_items_[2];
    wm::Menu game_ = {game_items_, 7};
    wm::Menu help_ = {help_items_, 2};
    const wm::MenuBarEntry entries_[2] = {{"&Game", &game_}, {"&Help", &help_}};
    const wm::MenuBar bar_ = {entries_, 2};
};

CactusWindow &cactus_window() { return *one<CactusWindow>("Cactus Patch"); }

} // namespace

void open_cactus_patch()
{
    const AsProgram as("Cactus Patch");
    open_one<CactusWindow>("Cactus Patch");
}

games::CactusField &cactus_patch_field() { return cactus_window().field(); }

void cactus_patch_refresh() { cactus_window().refresh(); }

int cactus_patch_seconds() { return cactus_window().seconds(); }

} // namespace shell
