// Chess: a game against the computer (backlog item 50). The rules and the
// engine are in core/games/chess.hpp; this is the window: the board, what
// each side has taken, the menus, and the two dialogs (the game to play,
// and what a pawn becomes). The design and the owner's choices are in
// docs/journal/2026-11-18-chess-design.md, the window's story in
// docs/journal/2026-11-18-chess-window.md.
//
// The engine thinks in a background job (os::start_job), so the window
// answers meanwhile and shows the hourglass. What the job works with is a
// block of its own, the Brain, because the window's object is destroyed
// when the window closes, job or no job.
#include "core/app/app.hpp"
#include "core/gfx/art.hpp"
#include "core/gfx/game_art.hpp"
#include "core/gfx/theme.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

#include "chess_pieces.hpp"
#include "core/games/chess.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

namespace chess = games::chess;
using chess::Color;
using chess::Move;
using chess::Status;
using chess::Type;
using gfx::Colour;
using gfx::Rect;
using gfx::rgb;

/* ---- The look --------------------------------------------------------------- */

// The measures of the approved mock-up (tools/mockup/chess_mockup.cpp).
constexpr int kSquare = 40, kFrame = 2, kMargin = 6;
constexpr int kStripH = 24;
constexpr int kBoardSide = 8 * kSquare + 2 * kFrame;
constexpr int kTopStrip = 4, kBoardTop = kTopStrip + kStripH + 2;
constexpr int kBottomStrip = kBoardTop + kBoardSide + 2;
constexpr int kStatusTop = kBottomStrip + kStripH + 4;
constexpr int kClientW = kBoardSide + 2 * kMargin, kClientH = kStatusTop + 18;
constexpr int kBoardLeft = kMargin + kFrame, kBoardY = kBoardTop + kFrame;
constexpr int kBig = chess_art::kBigSize, kSmall = chess_art::kSmallSize;
constexpr int kTakenStep = kSmall - 2; // from one captured piece to the next

// A board's colours. They are a drawing's own and stay the same in every
// theme of the desktop.
struct BoardColours {
    Colour light, dark;
    Colour moved_light, moved_dark; // the squares of the last move, and of the piece picked
    Colour dot_light, dot_dark;     // where the picked piece can go
};
constexpr BoardColours kBoards[2] = {
    // Green and white.
    {rgb(7, 7, 3), rgb(3, 4, 1), rgb(7, 7, 1), rgb(5, 6, 0), rgb(5, 5, 2), rgb(2, 3, 0)},
    // Brown and cream.
    {rgb(7, 6, 2), rgb(5, 4, 1), rgb(7, 7, 1), rgb(6, 5, 0), rgb(5, 4, 1), rgb(3, 2, 0)},
};
constexpr const char *kBoardKeys[2] = {"green", "brown"};                         // in chess.ini
constexpr const char *kSetKeys[chess_art::kSets] = {"contour", "symbols", "classic"};
constexpr const char *kSetLabels[chess_art::kSets] = {"&Contour", "&Symbols", "C&lassic"};   // in the menu
constexpr const char *kSideKeys[3] = {"white", "black", "random"};
enum Side : int { kPlayWhite, kPlayBlack, kPlayRandom };

// The two sides' inks, by what a pixel of a piece is: outline, body, detail.
// The outline is black on the board; off it, see outline_on_face().
constexpr Colour kInks[2][3] = {
    {rgb(0, 0, 0), rgb(7, 7, 3), rgb(0, 0, 0)},
    {rgb(0, 0, 0), rgb(2, 2, 1), rgb(5, 5, 2)},
};
constexpr int kClear = rgb(7, 0, 3); // magenta: no piece uses it

// One piece at (x, y), below. `scratch` holds kBig * kBig bytes and is the
// caller's: the pieces are kept two bits a pixel and coloured here. `lift`
// draws it that many pixels higher inside the same kBig by kBig: what would
// go over the top is left out, so a square's piece stays in its square.

// A colour's brightness, roughly: RRRGGGBB with green counting most.
constexpr int brightness(Colour c) { return 3 * (c >> 5) + 6 * ((c >> 2) & 7) + 2 * (c & 3); }
// The outline of a piece drawn on the window's face (the captured ones, and
// those in the dialogs): black, except for Black's pieces in a theme with a
// dark face, where a black piece with a black line around it would be lost.
Colour outline_on_face(Color side)
{
    return side == chess::Black && brightness(gfx::face()) < brightness(gfx::ink()) ? gfx::ink() : rgb(0, 0, 0);
}

void draw_piece(app::Canvas &c, uint8_t *scratch, int set, Color side, Type type, bool big, int x, int y,
                Colour outline, int lift = 0)
{
    if (type == chess::NoType) {
        return;
    }
    const int size = big ? kBig : kSmall;
    const uint8_t *packed = big ? chess_art::kBig[set][type - 1] : chess_art::kSmall[set][type - 1];
    const Colour inks[3] = {outline, kInks[side][1], kInks[side][2]};
    const int skipped = lift * size; // the rows that would go over the top
    for (int i = skipped; i < size * size; i++) {
        const int v = (packed[i >> 2] >> ((i & 3) * 2)) & 3;
        scratch[i - skipped] = v == 0 ? static_cast<uint8_t>(kClear) : inks[v - 1];
    }
    memset(scratch + size * size - skipped, kClear, static_cast<size_t>(skipped));
    c.blit({size, size, scratch, kClear}, x, y);
}

// The computer's piece lands (backlog item 64, the owner's pick of four
// drawn): it falls the last four pixels onto its square and bounces once,
// so that a move played while the user looked away is seen. How high the
// piece is above its place, a step every 60th of a second; then it rests.
constexpr uint8_t kLanding[] = {4, 4, 3, 3, 2, 1, 1, 1, 2, 2, 2, 1, 1, 1};
constexpr int kLandingSteps = static_cast<int>(sizeof(kLanding));
constexpr uint32_t kLandingStepUs = 1000000 / 60;

constexpr uint16_t kF2 = 0x3B;

const char kHowToPlay[] =
    "You play against the computer. White moves first.\n"
    "Drag a piece to where it goes, or click the piece\n"
    "and then the square. The dots show where the\n"
    "piece picked may go.\n"
    "What the computer has taken is above the board\n"
    "and what you have taken below it, with the lead\n"
    "counted in pawns.\n"
    "F2 starts another game. Ctrl+Z takes a move back.";

const char kAbout[] =
    "Chess, for Chinchilla.\n"
    "The engine that plays and the three sets of\n"
    "pieces were made for this system.";

/* ---- What the thinking job works with ---------------------------------------- */

// One block from the system, apart from the window: the job reads and
// writes it on its own task, and it must still be there if the window
// closes while the job runs. It then goes back when the job is done.
struct Brain {
    chess::Engine engine;
    chess::Position position; // a copy: the game goes on being painted
    chess::Limits limits;
    chess::Result result;
    void *table = nullptr;            // the engine's table of positions seen
    const job::Job *job = nullptr;    // while the work runs
    uint32_t polls = 0;
    std::atomic<bool> cancel{false};  // the answer is not wanted any more
    std::atomic<bool> time_up{false}; // the level's time has gone by: play the best so far
    // The UI task's.
    bool running = false;             // a job is out and has not come back
    bool orphan = false;              // the window has closed meanwhile
    app::Number window = app::kNone;
};

constexpr size_t kTableBytes = 256 * 1024;
constexpr uint32_t kJobStack = 8 * 1024; // the search took 2.3 KB on the board (board batch 8)
// The engine asks about every 256 positions, some 7 ms on the board: a rest
// of one tick about four times a second.
constexpr uint32_t kRestEvery = 32;
// Where the work is done by the caller (no tasks: the PC's plain desktop,
// the browser) the window waits for it, so it is kept short.
constexpr uint32_t kNodesWaitedFor = 200000;

void free_brain(Brain *brain)
{
    if (brain != nullptr) {
        os::release(brain->table);
        brain->~Brain();
        os::release(brain);
    }
}

/* ---- The dialogs -------------------------------------------------------------- */

class ChessWindow;
// What the dialogs tell the board (defined after ChessWindow).
void board_start_game(app::Number board, int level, int side);
void board_promote(app::Number board, Type type);

// The dialog before a game: how strong the computer plays and which side
// the user takes. Both are remembered for the next game.
class NewGameWindow : public app::Window {
public:
    static constexpr int kW = 340, kH = 250;
    NewGameWindow(app::Number board, int level, int side, int set) : board_(board), set_(set)
    {
        group({10, 8, kW - 20, 122}, "Difficulty");
        static const char *const kLevels[chess::kLevels] = {
            "&1  Beginner: plays at once, makes mistakes",
            "&2  Easy: plays at once, fewer mistakes",
            "&3  Medium: plays at once, looks further",
            "&4  Hard: thinks up to three seconds",
            "&5  Expert: thinks up to ten seconds",
        };
        for (int i = 0; i < chess::kLevels; i++) {
            radio({22, 28 + i * 19, kW - 44, 16}, kLevels[i], 0, i);
        }
        set_radio_value(0, level - 1);
        group({10, 138, kW - 20, 62}, "You play");
        // Room on the left of each choice for a picture of the piece.
        radio({50, 159, 70, 16}, "&White", 1, kPlayWhite);
        radio({154, 159, 70, 16}, "&Black", 1, kPlayBlack);
        radio({258, 159, 70, 16}, "&Random", 1, kPlayRandom);
        set_radio_value(1, side);
        label({22, 180, kW - 44, 14}, "White moves first.");
        play_ = button({kW - 172, kH - 34, 75, 23}, "&Play");
        cancel_ = button({kW - 87, kH - 34, 75, 23}, "Cancel");
        set_default(play_);
    }

    void paint(app::Canvas &c) override
    {
        draw_piece(c, scratch_, set_, chess::White, chess::King, false, 26, 156, outline_on_face(chess::White));
        draw_piece(c, scratch_, set_, chess::Black, chess::King, false, 130, 156, outline_on_face(chess::Black));
        // Random: one of each, one behind the other.
        draw_piece(c, scratch_, set_, chess::White, chess::King, false, 226, 156, outline_on_face(chess::White));
        draw_piece(c, scratch_, set_, chess::Black, chess::King, false, 236, 156, outline_on_face(chess::Black));
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close(); // as Cancel
            return true;
        }
        return false;
    }

    void command(app::Control which) override
    {
        if (which == play_) {
            // The board first: closing destroys this object.
            board_start_game(board_, radio_value(0) + 1, radio_value(1));
        }
        close();
    }

private:
    app::Number board_;
    int set_;
    app::Button play_, cancel_;
    uint8_t scratch_[kSmall * kSmall];
};

// A pawn has reached the last rank: what it becomes. Closed without a
// choice, the move is not made.
class PromoteWindow : public app::Window {
public:
    static constexpr int kButtonW = 68, kGap = 6;
    static constexpr int kW = 4 * kButtonW + 3 * kGap + 20, kH = 96;
    PromoteWindow(app::Number board, Color side, int set) : board_(board), side_(side), set_(set)
    {
        label({10, 8, kW - 20, 14}, "The pawn becomes a:");
        static const char *const kNames[4] = {"&Queen", "&Rook", "&Bishop", "K&night"};
        for (int i = 0; i < 4; i++) {
            buttons_[i] = button({10 + i * (kButtonW + kGap), kH - 33, kButtonW, 23}, kNames[i]);
        }
        set_default(buttons_[0]);
    }

    void paint(app::Canvas &c) override
    {
        for (int i = 0; i < 4; i++) {
            draw_piece(c, scratch_, set_, side_, kTypes[i], false,
                       10 + i * (kButtonW + kGap) + (kButtonW - kSmall) / 2, 30, outline_on_face(side_));
        }
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close(); // the move is not made
            return true;
        }
        return false;
    }

    void command(app::Control which) override
    {
        for (int i = 0; i < 4; i++) {
            if (which == buttons_[i]) {
                board_promote(board_, kTypes[i]);
            }
        }
        close();
    }

private:
    static constexpr Type kTypes[4] = {chess::Queen, chess::Rook, chess::Bishop, chess::Knight};
    app::Number board_;
    Color side_;
    int set_;
    app::Button buttons_[4];
    uint8_t scratch_[kSmall * kSmall];
};

/* ---- The board ---------------------------------------------------------------- */

class ChessWindow : public app::Window {
public:
    ChessWindow()
        : all_(picture({0, 0, kClientW, kStatusTop}, app::kMouse)),
          status_(plain({kMargin + 2, kStatusTop, kClientW - 2 * kMargin - 4, 14}))
    {
        game_items_[0] = {"&New game...\tF2", &ChessWindow::new_game_action, this, nullptr, nullptr, 0};
        game_items_[1] = {"&Take back\tCtrl+Z", &ChessWindow::take_back_action, this, nullptr, nullptr, 0};
        game_items_[2] = {"&Resign", &ChessWindow::resign_action, this, nullptr, nullptr, 0};
        game_items_[3] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        game_items_[4] = {"&Close", &ChessWindow::close_action, this, nullptr, nullptr, 0};
        board_items_[0] = {"&Green", &ChessWindow::pick_green, this, nullptr, nullptr, 0};
        board_items_[1] = {"&Brown", &ChessWindow::pick_brown, this, nullptr, nullptr, 0};
        void (*const kPickSet[chess_art::kSets])(void *) = {
            &ChessWindow::pick_set0, &ChessWindow::pick_set1, &ChessWindow::pick_set2};
        for (int i = 0; i < chess_art::kSets; i++) {
            piece_items_[i] = {kSetLabels[i], kPickSet[i], this, nullptr, nullptr, 0};
        }
        option_items_[0] = {"&Board", nullptr, nullptr, &board_menu_, nullptr, 0};
        option_items_[1] = {"&Pieces", nullptr, nullptr, &pieces_menu_, nullptr, 0};
        option_items_[2] = {"Show where a piece can &go", &ChessWindow::toggle_hints, this, nullptr, nullptr, 0};
        help_items_[0] = {"&How to play", &ChessWindow::how_to_play, nullptr, nullptr, nullptr, 0};
        help_items_[1] = {"&About Chess", &ChessWindow::about, nullptr, nullptr, nullptr, 0};
        set_menu_bar(&bar_);

        // What was chosen last time, from C:\.config\chess.ini.
        if (load_settings("chess", settings_)) {
            const int level = settings_.get_int("game", "level", level_);
            level_ = level >= 1 && level <= chess::kLevels ? level : level_;
            side_ = choice("game", "side", kSideKeys, 3, side_);
            board_ = choice("look", "board", kBoardKeys, 2, board_);
            set_ = choice("look", "pieces", kSetKeys, chess_art::kSets, set_);
            hints_ = settings_.get_bool("look", "dots", hints_);
        }
        pieces_ = static_cast<uint8_t *>(os::allocate(kPiecesBytes));
        colour_pieces();
        memset(shown_, 0xFF, sizeof(shown_));
        tick_menus();
        say("Choose a game.");
    }

    ~ChessWindow() override
    {
        stop_clock();
        stop_landing();
        if (pieces_ != nullptr) {
            os::release(pieces_);
        }
        if (brain_ != nullptr && brain_->running) {
            // The job still reads the block: it is told to stop, and the
            // block goes back when it has (thought()).
            brain_->orphan = true;
            brain_->cancel.store(true);
            brain_->engine.stop();
        } else {
            free_brain(brain_);
        }
    }

    void paint_in(app::Control, app::Canvas &c) override
    {
        const Rect wanted = c.limit();
        const Rect frame = {kMargin, kBoardTop, kBoardSide, kBoardSide};
        // The face around the board. The system paints this picture through
        // its strip of memory, so a pixel painted twice here is shown once.
        c.fill_around(wanted, frame, gfx::face());
        // What the computer has taken is above the board, what the user
        // has taken below.
        const Color computer = chess::other(user_);
        const int lead = game_.position().points(user_) - game_.position().points(computer);
        if (wanted.overlaps(strip_rect(kTopStrip))) {
            paint_strip(c, kTopStrip, user_, -lead);
        }
        if (wanted.overlaps(strip_rect(kBottomStrip))) {
            paint_strip(c, kBottomStrip, computer, lead);
        }
        if (!wanted.overlaps(frame)) {
            return;
        }
        c.sunken(frame);
        for (int sq = 0; sq < 64; sq++) {
            if (square_rect(sq).overlaps(wanted)) {
                paint_square(c, sq);
            }
        }
        if (lifted()) {
            const chess::Piece p = game_.position().at(picked_);
            draw_big(c, chess::color_of(p), chess::type_of(p), drag_x_ - kBig / 2, drag_y_ - kBig / 2, 0);
        }
    }

    void mouse(app::Control, const wm::MouseMsg &m) override
    {
        const int sq = square_at(m.x, m.y);
        switch (m.kind) {
        case wm::MouseKind::Down:
            if (!users_turn()) {
                break;
            }
            if (picked_ >= 0 && sq >= 0 && (targets_ >> sq) & 1) {
                user_move(picked_, sq); // the second click of two
                break;
            }
            if (sq >= 0 && sq != picked_ && own_piece(sq)) {
                pick(sq);
                again_ = false;
            } else {
                again_ = sq == picked_; // a click on the piece picked lets it go
                if (sq != picked_) {
                    pick(-1);
                }
            }
            if (picked_ >= 0) {
                held_ = true;
                moved_ = false;
                down_x_ = drag_x_ = m.x;
                down_y_ = drag_y_ = m.y;
            }
            break;
        case wm::MouseKind::Move:
            if (!held_) {
                break;
            }
            if (!moved_) {
                // A hand that trembles during a click is not a drag.
                const int dx = m.x - down_x_, dy = m.y - down_y_;
                if (dx * dx + dy * dy < 16) {
                    break;
                }
                moved_ = true;
            }
            all_.redraw(lifted_rect());
            drag_x_ = m.x;
            drag_y_ = m.y;
            all_.redraw(lifted_rect());
            break;
        case wm::MouseKind::Up:
            if (!held_) {
                break;
            }
            held_ = false;
            if (moved_) {
                moved_ = false;
                all_.redraw(lifted_rect());
                if (sq >= 0 && (targets_ >> sq) & 1) {
                    user_move(picked_, sq);
                    return;
                }
                // Let go anywhere else, the piece goes home and stays picked.
            } else if (again_) {
                pick(-1);
            }
            break;
        }
        sync();
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (!k.pressed) {
            return false;
        }
        if (k.usage == kF2) {
            ask_new_game();
            return true;
        }
        if (k.usage == wm::key::Z && (k.mods & wm::kCtrl) != 0) {
            take_back();
            return true;
        }
        return false;
    }

    void menu_opening(int which) override
    {
        if (which == 0) {
            game_items_[1].flags = can_take_back() ? 0 : wm::kMenuDisabled;
            game_items_[2].flags = playing_ ? 0 : wm::kMenuDisabled;
        }
    }

    // The dialog before a game. Called once the window is on the screen,
    // by F2 and by the Game menu.
    void ask_new_game()
    {
        app::Look look;
        look.title = "New game";
        look.width = NewGameWindow::kW;
        look.height = NewGameWindow::kH;
        look.flags = app::kDialog | app::kClientSize | app::kPicture;
        look.over = number();
        app::open<NewGameWindow>(look, number(), level_, side_, set_);
    }

    // From the dialog. `side` is a Side: random is settled here.
    void start_game(int level, int side)
    {
        level_ = level >= 1 && level <= chess::kLevels ? level : level_;
        side_ = side >= kPlayWhite && side <= kPlayRandom ? side : side_;
        settings_.set_int("game", "level", level_);
        settings_.set("game", "side", kSideKeys[side_]);
        save_settings("chess", settings_);

        forget_answer();
        games_++;
        user_ = side_ == kPlayRandom ? static_cast<Color>(seed() >> 7 & 1) : static_cast<Color>(side_);
        game_.reset();
        fresh_ = true;
        playing_ = true;
        resigned_ = false;
        held_ = moved_ = false;
        pick(-1);
        turn();
    }

    // From the dialog: the pawn waiting on the last rank becomes `type`.
    void promote(Type type)
    {
        if (promote_from_ >= 0 && users_turn()) {
            play_user(chess::move(promote_from_, promote_to_, type));
        }
        promote_from_ = promote_to_ = -1;
    }

    // Tests and the reference pictures.
    chess::Game &game() { return game_; }
    bool thinking() const { return thinking_; }
    Color user() const { return user_; }
    int level() const { return level_; }
    const char *status() const { return text_; }
    // After the game was changed from outside: whose turn it is, afresh.
    void refresh()
    {
        forget_answer();
        pick(-1);
        playing_ = true;
        resigned_ = false;
        turn();
    }

private:
    /* ---- Where things are ---- */

    // A square's place in the picture: the user's side is at the bottom.
    Rect square_rect(int sq) const
    {
        const int file = chess::file_of(sq), rank = chess::rank_of(sq);
        const int col = user_ == chess::White ? file : 7 - file;
        const int row = user_ == chess::White ? 7 - rank : rank;
        return {kBoardLeft + col * kSquare, kBoardY + row * kSquare, kSquare, kSquare};
    }
    int square_at(int x, int y) const
    {
        const int bx = x - kBoardLeft, by = y - kBoardY;
        if (bx < 0 || by < 0 || bx >= 8 * kSquare || by >= 8 * kSquare) {
            return -1;
        }
        const int col = bx / kSquare, row = by / kSquare;
        return user_ == chess::White ? chess::square(col, 7 - row) : chess::square(7 - col, row);
    }
    static Rect strip_rect(int top) { return {0, top, kClientW, kStripH}; }
    // The piece in the hand: whether there is one, and where it is painted.
    bool lifted() const { return held_ && moved_ && picked_ >= 0; }
    Rect lifted_rect() const { return {drag_x_ - kBig / 2, drag_y_ - kBig / 2, kBig, kBig}; }

    /* ---- Painting ---- */

    // What a square shows, in one byte, so that only squares that changed
    // are painted again: the piece, and bits for the yellow of a move, the
    // dot and the piece being away in the user's hand.
    uint8_t look(int sq) const
    {
        uint8_t v = game_.position().at(sq);
        const Move last = game_.last();
        if (sq == picked_ || (last != chess::kNoMove && (sq == chess::from_of(last) || sq == chess::to_of(last)))) {
            v |= 0x20;
        }
        if (hints_ && picked_ >= 0 && (targets_ >> sq) & 1) {
            v |= 0x40;
        }
        if (sq == picked_ && lifted()) {
            v |= 0x80;
        }
        return v;
    }

    // The board's pieces are coloured once, when the window opens and when
    // another set is chosen, not each time one is painted: the twelve of
    // them, kBig by kBig each, in memory the window gives back when it
    // closes. Without that memory each is coloured as it is painted.
    static constexpr size_t kPiecesBytes = 2 * 6 * kBig * kBig;
    void colour_pieces()
    {
        if (pieces_ == nullptr) {
            return;
        }
        for (int side = 0; side < 2; side++) {
            for (int type = 0; type < 6; type++) {
                const uint8_t *packed = chess_art::kBig[set_][type];
                const Colour inks[3] = {gfx::rgb(0, 0, 0), kInks[side][1], kInks[side][2]};
                uint8_t *out = pieces_ + (side * 6 + type) * kBig * kBig;
                for (int i = 0; i < kBig * kBig; i++) {
                    const int v = (packed[i >> 2] >> ((i & 3) * 2)) & 3;
                    out[i] = v == 0 ? static_cast<uint8_t>(kClear) : inks[v - 1];
                }
            }
        }
    }
    // A piece of the board at (x, y); `lift` as draw_piece has it.
    void draw_big(app::Canvas &c, Color side, Type type, int x, int y, int lift)
    {
        if (type == chess::NoType) {
            return;
        }
        if (pieces_ == nullptr) {
            draw_piece(c, scratch_, set_, side, type, true, x, y, gfx::rgb(0, 0, 0), lift);
            return;
        }
        // Lifted, the piece's top rows are left out and it starts higher
        // in its picture: the same pixels draw_piece gives.
        const uint8_t *pixels = pieces_ + (side * 6 + (type - 1)) * kBig * kBig;
        c.blit({kBig, kBig - lift, pixels + lift * kBig, kClear}, x, y);
    }

    void paint_square(app::Canvas &c, int sq)
    {
        const BoardColours &k = kBoards[board_];
        const Rect r = square_rect(sq);
        const int file = chess::file_of(sq), rank = chess::rank_of(sq);
        const bool dark = ((file + rank) & 1) == 0;
        const uint8_t shown = look(sq);
        c.fill(r, (shown & 0x20) != 0 ? (dark ? k.moved_dark : k.moved_light) : dark ? k.dark : k.light);
        const chess::Piece p = game_.position().at(sq);
        if (p != chess::kEmpty && (shown & 0x80) == 0) {
            draw_big(c, chess::color_of(p), chess::type_of(p), r.x, r.y, sq == landing_ ? lift_ : 0);
        }
        if ((shown & 0x40) != 0) {
            const Colour dot = dark ? k.dot_dark : k.dot_light;
            if (p == chess::kEmpty) {
                static const int8_t kHalf[12] = {2, 4, 5, 5, 6, 6, 6, 6, 5, 5, 4, 2}; // a disc 12 across
                for (int i = 0; i < 12; i++) {
                    c.hline(r.x + kSquare / 2 - kHalf[i], r.y + kSquare / 2 - 6 + i, 2 * kHalf[i], dot);
                }
            } else {
                // A piece that can be taken: the four corners of its
                // square, so that the mark does not sit on the piece.
                constexpr int kCorner = 8;
                for (int i = 0; i < kCorner; i++) {
                    const int w = kCorner - i;
                    c.hline(r.x, r.y + i, w, dot);
                    c.hline(r.x + kSquare - w, r.y + i, w, dot);
                    c.hline(r.x, r.y + kSquare - 1 - i, w, dot);
                    c.hline(r.x + kSquare - w, r.y + kSquare - 1 - i, w, dot);
                }
            }
        }
        // The board's letters and numbers, inside the squares on two of
        // its edges, in the other squares' colour.
        const Colour note = dark ? k.light : k.dark;
        if (r.x == kBoardLeft) {
            const char text[2] = {static_cast<char>('1' + rank), 0};
            c.text(r.x + 2, r.y + 1, text, note, true);
        }
        if (r.y == kBoardY + 7 * kSquare) {
            const char text[2] = {static_cast<char>('a' + file), 0};
            c.text(r.x + kSquare - 8, r.y + kSquare - 13, text, note, true);
        }
    }

    // A row of the pieces `of` has lost, the most valuable first, evenly
    // spaced, and by how many pawns the side that took them leads. All
    // fifteen a side can lose fit.
    void paint_strip(app::Canvas &c, int top, Color of, int lead)
    {
        static constexpr Type kOrder[5] = {chess::Queen, chess::Rook, chess::Bishop, chess::Knight, chess::Pawn};
        int x = kMargin + 2;
        for (Type type : kOrder) {
            for (int i = 0; i < game_.taken(of, type); i++) {
                draw_piece(c, scratch_, set_, of, type, false, x, top + 2, outline_on_face(of));
                x += kTakenStep;
            }
        }
        if (lead > 0) {
            char text[16];
            snprintf(text, sizeof(text), "+%d", lead);
            c.text(x + 3, top + 7, text, gfx::ink(), false);
        }
    }

    // Everything the two strips show, as one number.
    uint32_t strips() const
    {
        uint32_t v = static_cast<uint32_t>(game_.position().points(chess::White) -
                                           game_.position().points(chess::Black) + 64);
        for (int c = 0; c < 2; c++) {
            for (int t = chess::Pawn; t <= chess::Queen; t++) {
                v = v * 9 + static_cast<uint32_t>(game_.taken(static_cast<Color>(c), static_cast<Type>(t)));
            }
        }
        return v;
    }

    // Asks for whatever looks different from what was last shown to be
    // painted again.
    void sync()
    {
        settle_if_moved_on();
        for (int sq = 0; sq < 64; sq++) {
            const uint8_t now = look(sq);
            if (now != shown_[sq]) {
                shown_[sq] = now;
                all_.redraw(square_rect(sq));
            }
        }
        const uint32_t strips_now = strips();
        if (strips_now != shown_strips_) {
            shown_strips_ = strips_now;
            all_.redraw(strip_rect(kTopStrip));
            all_.redraw(strip_rect(kBottomStrip));
        }
    }

    // The board turned, or another look chosen: all of it again.
    void redraw_all()
    {
        settle_if_moved_on();
        for (int sq = 0; sq < 64; sq++) {
            shown_[sq] = look(sq);
        }
        shown_strips_ = strips();
        all_.redraw();
    }

    void say(const char *text)
    {
        snprintf(text_, sizeof(text_), "%s", text);
        status_.set(text_);
    }

    /* ---- The computer's piece landing ---- */

    // The piece the computer has just played to `sq` starts above its place.
    // A timer brings it down; only that square is painted again, and only
    // when the height changes.
    void start_landing(int sq)
    {
        stop_landing();
        landing_ = sq;
        landed_ply_ = game_.plies();
        landing_since_ = os::now_us();
        lift_ = kLanding[0];
        shown_[sq] = 0xFF; // painted again by the sync() that shows the move
        landing_timer_ = os::start_timer(kLandingStepUs, kLandingStepUs, &ChessWindow::landing_tick, this);
        if (landing_timer_ == 0) {
            stop_landing(); // no timer to be had: the piece is simply there
        }
    }

    static void landing_tick(void *context)
    {
        auto *w = static_cast<ChessWindow *>(context);
        // By the clock, not by counting: a late tick skips steps and the
        // landing takes as long as it should.
        const uint64_t step = (os::now_us() - w->landing_since_) / kLandingStepUs;
        if (step >= static_cast<uint64_t>(kLandingSteps)) {
            w->stop_landing();
            return;
        }
        if (kLanding[step] != w->lift_) {
            w->lift_ = kLanding[step];
            w->all_.redraw(w->square_rect(w->landing_));
        }
    }

    // The piece is at rest, now.
    void stop_landing()
    {
        if (landing_timer_ != 0) {
            os::stop_timer(landing_timer_);
            landing_timer_ = 0;
        }
        if (landing_ >= 0 && lift_ != 0) {
            all_.redraw(square_rect(landing_));
        }
        landing_ = -1;
        lift_ = 0;
    }

    // The game went on, or back, while the piece was landing: a move, Take
    // back, another game.
    void settle_if_moved_on()
    {
        if (landing_ >= 0 && game_.plies() != landed_ply_) {
            stop_landing();
        }
    }

    /* ---- The user's moves ---- */

    bool users_turn() const { return playing_ && !thinking_ && game_.position().side() == user_; }
    bool own_piece(int sq) const
    {
        const chess::Piece p = game_.position().at(sq);
        return p != chess::kEmpty && chess::color_of(p) == user_;
    }

    // Picks a piece up (-1: none), and works out where it may go.
    void pick(int sq)
    {
        picked_ = sq;
        targets_ = 0;
        if (sq < 0) {
            held_ = moved_ = false;
            return;
        }
        Move moves[chess::kMaxMoves];
        const int n = game_.legal_moves(moves);
        for (int i = 0; i < n; i++) {
            if (chess::from_of(moves[i]) == sq) {
                targets_ |= 1ull << chess::to_of(moves[i]);
            }
        }
    }

    // The user's piece goes from one square to another. A pawn reaching the
    // last rank asks what it becomes first.
    void user_move(int from, int to)
    {
        const chess::Piece p = game_.position().at(from);
        const int last_rank = user_ == chess::White ? 7 : 0;
        held_ = moved_ = false;
        if (chess::type_of(p) == chess::Pawn && chess::rank_of(to) == last_rank) {
            promote_from_ = from;
            promote_to_ = to;
            pick(-1);
            sync();
            app::Look look;
            look.title = "Chess";
            look.width = PromoteWindow::kW;
            look.height = PromoteWindow::kH;
            look.flags = app::kDialog | app::kClientSize | app::kPicture;
            look.over = number();
            app::open<PromoteWindow>(look, number(), user_, set_);
            return;
        }
        play_user(chess::move(from, to));
    }

    void play_user(Move m)
    {
        pick(-1);
        if (game_.play(m)) {
            turn();
        } else {
            sync();
        }
    }

    /* ---- The game ---- */

    // After every move, and after a game starts: is it over, and if not,
    // whose turn is it.
    void turn()
    {
        sync();
        const Status status = game_.status();
        if (status != Status::Playing) {
            playing_ = false;
            set_hourglass(false);
            const bool user_lost = game_.position().side() == user_;
            switch (status) {
            case Status::Checkmate: say(user_lost ? "Checkmate. The computer wins." : "Checkmate. You win!"); break;
            case Status::Stalemate: say("Stalemate: a draw."); break;
            case Status::DrawRepetition: say("A draw: the same position three times."); break;
            case Status::DrawFifty: say("A draw: fifty moves with nothing taken."); break;
            default: say("A draw: not enough pieces left to mate."); break;
            }
            return;
        }
        if (game_.position().side() == user_) {
            set_hourglass(false);
            say(game_.position().in_check() ? "Check. Your move." : "Your move.");
            return;
        }
        think();
    }

    // The computer's turn: the engine thinks in a job, on a copy of the
    // position, and thought() plays what it found.
    void think()
    {
        thinking_ = true;
        set_hourglass(true);
        say("The computer is thinking...");
        if (brain_ != nullptr && brain_->running) {
            return; // an answer nobody wants is still on its way: thought() comes back here
        }
        if (brain_ == nullptr) {
            void *block = os::allocate(sizeof(Brain));
            if (block == nullptr) {
                cannot_think();
                return;
            }
            brain_ = new (block) Brain();
            brain_->window = number();
            // Without the table the engine plays the same, a little slower.
            brain_->table = os::allocate(kTableBytes);
            brain_->engine.set_table(brain_->table, brain_->table != nullptr ? kTableBytes : 0);
        }
        Brain &b = *brain_;
        if (fresh_) {
            fresh_ = false;
            b.engine.new_game();
        }
        b.position = game_.position();
        b.limits = chess::limits_for(level_, b.position, seed());
        // The engine is given no clock (a job cannot ask the time): a timer
        // of the window's says when the level's time is up.
        const uint32_t time_ms = b.limits.time_ms;
        b.limits.time_ms = 0;
        b.cancel.store(false);
        b.time_up.store(false);
        b.polls = 0;
        b.result = chess::Result();
        b.running = true;
        clock_ = os::start_timer(time_ms * 1000u, 0, &ChessWindow::time_is_up, this);

        job::Spec spec;
        spec.name = "chess";
        spec.work = &ChessWindow::work;
        spec.done = &ChessWindow::thought;
        spec.context = brain_;
        spec.stack_bytes = kJobStack;
        // Below the desktop and everything that answers someone: it takes
        // the processor only when nothing else wants it.
        spec.priority = ports::Priority::Low;
        // Last: where there are no tasks the work is done, and thought()
        // called, before this returns.
        if (!os::start_job(spec)) {
            b.running = false;
            cannot_think();
        }
    }

    void cannot_think()
    {
        thinking_ = false;
        stop_clock();
        set_hourglass(false);
        say("Not enough memory for the computer to think.");
    }

    // The job: on its own task. It touches the Brain and nothing else.
    static uint32_t work(const job::Job &job, void *context)
    {
        Brain &b = *static_cast<Brain *>(context);
        b.job = &job;
        if (!job.own_task()) {
            b.limits.nodes = kNodesWaitedFor;
        }
        chess::Hooks hooks;
        hooks.poll = &ChessWindow::poll;
        hooks.context = &b;
        b.result = b.engine.think(b.position, b.limits, hooks);
        return 0;
    }

    static bool poll(void *context)
    {
        Brain &b = *static_cast<Brain *>(context);
        if (b.cancel.load() || b.time_up.load() || b.job->stopping()) {
            return true;
        }
        if (++b.polls % kRestEvery == 0) {
            os::job_rest(1);
        }
        return false;
    }

    // Where the program's events run, when the job has returned.
    static void thought(void *context, uint32_t, bool)
    {
        Brain *b = static_cast<Brain *>(context);
        b->running = false;
        if (b->orphan) {
            free_brain(b); // the window closed while the engine thought
            return;
        }
        if (ChessWindow *w = app::program<ChessWindow>(b->window)) {
            w->answer();
        }
    }

    void answer()
    {
        stop_clock();
        if (brain_->cancel.load()) {
            // The game changed while the engine thought. If it is the
            // computer's turn in the game there is now, it thinks again.
            if (thinking_) {
                think();
            }
            return;
        }
        thinking_ = false;
        if (brain_->result.best != chess::kNoMove && game_.play(brain_->result.best)) {
            pick(-1);
            start_landing(chess::to_of(brain_->result.best));
        }
        turn();
    }

    static void time_is_up(void *context)
    {
        auto *w = static_cast<ChessWindow *>(context);
        w->clock_ = 0; // it fired once and is gone
        if (w->brain_ != nullptr && w->brain_->running) {
            w->brain_->time_up.store(true);
        }
    }

    void stop_clock()
    {
        if (clock_ != 0) {
            os::stop_timer(clock_);
            clock_ = 0;
        }
    }

    // The game is about to change under the engine: what it is working out
    // is not wanted.
    void forget_answer()
    {
        if (brain_ != nullptr && brain_->running) {
            brain_->cancel.store(true);
            brain_->engine.stop();
        }
        thinking_ = false;
        stop_clock();
        set_hourglass(false);
    }

    // Take back: the user's last move, with the computer's answer to it if
    // there was one. Also while the computer thinks, and after the end.
    int take_back_plies() const
    {
        if (resigned_) {
            return 0; // nothing was played: the game simply goes on
        }
        return game_.position().side() == user_ ? 2 : 1;
    }
    bool can_take_back() const
    {
        return games_ > 0 && (resigned_ || game_.plies() >= take_back_plies()) && game_.plies() <= chess::Game::kMaxKept;
    }
    void take_back()
    {
        if (!can_take_back()) {
            return;
        }
        forget_answer();
        const int plies = take_back_plies();
        if (plies > 0 && !game_.take_back(plies)) {
            return;
        }
        playing_ = true;
        resigned_ = false;
        pick(-1);
        turn();
    }

    void resign()
    {
        if (!playing_) {
            return;
        }
        forget_answer();
        playing_ = false;
        resigned_ = true;
        pick(-1);
        sync();
        say("You resigned. The computer wins.");
    }

    /* ---- Options ---- */

    // One of `count` words of the settings file, as its place in the list.
    int choice(const char *section, const char *key, const char *const *words, int count, int fallback) const
    {
        char word[16];
        if (settings_.get(section, key, word, sizeof(word))) {
            for (int i = 0; i < count; i++) {
                if (strcmp(word, words[i]) == 0) {
                    return i;
                }
            }
        }
        return fallback;
    }

    void tick_menus()
    {
        for (int i = 0; i < 2; i++) {
            board_items_[i].flags = i == board_ ? wm::kMenuChecked : 0;
        }
        for (int i = 0; i < chess_art::kSets; i++) {
            piece_items_[i].flags = i == set_ ? wm::kMenuChecked : 0;
        }
        option_items_[2].flags = hints_ ? wm::kMenuChecked : 0;
    }

    void set_look(int board, int set, bool hints)
    {
        board_ = board;
        set_ = set;
        hints_ = hints;
        colour_pieces();
        tick_menus();
        settings_.set("look", "board", kBoardKeys[board_]);
        settings_.set("look", "pieces", kSetKeys[set_]);
        settings_.set_bool("look", "dots", hints_);
        save_settings("chess", settings_);
        redraw_all();
    }

    // Another number each game, each move and each time of day: the engine
    // draws its mistakes and its openings from it, and Random its side.
    uint32_t seed() const
    {
        return static_cast<uint32_t>(os::now_us()) ^ (static_cast<uint32_t>(games_) * 0x9E3779B9u) ^
               (static_cast<uint32_t>(game_.plies()) * 0x85EBCA6Bu);
    }

    static ChessWindow *self(void *context) { return static_cast<ChessWindow *>(context); }
    static void new_game_action(void *context) { self(context)->ask_new_game(); }
    static void take_back_action(void *context) { self(context)->take_back(); }
    static void resign_action(void *context) { self(context)->resign(); }
    static void close_action(void *context) { self(context)->close(); }
    static void pick_green(void *context) { self(context)->set_look(0, self(context)->set_, self(context)->hints_); }
    static void pick_brown(void *context) { self(context)->set_look(1, self(context)->set_, self(context)->hints_); }
    static void pick_set0(void *context) { self(context)->set_look(self(context)->board_, 0, self(context)->hints_); }
    static void pick_set1(void *context) { self(context)->set_look(self(context)->board_, 1, self(context)->hints_); }
    static void pick_set2(void *context) { self(context)->set_look(self(context)->board_, 2, self(context)->hints_); }
    static void toggle_hints(void *context)
    {
        self(context)->set_look(self(context)->board_, self(context)->set_, !self(context)->hints_);
    }
    static void how_to_play(void *) { message_box("How to play", kHowToPlay); }
    static void about(void *) { message_box("About Chess", kAbout); }

    chess::Game game_;
    Brain *brain_ = nullptr; // made for the first move the computer thinks about
    app::Picture all_;
    app::Plain status_;
    fs::Ini settings_; // C:\.config\chess.ini

    int level_ = 3;             // 1 to 5
    int side_ = kPlayRandom;    // as asked for in the dialog
    int board_ = 0, set_ = 0;   // places in kBoards and in the sets of pieces
    bool hints_ = true;         // dots where the picked piece can go
    Color user_ = chess::White; // in the game there is
    int games_ = 0;             // started since the window opened
    bool playing_ = false;      // a game is on and not over
    bool resigned_ = false;
    bool thinking_ = false;     // the game waits for the computer's move
    bool fresh_ = true;         // the engine has not been told of this game yet
    int clock_ = 0;             // the timer that says the level's time is up

    int landing_ = -1;           // the square the computer's piece is landing on, or -1
    int lift_ = 0;               // how many pixels above its place it is painted
    int landing_timer_ = 0;
    int landed_ply_ = 0;         // the game's length when it was played
    uint64_t landing_since_ = 0;

    int picked_ = -1;      // the square of the piece picked, or -1
    uint64_t targets_ = 0; // the squares it may go to, a bit each
    bool held_ = false;    // the button is down on the piece picked
    bool moved_ = false;   // and has moved: the piece is in the hand
    bool again_ = false;   // the press was on the piece already picked
    int down_x_ = 0, down_y_ = 0, drag_x_ = 0, drag_y_ = 0;
    int promote_from_ = -1, promote_to_ = -1; // the pawn the dialog asks about

    // What the screen shows, to paint again only what changed.
    uint8_t shown_[64];
    uint32_t shown_strips_ = 0;
    char text_[48] = "";
    uint8_t scratch_[kBig * kBig]; // a piece being coloured
    uint8_t *pieces_ = nullptr;    // the board's twelve, coloured (colour_pieces)

    wm::MenuItem game_items_[5];
    wm::MenuItem board_items_[2];
    wm::MenuItem piece_items_[chess_art::kSets];
    wm::MenuItem option_items_[3];
    wm::MenuItem help_items_[2];
    wm::Menu game_menu_ = {game_items_, 5};
    wm::Menu board_menu_ = {board_items_, 2};
    wm::Menu pieces_menu_ = {piece_items_, chess_art::kSets};
    wm::Menu options_ = {option_items_, 3};
    wm::Menu help_ = {help_items_, 2};
    const wm::MenuBarEntry entries_[3] = {{"&Game", &game_menu_}, {"&Options", &options_}, {"&Help", &help_}};
    const wm::MenuBar bar_ = {entries_, 3};
};

void board_start_game(app::Number board, int level, int side)
{
    if (ChessWindow *w = app::program<ChessWindow>(board)) {
        w->start_game(level, side);
    }
}

void board_promote(app::Number board, Type type)
{
    if (ChessWindow *w = app::program<ChessWindow>(board)) {
        w->promote(type);
    }
}

app::Number open_window(const gfx::Bitmap *icon)
{
    app::Look look;
    look.title = "Chess";
    look.icon = icon;
    look.width = kClientW;
    look.height = kClientH;
    look.flags = app::kClientSize | app::kFixedSize | app::kMenuBar;
    const app::Number number = app::open<ChessWindow>(look);
    if (number != app::kNone) {
        // A game is chosen before it is played.
        app::program<ChessWindow>(number)->ask_new_game();
    }
    return number;
}

#ifndef APP_FILE
app::Number g_window = app::kNone; // the one there may be: a number, dead once it has closed

// For the tests and the reference pictures, while Chess is open.
ChessWindow &chess_window() { return *app::program<ChessWindow>(g_window); }
#endif

} // namespace

#ifdef APP_FILE
} // namespace shell

// Chess kept in a file (backlog item 27; programs/chess): on the board it
// is not part of the firmware. 6 KB of stack for its task: the rules list
// a position's moves on it (half a kilobyte, twice over).
APP_INFO(app::file_info("Chess", 6 * 1024, gfx::game_art::kChess16, gfx::game_art::kChessInks,
                        gfx::game_art::kChess32, gfx::game_art::kChessInks));

namespace {
// The window's icon: the header's own pixels are not in memory with the
// program, so they are made again here from the same rows.
constexpr app::Icon16 kWindowIcon = app::icon16(gfx::game_art::kChess16, gfx::game_art::kChessInks);
} // namespace

APP_MAIN(int, char **)
{
    static const gfx::Bitmap icon = {16, 16, kWindowIcon.pixels, app::kClearInk};
    return shell::open_window(&icon) != app::kNone ? 0 : 1;
}
#else
void open_chess()
{
    const AsProgram as("Chess");
    if (app::bring_forward(g_window)) {
        return;
    }
    g_window = open_window(&gfx::chess_icon_16());
    if (g_window == app::kNone) {
        no_memory_for("Chess");
    }
}

games::chess::Game &chess_game() { return chess_window().game(); }
bool chess_thinking() { return chess_window().thinking(); }
games::chess::Color chess_user() { return chess_window().user(); }
const char *chess_status() { return chess_window().status(); }
void chess_refresh() { chess_window().refresh(); }

} // namespace shell
#endif
