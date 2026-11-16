// Culebra: the snake game. The rules are in core/games/snake; this is its
// window: the board, the score, the keys and the timer that moves the snake.
// All of its art is drawn here, pixel by pixel, for this project.
#include <cstdio>

#include "core/games/snake.hpp"
#include "core/gfx/art.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "core/wm/wm.hpp"
#include "internal.hpp"

namespace shell {

namespace {

using games::Snake;

constexpr int kCell = 12;
constexpr int kBoardW = Snake::kCols * kCell, kBoardH = Snake::kRows * kCell;
constexpr uint16_t kF2 = 0x3B, kKeyP = 0x13;

constexpr gfx::Colour kGround = gfx::rgb(7, 7, 2);     // pale sand
constexpr gfx::Colour kGroundDot = gfx::rgb(6, 6, 1);
constexpr gfx::Colour kOutline = gfx::rgb(0, 2, 0);
constexpr gfx::Colour kScaleDark = gfx::rgb(1, 4, 0);
constexpr gfx::Colour kScaleLight = gfx::rgb(3, 6, 1);
constexpr gfx::Colour kTongue = gfx::rgb(7, 0, 0);

constexpr const char *kBerry[kCell] = {
    "............", "......LL....", ".....LK.....", "...KKKKKK...", "..KRRRRRRK..", ".KRWWRRRRRK.",
    ".KRWRRRRRRK.", ".KRRRRRRRDK.", ".KRRRRRRDDK.", "..KRRRRDDK..", "...KKKKKK...", "............",
};

gfx::Colour berry_ink(char key)
{
    switch (key) {
    case 'K':
        return gfx::rgb(2, 0, 0);
    case 'R':
        return gfx::rgb(7, 0, 0);
    case 'D':
        return gfx::rgb(5, 0, 0);
    case 'W':
        return gfx::kWhite;
    case 'L':
        return gfx::rgb(1, 5, 0);
    default:
        return kGround;
    }
}

// Whether pixel (x, y) of a cell belongs to a snake segment with these
// links: a body one pixel in from the cell's edges, reaching the edge on
// the sides that join a neighbour, with rounded corners elsewhere.
bool in_segment(int x, int y, uint8_t links)
{
    const bool up = links & 1, right = links & 2, down = links & 4, left = links & 8;
    const int last = kCell - 1;
    if (x < 0 || x > last || y < 0 || y > last) {
        // Outside the cell: the neighbouring segment, on a joined side.
        return (y < 0 && up) || (x > last && right) || (y > last && down) || (x < 0 && left);
    }
    if ((y == 0 && !up) || (x == last && !right) || (y == last && !down) || (x == 0 && !left)) {
        return false;
    }
    if ((x == 0 || x == last) && (y == 0 || y == last)) {
        return false; // the cell's own corners are never part of it
    }
    // Rounded where neither side of a corner joins a neighbour.
    const int dx = x < kCell / 2 ? x - 1 : last - 1 - x;
    const int dy = y < kCell / 2 ? y - 1 : last - 1 - y;
    const bool x_side = x < kCell / 2 ? left : right, y_side = y < kCell / 2 ? up : down;
    if (!x_side && !y_side && dx + dy < 2) {
        return false;
    }
    return true;
}

// The picture of one cell, kCell x kCell board colours.
void cell_picture(const Snake &game, int col, int row, uint8_t *out)
{
    const Snake::Cell cell = game.at(col, row);
    if (cell.kind == Snake::Kind::Berry) {
        for (int y = 0; y < kCell; y++) {
            for (int x = 0; x < kCell; x++) {
                out[y * kCell + x] = berry_ink(kBerry[y][x]);
            }
        }
        return;
    }
    for (int i = 0; i < kCell * kCell; i++) {
        out[i] = kGround;
    }
    if (cell.kind == Snake::Kind::Empty) {
        out[0] = kGroundDot;
        return;
    }
    const gfx::Colour scale = ((col + row) & 1) != 0 ? kScaleLight : kScaleDark;
    for (int y = 0; y < kCell; y++) {
        for (int x = 0; x < kCell; x++) {
            if (!in_segment(x, y, cell.links)) {
                continue;
            }
            const bool edge = !in_segment(x - 1, y, cell.links) || !in_segment(x + 1, y, cell.links) ||
                              !in_segment(x, y - 1, cell.links) || !in_segment(x, y + 1, cell.links);
            out[y * kCell + x] = edge ? kOutline : scale;
        }
    }
    if (cell.kind != Snake::Kind::Head) {
        return;
    }
    // The face, drawn looking up and turned to look away from the neck.
    int facing = static_cast<int>(game.heading());
    for (int d = 0; d < 4; d++) {
        if (cell.links == (1 << d)) {
            facing = (d + 2) & 3;
        }
    }
    struct Dot {
        int8_t x, y;
        gfx::Colour colour;
    };
    static constexpr Dot kFace[] = {
        {2, 3, gfx::kWhite}, {3, 3, gfx::kBlack}, {2, 4, gfx::kWhite}, {3, 4, gfx::kWhite},
        {8, 3, gfx::kBlack}, {9, 3, gfx::kWhite}, {8, 4, gfx::kWhite}, {9, 4, gfx::kWhite},
        {5, 0, kTongue},     {6, 0, kTongue},
    };
    const int last = kCell - 1;
    for (const Dot &dot : kFace) {
        int x = dot.x, y = dot.y;
        if (facing == 1) {
            x = last - dot.y;
            y = dot.x;
        } else if (facing == 2) {
            x = last - dot.x;
            y = last - dot.y;
        } else if (facing == 3) {
            x = dot.y;
            y = last - dot.x;
        }
        out[y * kCell + x] = dot.colour;
    }
}

// The board, in a sunken frame, with a message panel over it whenever the
// game is not running.
class BoardView : public wm::Widget {
public:
    static constexpr int kFrame = 2;
    // The message panel covers whole cells, so the cells under it are
    // simply not drawn.
    static constexpr int kPanelCol = 4, kPanelRow = 12, kPanelCols = 16, kPanelRows = 4;

    BoardView(const gfx::Rect &r, const Snake &game) : Widget(r), game_(game) {}

    static gfx::Rect cell_rect(int col, int row)
    {
        return {kFrame + col * kCell, kFrame + row * kCell, kCell, kCell};
    }

    void cell_changed(int col, int row) { invalidate_part(cell_rect(col, row)); }

    void paint(gfx::Canvas &c) override
    {
        c.sunken({0, 0, rect.w, rect.h});
        const bool panel = game_.state() != Snake::State::Playing;
        const gfx::Rect limit = c.limit().offset(-c.origin_x(), -c.origin_y());
        uint8_t pixels[kCell * kCell];
        const gfx::Bitmap picture = {kCell, kCell, pixels, -1};
        for (int row = 0; row < Snake::kRows; row++) {
            for (int col = 0; col < Snake::kCols; col++) {
                const gfx::Rect r = cell_rect(col, row);
                if (!r.overlaps(limit) || (panel && under_panel(col, row))) {
                    continue;
                }
                cell_picture(game_, col, row, pixels);
                c.blit(picture, r.x, r.y);
            }
        }
        if (panel) {
            paint_panel(c);
        }
    }

private:
    static bool under_panel(int col, int row)
    {
        return col >= kPanelCol && col < kPanelCol + kPanelCols && row >= kPanelRow &&
               row < kPanelRow + kPanelRows;
    }

    void paint_panel(gfx::Canvas &c)
    {
        const char *title = "Culebra", *hint = "Press an arrow key to start";
        switch (game_.state()) {
        case Snake::State::Paused:
            title = "Paused";
            hint = "Press Space to carry on";
            break;
        case Snake::State::Over:
            title = "Game over";
            hint = "Press Space to play again";
            break;
        case Snake::State::Won:
            title = "You filled the board!";
            hint = "Press Space to play again";
            break;
        default:
            break;
        }
        const gfx::Rect r = {kFrame + kPanelCol * kCell, kFrame + kPanelRow * kCell, kPanelCols * kCell,
                             kPanelRows * kCell};
        c.button_frame(r, gfx::ButtonLook::Raised);
        // Two centred lines; every pixel of the panel is written once.
        const gfx::Rect inside = r.inset(2);
        const int th = gfx::Canvas::text_height();
        const int tw = gfx::Canvas::text_width(title), hw = gfx::Canvas::text_width(hint);
        const gfx::Rect line1 = {inside.x + (inside.w - tw) / 2, inside.y + 8, tw, th};
        const gfx::Rect line2 = {inside.x + (inside.w - hw) / 2, inside.y + 24, hw, th};
        const gfx::Rect top = {inside.x, inside.y, inside.w, line2.y - inside.y};
        const gfx::Rect bottom = {inside.x, line2.y, inside.w, inside.bottom() - line2.y};
        c.fill_around(top, line1, gfx::kGrey);
        c.fill_around(bottom, line2, gfx::kGrey);
        c.text_opaque(line1.x, line1.y, title, gfx::kBlack, gfx::kGrey);
        c.text_opaque(line2.x, line2.y, hint, gfx::kBlack, gfx::kGrey);
    }

    const Snake &game_;
};

class CulebraWindow : public wm::Window {
public:
    static constexpr int kMargin = 6;
    static constexpr int kBoardTop = 26;
    static constexpr int kClientW = kBoardW + 2 * BoardView::kFrame + 2 * kMargin;
    static constexpr int kClientH = kBoardTop + kBoardH + 2 * BoardView::kFrame + kMargin;
    static constexpr int kSpeeds = 3;

    CulebraWindow()
        : Window("Culebra",
                 {150, 60, kClientW + 2 * kBorder, kClientH + kClientTop + kMenuBarHeight + kBorder},
                 &gfx::snake_icon_16()),
          score_label_({kMargin + 2, 6, 120, 14}, score_text_),
          best_label_({kClientW - kMargin - 80, 6, 80, 14}, best_text_),
          board_({kMargin, kBoardTop, kBoardW + 2 * BoardView::kFrame, kBoardH + 2 * BoardView::kFrame}, game_)
    {
        set_resizable(false);
        add(score_label_);
        add(best_label_);
        add(board_);
        game_items_[0] = {"&New game", &CulebraWindow::menu_new, this, nullptr, nullptr, 0};
        game_items_[1] = {"&Pause", &CulebraWindow::menu_pause, this, nullptr, nullptr, 0};
        game_items_[2] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        game_items_[3] = {"&Speed", nullptr, nullptr, &speed_menu_, nullptr, 0};
        game_items_[4] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        game_items_[5] = {"&Close", &CulebraWindow::menu_close, this, nullptr, nullptr, 0};
        speed_items_[0] = {"&Slow", &CulebraWindow::pick_slow, this, nullptr, nullptr, 0};
        speed_items_[1] = {"&Normal", &CulebraWindow::pick_normal, this, nullptr, nullptr, 0};
        speed_items_[2] = {"&Fast", &CulebraWindow::pick_fast, this, nullptr, nullptr, 0};
        help_items_[0] = {"&How to play", &CulebraWindow::menu_help, this, nullptr, nullptr, 0};
        help_items_[1] = {"&About Culebra", &CulebraWindow::menu_about, this, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
        show_score();
    }

    const Snake &game() const { return game_; }

    void new_game()
    {
        stop_timer();
        // A different game each time, the same in tests (their clock is set).
        games_++;
        game_.reset(static_cast<uint32_t>(shell::manager().now_us()) + games_ * 2654435761u);
        score_ = 0;
        show_score();
        board_.invalidate();
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (!k.pressed || (k.mods & (wm::kAlt | wm::kCtrl)) != 0) {
            return Window::key(k);
        }
        switch (k.usage) {
        case wm::key::Up:
            steer(Snake::Dir::Up);
            return true;
        case wm::key::Right:
            steer(Snake::Dir::Right);
            return true;
        case wm::key::Down:
            steer(Snake::Dir::Down);
            return true;
        case wm::key::Left:
            steer(Snake::Dir::Left);
            return true;
        case wm::key::Space:
        case kKeyP:
            pause_key();
            return true;
        case kF2:
            new_game();
            return true;
        default:
            return Window::key(k);
        }
    }

    void menu_opening(int) override
    {
        const Snake::State s = game_.state();
        game_items_[1].label = s == Snake::State::Paused ? "&Carry on" : "&Pause";
        game_items_[1].flags =
            (s == Snake::State::Playing || s == Snake::State::Paused) ? 0 : wm::kMenuDisabled;
        for (int i = 0; i < kSpeeds; i++) {
            speed_items_[i].flags = speed_ == i ? wm::kMenuChecked : 0;
        }
    }

    void closed() override
    {
        // Closed in the middle of a game: it waits, paused, for next time.
        stop_timer();
        game_.pause();
    }

private:
    // Time between steps, and points for a berry, at each speed.
    static constexpr uint32_t kStepUs[kSpeeds] = {200000, 130000, 80000};
    static constexpr int kPoints[kSpeeds] = {5, 10, 20};

    static CulebraWindow &self(void *context) { return *static_cast<CulebraWindow *>(context); }
    static void menu_new(void *context) { self(context).new_game(); }
    static void menu_pause(void *context) { self(context).pause_key(); }
    static void menu_close(void *context) { self(context).close(); }
    static void pick_slow(void *context) { self(context).set_speed(0); }
    static void pick_normal(void *context) { self(context).set_speed(1); }
    static void pick_fast(void *context) { self(context).set_speed(2); }
    static void menu_help(void *)
    {
        message_box("How to play",
                    "Steer the snake with the arrow keys and eat the\n"
                    "berries. Each one makes the snake a little longer.\n"
                    "Keep away from the walls and from your own tail.\n"
                    "\n"
                    "Space pauses the game. F2 starts a new one.");
    }
    static void menu_about(void *)
    {
        message_box("About Culebra", "Culebra, a snake game for Chinchilla.\n"
                                     "Faster games score more for each berry.");
    }

    void steer(Snake::Dir d)
    {
        if (game_.steer(d)) {
            state_changed();
        }
    }

    void pause_key()
    {
        if (game_.state() == Snake::State::Over || game_.state() == Snake::State::Won) {
            new_game();
        } else if (game_.toggle_pause()) {
            state_changed();
        }
    }

    void set_speed(int speed)
    {
        speed_ = speed;
        if (timer_ != 0) {
            stop_timer();
            state_changed();
        }
    }

    // The message panel comes or goes, and the timer runs only while the
    // snake moves.
    void state_changed()
    {
        board_.invalidate();
        if (game_.state() == Snake::State::Playing) {
            if (timer_ == 0) {
                timer_ = shell::manager().start_timer(kStepUs[speed_], kStepUs[speed_], &CulebraWindow::tick, this);
            }
        } else {
            stop_timer();
        }
    }

    void stop_timer()
    {
        if (timer_ != 0) {
            shell::manager().stop_timer(timer_);
            timer_ = 0;
        }
    }

    static void tick(void *context)
    {
        CulebraWindow &w = self(context);
        // Nobody is steering while another window or a menu is in use.
        if (!w.focused() || w.state() == wm::WindowState::Minimized || shell::manager().menu_open()) {
            if (w.game_.pause()) {
                w.state_changed();
            }
            return;
        }
        if (w.game_.step()) {
            w.score_ += kPoints[w.speed_];
            w.show_score();
        }
        for (int i = 0; i < w.game_.changed_count(); i++) {
            w.board_.cell_changed(w.game_.changed_col(i), w.game_.changed_row(i));
        }
        if (w.game_.state() != Snake::State::Playing) {
            w.state_changed();
        }
    }

    void show_score()
    {
        if (score_ > best_) {
            best_ = score_;
        }
        snprintf(score_text_, sizeof(score_text_), "Score: %d", score_);
        snprintf(best_text_, sizeof(best_text_), "Best: %d", best_);
        score_label_.set_text(score_text_);
        best_label_.set_text(best_text_);
    }

    Snake game_;
    int score_ = 0, best_ = 0;
    int speed_ = 1;
    int timer_ = 0;
    uint32_t games_ = 0;
    char score_text_[24] = "";
    char best_text_[24] = "";
    wm::Label score_label_, best_label_;
    BoardView board_;

    wm::MenuItem game_items_[6];
    wm::MenuItem speed_items_[kSpeeds];
    wm::MenuItem help_items_[2];
    wm::Menu game_menu_ = {game_items_, 6};
    wm::Menu speed_menu_ = {speed_items_, kSpeeds};
    wm::Menu help_ = {help_items_, 2};
    const wm::MenuBarEntry entries_[2] = {{"&Game", &game_menu_}, {"&Help", &help_}};
    const wm::MenuBar bar_ = {entries_, 2};
};

CulebraWindow &culebra()
{
    static CulebraWindow window;
    return window;
}

} // namespace

void open_culebra()
{
    CulebraWindow &w = culebra();
    if (!w.open() && w.game().state() != Snake::State::Paused) {
        w.new_game(); // nothing was left half played
    }
    show(w);
}

const games::Snake &culebra_game() { return culebra().game(); }

} // namespace shell
