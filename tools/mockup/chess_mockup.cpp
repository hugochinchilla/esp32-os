// Draws the proposed look of the chess game (backlog item 50) for the owner
// to choose from: the board in both colour sets, the captured pieces, the
// hourglass while the engine thinks, and the dialog before a game. It is a
// mock-up, not the game: there are no rules and no engine here, only fixed
// positions. The windows, menus and controls are the system's real ones
// (core/), running on the PC as in host/tests/render_scenes.cpp, so what is
// drawn is what the board would show.
//
//     python3 tools/mockup/chess_pieces.py header build/host/chess_pieces.inc
//     cmake --build build/host --target chess_mockup
//     build/host/chess_mockup <folder>        writes <folder>/*.ppm
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/app/app.hpp"
#include "core/desk/desk.hpp"
#include "core/fs/memory_volume.hpp"
#include "core/gfx/theme.hpp"
#include "core/init/init.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "../../core/src/shell/internal.hpp"
#include "../../host/host_adapters.hpp"

namespace {

#include "chess_pieces.inc"

constexpr int kSquare = 40, kFrame = 2, kMargin = 6;
constexpr int kStripH = 24;
constexpr int kBoardSide = 8 * kSquare + 2 * kFrame;
constexpr int kTopStrip = 4, kBoardTop = kTopStrip + kStripH + 2;
constexpr int kBottomStrip = kBoardTop + kBoardSide + 2;
constexpr int kStatusTop = kBottomStrip + kStripH + 4;
constexpr int kClientW = kBoardSide + 2 * kMargin, kClientH = kStatusTop + 18;

struct Colours {
    gfx::Colour light, dark;
    gfx::Colour moved_light, moved_dark; // the squares of the last move
    gfx::Colour dot_light, dot_dark;     // where the picked piece can go
};
constexpr Colours kGreen = {gfx::kWhite,       gfx::rgb(3, 4, 1), gfx::rgb(7, 7, 1),
                            gfx::rgb(5, 6, 0), gfx::rgb(5, 5, 2), gfx::rgb(2, 3, 0)};
constexpr Colours kBrown = {gfx::rgb(7, 6, 2), gfx::rgb(5, 4, 1), gfx::rgb(7, 7, 1),
                            gfx::rgb(6, 5, 0), gfx::rgb(5, 4, 1), gfx::rgb(3, 2, 0)};

// The two sides' inks: outline, body, detail.
constexpr gfx::Colour kWhiteInks[3] = {gfx::kBlack, gfx::kWhite, gfx::kBlack};
constexpr gfx::Colour kBlackInks[3] = {gfx::kBlack, gfx::rgb(2, 2, 1), gfx::rgb(5, 5, 2)};

int piece_index(char letter)
{
    const char *order = "pnbrqk";
    const char lower = static_cast<char>(letter | 0x20);
    return static_cast<int>(strchr(order, lower) - order);
}

// One piece; capitals are White's, as in the usual notation.
void draw_piece(app::Canvas &c, int x, int y, char letter, bool big)
{
    const int size = big ? kBigSize : kSmallSize;
    const char *const *rows = big ? kBig[piece_index(letter)] : kSmall[piece_index(letter)];
    const gfx::Colour *inks = letter < 'a' ? kWhiteInks : kBlackInks;
    static uint8_t pixels[kBigSize * kBigSize];
    constexpr int kClear = 0xE3; // a pink no piece uses
    for (int row = 0; row < size; row++) {
        for (int col = 0; col < size; col++) {
            const char ch = rows[row][col];
            pixels[row * size + col] =
                ch == ' ' ? kClear : ch == 'X' ? inks[0] : ch == 'o' ? inks[1] : inks[2];
        }
    }
    c.blit({size, size, pixels, kClear}, x, y);
}

struct Scene {
    const char *rows[8];    // rank 8 first, '.' empty
    bool user_is_white;
    const Colours *colours;
    const char *taken_by_user, *taken_by_engine; // the pieces, most valuable first
    int lead;               // of the user in pawns; negative: of the engine
    const char *moved;      // the last move's two squares, "b3b7", or ""
    const char *picked;     // the square of the piece held, or ""
    const char *targets;    // where it can go, "b4b6c3..."
    const char *status;
};

class ChessWindow : public app::Window {
public:
    explicit ChessWindow(const Scene &scene)
        : scene_(scene), all_(picture({0, 0, kClientW, kStatusTop})), status_(plain({kMargin + 2, kStatusTop, 300, 14}))
    {
        game_items_[0] = {"&New game...\tF2", nullptr, nullptr, nullptr, nullptr, 0};
        game_items_[1] = {"&Take back\tCtrl+Z", nullptr, nullptr, nullptr, nullptr, 0};
        game_items_[2] = {"&Resign", nullptr, nullptr, nullptr, nullptr, 0};
        game_items_[3] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        game_items_[4] = {"&Close", nullptr, nullptr, nullptr, nullptr, 0};
        board_items_[0] = {"&Green", nullptr, nullptr, nullptr, nullptr,
                           static_cast<uint8_t>(scene.colours == &kGreen ? wm::kMenuChecked : 0)};
        board_items_[1] = {"&Brown", nullptr, nullptr, nullptr, nullptr,
                           static_cast<uint8_t>(scene.colours == &kBrown ? wm::kMenuChecked : 0)};
        option_items_[0] = {"&Board", nullptr, nullptr, &board_menu_, nullptr, 0};
        option_items_[1] = {"Show where a piece can &go", nullptr, nullptr, nullptr, nullptr, wm::kMenuChecked};
        help_items_[0] = {"&How to play", nullptr, nullptr, nullptr, nullptr, 0};
        help_items_[1] = {"&About Chess", nullptr, nullptr, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
        status_.set(scene.status);
    }

    void paint_in(app::Control, app::Canvas &c) override
    {
        c.fill({0, 0, kClientW, kStatusTop}, gfx::face());
        // What the engine took is above the board, what the user took below.
        paint_strip(c, kTopStrip, scene_.taken_by_engine, -scene_.lead);
        paint_strip(c, kBottomStrip, scene_.taken_by_user, scene_.lead);
        c.sunken({kMargin, kBoardTop, kBoardSide, kBoardSide});
        const Colours &k = *scene_.colours;
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                // The user's side is at the bottom.
                const int file = scene_.user_is_white ? col : 7 - col;
                const int rank = scene_.user_is_white ? 7 - row : row;
                const bool dark = ((file + rank) & 1) == 0;
                const gfx::Rect r = {kMargin + kFrame + col * kSquare, kBoardTop + kFrame + row * kSquare, kSquare,
                                     kSquare};
                const bool marked = has(scene_.moved, file, rank) || has(scene_.picked, file, rank);
                c.fill(r, marked ? (dark ? k.moved_dark : k.moved_light) : dark ? k.dark : k.light);
                // The board's letters and numbers, inside the squares on
                // two of its edges, in the other squares' colour.
                const gfx::Colour note = dark ? k.light : k.dark;
                if (col == 0) {
                    const char text[2] = {static_cast<char>('1' + rank), 0};
                    c.text(r.x + 2, r.y + 1, text, note, true);
                }
                if (row == 7) {
                    const char text[2] = {static_cast<char>('a' + file), 0};
                    c.text(r.x + kSquare - 8, r.y + kSquare - 13, text, note, true);
                }
                const char piece = scene_.rows[7 - rank][file];
                if (piece != '.') {
                    draw_piece(c, r.x, r.y, piece, true);
                }
                if (has(scene_.targets, file, rank)) {
                    paint_dot(c, r.x + kSquare / 2, r.y + kSquare / 2, dark ? k.dot_dark : k.dot_light);
                }
            }
        }
    }

private:
    static bool has(const char *squares, int file, int rank)
    {
        for (const char *s = squares; s[0] != 0; s += 2) {
            if (s[0] == 'a' + file && s[1] == '1' + rank) {
                return true;
            }
        }
        return false;
    }

    static void paint_dot(app::Canvas &c, int cx, int cy, gfx::Colour colour)
    {
        static const int kHalf[12] = {2, 4, 5, 5, 6, 6, 6, 6, 5, 5, 4, 2}; // a disc 12 across
        for (int i = 0; i < 12; i++) {
            c.hline(cx - kHalf[i], cy - 6 + i, 2 * kHalf[i], colour);
        }
    }

    // A row of captured pieces, those of a kind close together, and by how
    // many pawns that side leads.
    void paint_strip(app::Canvas &c, int top, const char *taken, int lead)
    {
        int x = kMargin + 2;
        for (const char *p = taken; *p != 0; p++) {
            draw_piece(c, x, top + 2, *p, false);
            x += p[1] == p[0] ? 10 : kSmallSize + 2;
        }
        if (lead > 0) {
            char text[16];
            snprintf(text, sizeof(text), "+%d", lead);
            c.text(x + 3, top + 7, text, gfx::ink(), false);
        }
    }

    const Scene scene_;
    app::Picture all_;
    app::Plain status_;
    wm::MenuItem game_items_[5];
    wm::MenuItem board_items_[2];
    wm::MenuItem option_items_[2];
    wm::MenuItem help_items_[2];
    wm::Menu game_menu_ = {game_items_, 5};
    wm::Menu board_menu_ = {board_items_, 2};
    wm::Menu options_ = {option_items_, 2};
    wm::Menu help_ = {help_items_, 2};
    const wm::MenuBarEntry entries_[3] = {{"&Game", &game_menu_}, {"&Options", &options_}, {"&Help", &help_}};
    const wm::MenuBar bar_ = {entries_, 3};
};

// The dialog before a game: how strong the engine plays and which side the
// user takes. Both are remembered for the next game.
class NewGameWindow : public app::Window {
public:
    static constexpr int kW = 340, kH = 250;
    NewGameWindow()
    {
        group({10, 8, kW - 20, 122}, "Difficulty");
        static const char *const kLevels[5] = {
            "&1  Beginner: plays at once, makes mistakes",
            "&2  Easy: looks two moves ahead",
            "&3  Medium: thinks about a second",
            "&4  Hard: thinks a few seconds",
            "&5  Expert: thinks up to ten seconds",
        };
        for (int i = 0; i < 5; i++) {
            radio({22, 28 + i * 19, kW - 44, 16}, kLevels[i], 0, i);
        }
        set_radio_value(0, 2);
        group({10, 138, kW - 20, 62}, "You play");
        // Room on the left of each choice for a picture of the piece.
        radio({50, 159, 70, 16}, "&White", 1, 0);
        radio({154, 159, 70, 16}, "&Black", 1, 1);
        radio({258, 159, 70, 16}, "&Random", 1, 2);
        set_radio_value(1, 2);
        label({22, 180, kW - 44, 14}, "White moves first.");
        const app::Button play = button({kW - 172, kH - 34, 75, 23}, "&Play");
        button({kW - 87, kH - 34, 75, 23}, "Cancel");
        set_default(play);
    }

    void paint(app::Canvas &c) override
    {
        draw_piece(c, 26, 156, 'K', false);
        draw_piece(c, 130, 156, 'k', false);
        // Random: one of each, one behind the other.
        draw_piece(c, 226, 156, 'K', false);
        draw_piece(c, 236, 156, 'k', false);
    }
};

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
wm::Wm desktop(display, pointer, clock_);

void save(const std::string &path, bool with_pointer)
{
    desktop.update();
    std::vector<uint8_t> shown(display.pixels(), display.pixels() + 640 * 480);
    if (with_pointer) {
        video::compose_pointer(shown.data(), 0, 640 * 480, 640, pointer.state());
    }
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
        printf("cannot write %s\n", path.c_str());
        exit(1);
    }
    fprintf(f, "P6 640 480 255\n");
    for (uint8_t colour : shown) {
        const gfx::Rgb rgb = gfx::shown(colour);
        fputc(rgb.r, f);
        fputc(rgb.g, f);
        fputc(rgb.b, f);
    }
    fclose(f);
}

app::Number open_board(const Scene &scene)
{
    app::Look look;
    look.title = "Chess";
    look.x = 150;
    look.y = 4;
    look.width = kClientW;
    look.height = kClientH;
    look.flags = app::kClientSize | app::kFixedSize | app::kMenuBar;
    return app::open<ChessWindow>(look, scene);
}

void move_pointer(int x, int y)
{
    clock_.now += 300000;
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 0, 0});
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: chess_mockup <folder>\n");
        return 2;
    }
    const std::string folder = argv[1];
    static host::HostMemory memory;
    static fs::Storage storage;
    static fs::MemoryVolume ram_disk(memory.allocate(2 << 20), 2 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    system_.utc = 1795000000;
    ram_disk.set_clock(&system_);
    fs::add_samples(storage, 'C');
    static init::Init first(proc::table());
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    const shell::AsProgram as("Chess");

    // Fifteen moves into a game; White has just taken a pawn with the queen.
    const char *const kMiddle[8] = {"r..q.rk.", "pQ...ppp", "..p.....", "...n....",
                                    "...PN.b.", ".....N..", "PP...PPP", "R...R.K."};
    const char *const kStart[8] = {"rnbqkbnr", "pppppppp", "........", "........",
                                   "........", "........", "PPPPPPPP", "RNBQKBNR"};

    // 1. Green, the user is White and has moved: the engine thinks, and the
    //    pointer over the window is the hourglass.
    Scene thinking = {{}, true, &kGreen, "bnppp", "BBPP", 1, "b3b7", "", "", "The computer is thinking..."};
    memcpy(thinking.rows, kMiddle, sizeof(kMiddle));
    app::Number board = open_board(thinking);
    os::ui::set_locked(board, true);
    move_pointer(430, 215);
    save(folder + "/chess-1-green-thinking.ppm", true);
    os::ui::set_locked(board, false);
    os::ui::close(board);
    desktop.update();

    // 2. Brown, the user is Black (the board turned round) and has picked
    //    up the knight: where it can go is marked.
    Scene picked = {{}, false, &kBrown, "BBPP", "bnppp", -1, "b3b7", "d5", "b4b6c3c7e3e7f4f6",
                    "Your move."};
    memcpy(picked.rows, kMiddle, sizeof(kMiddle));
    board = open_board(picked);
    move_pointer(370, 190);
    save(folder + "/chess-2-brown-black-side.ppm", true);
    os::ui::close(board);
    desktop.update();

    // 3. The dialog before a game, over a board not yet played on.
    Scene fresh = {{}, true, &kGreen, "", "", 0, "", "", "", "Choose a game."};
    memcpy(fresh.rows, kStart, sizeof(kStart));
    board = open_board(fresh);
    app::Look look;
    look.title = "New game";
    look.width = NewGameWindow::kW;
    look.height = NewGameWindow::kH;
    look.flags = app::kDialog | app::kClientSize | app::kPicture;
    look.over = board;
    app::open<NewGameWindow>(look);
    move_pointer(560, 300);
    save(folder + "/chess-3-new-game.ppm", true);
    return 0;
}
