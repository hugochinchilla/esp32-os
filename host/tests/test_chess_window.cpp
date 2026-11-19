// Chess, the window (core/src/shell/chess.cpp): driven with the mouse and
// the keyboard as the user would, first with the engine thinking inside
// the call (no tasks, as most of the PC) and then in a background job on a
// real thread. The rules and the engine have their own test (test_chess).
// Given a folder, it writes the game's reference pictures there instead
// (see tools/check_pictures.py).
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "core/fs/memory_volume.hpp"
#include "core/games/chess.hpp"
#include "core/gfx/theme.hpp"
#include "core/job/job.hpp"
#include "core/msg/queue.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "core/sound/sound.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"
#include "../thread_queue.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

namespace chess = games::chess;
using chess::Black;
using chess::White;

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
host::HostMemory memory;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);
host::ThreadQueue queue;
host::ThreadTasks tasks;

void mouse(int x, int y, uint8_t buttons)
{
    clock_.now += 300000;
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
}

void click(int x, int y)
{
    mouse(x, y, 0);
    mouse(x, y, 1);
    mouse(x, y, 0);
}

void key(uint16_t usage, uint8_t mods = 0)
{
    const uint16_t mod_keys[3] = {wm::key::LeftShift, wm::key::LeftCtrl, wm::key::LeftAlt};
    for (int i = 0; i < 3; i++) {
        if (mods & (1 << i)) {
            desktop.key(mod_keys[i], true);
        }
    }
    desktop.key(usage, true);
    desktop.key(usage, false);
    for (int i = 0; i < 3; i++) {
        if (mods & (1 << i)) {
            desktop.key(mod_keys[i], false);
        }
    }
    clock_.now += 100000;
}
constexpr uint8_t kCtrl = 2, kAlt = 4;
constexpr uint16_t kF2 = 0x3B;

void letter(char c, uint8_t mods = 0) { key(static_cast<uint16_t>(wm::key::A + (c - 'a')), mods); }

void wait(uint32_t us)
{
    clock_.now += us;
    desktop.tick();
}

wm::Window *titled(const char *title)
{
    for (int z = 0; z < desktop.window_count(); z++) {
        if (strcmp(desktop.window_at(z)->title(), title) == 0) {
            return desktop.window_at(z);
        }
    }
    return nullptr;
}
wm::Window *board() { return titled("Chess"); }
const char *front() { return desktop.active() != nullptr ? desktop.active()->title() : ""; }

// The middle of a square on the screen: "e2". The user's side is at the
// bottom.
void square_xy(const char *name, int &x, int &y)
{
    const int file = name[0] - 'a', rank = name[1] - '1';
    const bool white = shell::chess_user() == White;
    const gfx::Rect c = board()->client();
    x = c.x + 8 + (white ? file : 7 - file) * 40 + 20;
    y = c.y + 32 + (white ? 7 - rank : rank) * 40 + 20;
}

// The computer's answer is not shown sooner than 250 to 350 ms after it
// began to think: long enough for it to be on the board.
void let_it_answer() { wait(400000); }

void click_square(const char *name)
{
    int x, y;
    square_xy(name, x, y);
    click(x, y);
    let_it_answer();
}

void drag(const char *from, const char *to)
{
    int x, y, x2, y2;
    square_xy(from, x, y);
    square_xy(to, x2, y2);
    mouse(x, y, 0);
    mouse(x, y, 1);
    mouse((x + x2) / 2, (y + y2) / 2, 1);
    mouse(x2, y2, 1);
    mouse(x2, y2, 0);
    let_it_answer();
}

uint8_t pixel(int x, int y) { return display.pixels()[y * 640 + x]; }
// The colour of a square, read at a corner no piece reaches.
uint8_t square_colour(const char *name)
{
    int x, y;
    square_xy(name, x, y);
    desktop.update();
    return pixel(x + 17, y - 17);
}

bool no_stale_pixels()
{
    desktop.update();
    std::string before(reinterpret_cast<char *>(display.pixels()), 640 * 480);
    desktop.repaint_all();
    return memcmp(before.data(), display.pixels(), before.size()) == 0;
}

std::string settings()
{
    char buf[512];
    size_t n = 0;
    if (storage.read_file("C:\\.config\\chess.ini", buf, sizeof(buf), n) != ports::FsError::Ok) {
        return "<missing>";
    }
    return std::string(buf, n);
}
// "level=3": the file has it as "level = 3".
bool kept(const char *line)
{
    std::string text = settings(), plain;
    for (char ch : text) {
        if (ch != ' ') {
            plain += ch;
        }
    }
    return plain.find(line) != std::string::npos;
}

void start_desktop()
{
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    constexpr size_t kRamDisk = 256 * 1024;
    static fs::MemoryVolume disk(memory.allocate(kRamDisk), kRamDisk, "RAM disk");
    storage.mount('C', disk);
    desktop.set_animations(false);
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_time(9, 41, 0);
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    wait(1000);
}

void open_chess()
{
    shell::launch(*shell::find_app("Chess"));
    desktop.update();
}

// The dialog before a game, which must be in front: a level (1 to 5), a
// side (0 White, 1 Black, 2 Random; -1 leaves it), then Play.
void choose(int level, int side)
{
    CHECK(strcmp(front(), "New game") == 0);
    const gfx::Rect c = desktop.active()->client();
    click(c.x + 30, c.y + 28 + (level - 1) * 19 + 8);
    if (side >= 0) {
        click(c.x + 56 + side * 104, c.y + 167);
    }
    key(wm::key::Enter);
    let_it_answer(); // if the computer has White
    desktop.update();
}

// Moves played straight into the game, for both sides.
void play(const char *moves)
{
    chess::Game &g = shell::chess_game();
    for (const char *m = moves; *m != 0;) {
        char text[6] = "";
        int n = 0;
        while (*m != 0 && *m != ' ') {
            text[n++] = *m++;
        }
        while (*m == ' ') {
            m++;
        }
        chess::Position p = g.position();
        CHECK(g.play(p.parse(text)));
    }
}

/* ---- A game, the engine thinking inside the call ------------------------------ */

void test_game()
{
    start_desktop();
    const uint32_t bytes_before = os::bytes_out();
    const shell::App *app = shell::find_app("Chess");
    CHECK(app != nullptr && app->icon != nullptr);
    open_chess();

    // The board, and over it the dialog that asks which game.
    CHECK(board() != nullptr && strcmp(front(), "New game") == 0);
    CHECK(strcmp(shell::chess_status(), "Choose a game.") == 0);
    const gfx::Rect frame = board()->frame();
    CHECK(frame.x >= 0 && frame.y >= 0 && frame.bottom() <= desktop.work_area().h && frame.right() <= 640);
    CHECK(board()->client().w == 336 && board()->client().h == 402);
    CHECK(no_stale_pixels());
    // Nothing moves before a game is chosen. Cancel leaves it so.
    {
        const gfx::Rect c = desktop.active()->client();
        click(c.x + 340 - 50, c.y + 250 - 22); // Cancel
    }
    desktop.update();
    CHECK(titled("New game") == nullptr && desktop.active() == board());
    key(kF2);
    desktop.update();
    CHECK(strcmp(front(), "New game") == 0);
    key(wm::key::Escape); // the same
    desktop.update();
    CHECK(titled("New game") == nullptr && desktop.active() == board());
    click_square("e2");
    click_square("e4");
    CHECK(shell::chess_game().plies() == 0 && settings() == "<missing>");

    // F2: level 1, White. Kept in chess.ini.
    key(kF2);
    desktop.update();
    choose(1, 0);
    CHECK(titled("New game") == nullptr && desktop.active() == board());
    CHECK(shell::chess_user() == White && strcmp(shell::chess_status(), "Your move.") == 0);
    CHECK(kept("level=1") && kept("side=white"));
    chess::Game &g = shell::chess_game();
    CHECK(g.plies() == 0 && !shell::chess_thinking());

    // The sounds (backlog item 70): every piece put down knocks, the
    // user's and the computer's; check and checkmate have their own.
    static const int16_t samples[sound::kIds] = {};
    static sound::Clip clips[sound::kIds];
    for (int i = 0; i < sound::kIds; i++) {
        clips[i] = {&samples[i], 1};
    }
    static host::RecordingSound heard;
    sound::set_clips(clips);
    os::install_sound(&heard);
    auto clip_of = [](sound::Id id) { return &samples[static_cast<int>(id)]; };
    auto heard_since = [&](size_t mark, sound::Id id) {
        for (size_t i = mark; i < heard.played.size(); i++) {
            if (heard.played[i] == clip_of(id)) {
                return true;
            }
        }
        return false;
    };

    // Two clicks: the piece, then the square. The piece picked stands on
    // yellow; the computer answers before the click returns (no tasks).
    const uint8_t dark = square_colour("e2");
    CHECK(dark == gfx::rgb(7, 7, 3) && square_colour("e3") == gfx::rgb(3, 4, 1));
    click_square("e2");
    CHECK(square_colour("e2") == gfx::rgb(7, 7, 1) && g.plies() == 0);
    CHECK(no_stale_pixels());
    click_square("e4");
    CHECK(g.plies() == 2 && g.position().side() == White && !shell::chess_thinking());
    CHECK(heard.played.size() == 2 && heard.played[0] == clip_of(sound::Id::ChessMove) &&
          heard.played[1] == clip_of(sound::Id::ChessMove));
    CHECK(chess::type_of(g.position().at(chess::square(4, 3))) == chess::Pawn);
    CHECK(strstr(shell::chess_status(), "Your move.") != nullptr);
    CHECK(no_stale_pixels());

    // A click on the piece picked lets it go; a click elsewhere too; and a
    // square it cannot go to is not a move.
    click_square("g1");
    CHECK(square_colour("g1") != gfx::rgb(3, 4, 1));
    click_square("g1");
    CHECK(square_colour("g1") == gfx::rgb(3, 4, 1));
    click_square("g1");
    click_square("g5");
    CHECK(g.plies() == 2 && square_colour("g1") == gfx::rgb(3, 4, 1));
    CHECK(no_stale_pixels());
    // The computer's pieces are not the user's to pick.
    click_square("b8");
    click_square("c6");
    CHECK(g.plies() == 2);

    // A drag. While the piece is in the hand its square is empty.
    {
        int x, y, x2, y2;
        square_xy("g1", x, y);
        square_xy("f3", x2, y2);
        mouse(x, y, 0);
        mouse(x, y, 1);
        mouse(x + 1, y, 1); // a tremble is not a drag
        CHECK(no_stale_pixels());
        mouse(x - 10, y - 30, 1);
        CHECK(no_stale_pixels());
        CHECK(pixel(x, y + 8) == gfx::rgb(5, 6, 0)); // yellow where the knight's body was
        mouse(x2, y2, 1);
        CHECK(no_stale_pixels());
        mouse(x2, y2, 0);
        let_it_answer();
    }
    CHECK(g.plies() == 4 && chess::type_of(g.position().at(chess::square(5, 2))) == chess::Knight);
    CHECK(no_stale_pixels());
    // Dropped where it cannot go, the piece goes home.
    drag("f3", "f4");
    CHECK(g.plies() == 4 && chess::type_of(g.position().at(chess::square(5, 2))) == chess::Knight);
    key(wm::key::Escape);
    CHECK(no_stale_pixels());

    // Take back, by Ctrl+Z and by the menu: the user's move and the answer.
    letter('z', kCtrl);
    CHECK(g.plies() == 2 && g.position().at(chess::square(5, 2)) == chess::kEmpty);
    letter('g', kAlt);
    letter('t');
    CHECK(g.plies() == 0 && strcmp(shell::chess_status(), "Your move.") == 0);
    letter('z', kCtrl); // nothing left
    CHECK(g.plies() == 0);
    CHECK(no_stale_pixels());

    // The whole window paints each screen pixel once.
    {
        std::vector<uint8_t> counts(640 * 480, 0);
        board()->invalidate();
        gfx::Canvas::count_writes(counts.data());
        desktop.update();
        gfx::Canvas::count_writes(nullptr);
        const gfx::Rect c = board()->client();
        int most = 0, missed = 0;
        for (int py = c.y; py < c.bottom(); py++) {
            for (int px = c.x; px < c.right(); px++) {
                const int n = counts[py * 640 + px];
                most = n > most ? n : most;
                missed += n == 0;
            }
        }
        CHECK(most == 1 && missed == 0);
    }
    // And a move paints only its squares: two of 40 by 40 for the user's,
    // and the like for the answer and the yellow that moves on.
    {
        std::vector<uint8_t> counts(640 * 480, 0);
        gfx::Canvas::count_writes(counts.data());
        click_square("d2");
        click_square("d4");
        desktop.update();
        gfx::Canvas::count_writes(nullptr);
        int touched = 0;
        for (uint8_t n : counts) {
            touched += n != 0;
        }
        CHECK(g.plies() == 2 && touched > 0 && touched <= 8 * 1600 + 336 * 20);
    }

    // What each side has taken: the user's captures below the board, the
    // computer's above, and the lead in pawns beside the side ahead.
    key(kF2);
    desktop.update();
    choose(1, 0);
    play("e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 a5c3 b2c3 g8f6");
    shell::chess_refresh();
    CHECK(g.taken(Black, chess::Pawn) == 1 && g.taken(Black, chess::Queen) == 1 && g.taken(White, chess::Knight) == 1 &&
          g.taken(White, chess::Pawn) == 1);
    CHECK(g.position().points(White) - g.position().points(Black) == 6);
    {
        desktop.update();
        const gfx::Rect c = board()->client();
        const auto drawn = [&](int x0, int top) {
            int n = 0;
            for (int y = top; y < top + 24; y++) {
                for (int x = x0; x < x0 + 18; x++) {
                    n += pixel(c.x + x, c.y + y) != gfx::face();
                }
            }
            return n;
        };
        // Below: the queen and the pawn the user took, then "+6". Above:
        // the knight and the pawn the computer took, and no figure.
        CHECK(drawn(8, 356) > 40 && drawn(26, 356) > 20 && drawn(44, 356) > 4 && drawn(80, 356) == 0);
        CHECK(drawn(8, 4) > 40 && drawn(26, 4) > 20 && drawn(44, 4) == 0);
    }
    CHECK(no_stale_pixels());

    // A pawn on the last rank asks what it becomes; closed without an
    // answer, the move is not made.
    key(kF2);
    desktop.update();
    choose(1, 0);
    play("a2a4 b7b5 a4b5 a7a6 b5a6 c8b7 a6b7 b8c6");
    shell::chess_refresh();
    const int plies = g.plies();
    drag("b7", "b8");
    desktop.update();
    CHECK(strcmp(front(), "Chess") == 0 && desktop.active() != board() && g.plies() == plies);
    CHECK(no_stale_pixels());
    key(wm::key::Escape);
    desktop.update();
    CHECK(desktop.active() == board() && g.plies() == plies);
    CHECK(chess::type_of(g.position().at(chess::square(1, 6))) == chess::Pawn);
    click_square("b7");
    click_square("a8"); // taking the rook, and promoting
    desktop.update();
    CHECK(desktop.active() != board() && g.plies() == plies);
    {
        const gfx::Rect c = desktop.active()->client();
        click(c.x + 10 + 74 + 34, c.y + 96 - 22); // Rook
    }
    let_it_answer();
    desktop.update();
    CHECK(desktop.active() == board() && g.plies() == plies + 2);
    CHECK(g.position().at(chess::square(0, 7)) == chess::piece(White, chess::Rook) || g.taken(White, chess::Rook) == 1);
    CHECK(no_stale_pixels());

    // The end of a game: mated by the computer (level 2 does not pass up a
    // mate in one). Nothing moves afterwards, but the move can be taken
    // back and the game goes on.
    key(kF2);
    desktop.update();
    choose(2, 0);
    play("f2f3 e7e5");
    shell::chess_refresh();
    click_square("g2");
    click_square("g4");
    CHECK(g.status() == chess::Status::Checkmate && g.plies() == 4);
    CHECK(heard.played.back() == clip_of(sound::Id::ChessMate)); // the computer's piece
    size_t mark = heard.played.size();
    CHECK(strcmp(shell::chess_status(), "Checkmate. The computer wins.") == 0);
    click_square("a2");
    click_square("a3");
    CHECK(g.plies() == 4);
    letter('z', kCtrl);
    CHECK(g.plies() == 2 && strcmp(shell::chess_status(), "Your move.") == 0);
    CHECK(heard.played.size() == mark); // no move, and a move taken back: silence
    click_square("a2");
    click_square("a3");
    CHECK(g.plies() == 4 && g.status() == chess::Status::Playing);
    // And mating it.
    key(kF2);
    desktop.update();
    choose(1, 0);
    play("e2e4 f7f6 d2d4 g7g5");
    shell::chess_refresh();
    click_square("d1");
    click_square("h5");
    CHECK(g.status() == chess::Status::Checkmate && strcmp(shell::chess_status(), "Checkmate. You win!") == 0);
    CHECK(!shell::chess_thinking());
    CHECK(heard.played.back() == clip_of(sound::Id::ChessMate));

    // Resign ends the game; Take back takes the resignation back.
    key(kF2);
    desktop.update();
    choose(1, 0);
    letter('g', kAlt);
    letter('r');
    CHECK(strcmp(shell::chess_status(), "You resigned. The computer wins.") == 0);
    click_square("e2");
    click_square("e4");
    CHECK(g.plies() == 0);
    letter('z', kCtrl);
    click_square("e2");
    click_square("e4");
    CHECK(g.plies() == 2);

    // Playing Black: the computer moves first, and the board is turned so
    // that Black is at the bottom.
    key(kF2);
    desktop.update();
    choose(3, 1);
    CHECK(shell::chess_user() == Black && g.plies() == 1 && g.position().side() == Black);
    CHECK(kept("level=3") && kept("side=black"));
    {
        int x, y, x2, y2;
        square_xy("a8", x, y);
        square_xy("h1", x2, y2);
        CHECK(x > x2 && y > y2); // a8 at the bottom right
    }
    letter('z', kCtrl); // the computer's first move is not the user's to take back
    CHECK(g.plies() == 1);
    drag("e7", "e5");
    CHECK(g.plies() == 3 && chess::type_of(g.position().at(chess::square(4, 4))) == chess::Pawn);
    CHECK(no_stale_pixels());

    // Options: the brown board, another set of pieces, no dots. Each is
    // kept in chess.ini as it is chosen.
    CHECK(square_colour("a6") == gfx::rgb(7, 7, 3));
    letter('o', kAlt);
    letter('b');
    letter('b');
    CHECK(square_colour("a6") == gfx::rgb(7, 6, 2) && kept("board=brown"));
    letter('o', kAlt);
    letter('p');
    letter('s');
    CHECK(kept("pieces=symbols"));
    click_square("b8");
    {
        int x, y;
        square_xy("c6", x, y);
        desktop.update();
        CHECK(pixel(x, y) == gfx::rgb(5, 4, 1)); // the dot on a cream square
        letter('o', kAlt);
        letter('g');
        desktop.update();
        CHECK(pixel(x, y) == gfx::rgb(7, 6, 2) && kept("dots=false"));
    }
    click_square("c6"); // the move is still one
    CHECK(g.plies() == 5);
    CHECK(no_stale_pixels());

    // In every theme the window is whole and nothing is left behind.
    for (int theme = 1; theme <= gfx::theme_count(); theme++) {
        os::set_theme(theme % gfx::theme_count());
        desktop.update();
        CHECK(no_stale_pixels());
        CHECK(square_colour("a6") == gfx::rgb(7, 6, 2)); // the board keeps its colours
    }

    // Closing ends the program and gives everything back. Opened again,
    // it starts from what the settings file holds.
    letter('g', kAlt);
    letter('c');
    wait(1000);
    wait(1000);
    CHECK(board() == nullptr && proc::table().find_by_name("Chess") == proc::kNone);
    CHECK(os::bytes_out() == bytes_before);
    open_chess();
    CHECK(strcmp(front(), "New game") == 0);
    key(wm::key::Enter); // as it stands: level 3, Black
    let_it_answer();
    desktop.update();
    CHECK(shell::chess_user() == Black && shell::chess_game().plies() == 1);
    CHECK(square_colour("a6") == gfx::rgb(7, 6, 2));
    // Random settles on a side each game.
    key(kF2);
    desktop.update();
    choose(1, 2);
    CHECK(kept("side=random"));
    CHECK(shell::chess_game().plies() == (shell::chess_user() == White ? 0 : 1));
    // Check: the queen to h5 again, where a pawn can still step in.
    key(kF2);
    desktop.update();
    choose(1, 0);
    play("e2e4 f7f6 d2d4 a7a6");
    shell::chess_refresh();
    mark = heard.played.size();
    click_square("d1");
    click_square("h5");
    CHECK(shell::chess_game().status() == chess::Status::Playing && heard.played.size() > mark);
    CHECK(heard.played[mark] == clip_of(sound::Id::ChessCheck) && !heard_since(mark, sound::Id::ChessMate));
    os::install_sound(nullptr);
    desktop.close(*board());
    wait(1000);
    wait(1000);
    CHECK(board() == nullptr && os::bytes_out() == bytes_before);
    storage.remove("C:\\.config\\chess.ini");
}

/* ---- The computer's piece landing ------------------------------------------------ */

// The 40 by 40 pixels of a square, as the screen shows them now.
std::string square_pixels(int sq)
{
    const bool white = shell::chess_user() == White;
    const int file = chess::file_of(sq), rank = chess::rank_of(sq);
    const gfx::Rect c = board()->client();
    const int x = c.x + 8 + (white ? file : 7 - file) * 40, y = c.y + 32 + (white ? 7 - rank : rank) * 40;
    desktop.update();
    std::string out;
    for (int row = 0; row < 40; row++) {
        out.append(reinterpret_cast<const char *>(display.pixels()) + (y + row) * 640 + x, 40);
    }
    return out;
}
// The lowest row of a square with something of the piece in it, looked for
// in the middle columns, where no letter, number or corner mark is.
int foot(const std::string &square)
{
    const char ground = square[1 * 40 + 38]; // a corner: a lifted king reaches the top in the middle
    for (int row = 39; row >= 0; row--) {
        for (int col = 10; col < 30; col++) {
            if (square[row * 40 + col] != ground) {
                return row;
            }
        }
    }
    return -1;
}
// The screen without one square of the board and without the taskbar's clock.
std::string all_but(int sq)
{
    const bool white = shell::chess_user() == White;
    const int file = chess::file_of(sq), rank = chess::rank_of(sq);
    const gfx::Rect c = board()->client();
    const int x = c.x + 8 + (white ? file : 7 - file) * 40, y = c.y + 32 + (white ? 7 - rank : rank) * 40;
    desktop.update();
    std::string out(reinterpret_cast<const char *>(display.pixels()), 640 * desktop.work_area().h);
    for (int row = 0; row < 40; row++) {
        memset(&out[(y + row) * 640 + x], 0, 40);
    }
    return out;
}

// The second click of a move, as far as the press: the move is played on
// it, and the test's clock does not go on as it does for the release.
int pressed_x = 0, pressed_y = 0;
void press_square(const char *name)
{
    square_xy(name, pressed_x, pressed_y);
    mouse(pressed_x, pressed_y, 0);
    mouse(pressed_x, pressed_y, 1);
    let_it_answer(); // the piece starts to land on this tick
}
void release()
{
    desktop.mouse({true, static_cast<int16_t>(pressed_x), static_cast<int16_t>(pressed_y), 0, 0});
}

void test_landing()
{
    start_desktop();
    // The taskbar's clock is painted on a tick; no minute ends in this test.
    desktop.set_time(9, 41, 0);
    wait(1000);
    const uint32_t bytes_before = os::bytes_out();
    open_chess();
    choose(1, 0);
    chess::Game &g = shell::chess_game();

    // The computer answers inside the click: its piece is painted four
    // pixels above its place, comes down, bounces once and rests within a
    // quarter of a second. Nothing but its square changes meanwhile.
    click_square("e2");
    press_square("e4");
    CHECK(g.plies() == 2);
    const int to = chess::to_of(g.last());
    const std::string first = square_pixels(to);
    const std::string around = all_but(to);
    CHECK(no_stale_pixels());
    std::vector<int> feet = {foot(first)};
    std::string now = first;
    int changes = 0;
    for (int step = 1; step < 14; step++) {
        wait(1000000 / 60);
        const std::string next = square_pixels(to);
        changes += next != now;
        now = next;
        feet.push_back(foot(now));
        CHECK(no_stale_pixels());
        CHECK(all_but(to) == around);
    }
    wait(1000000 / 60);
    const std::string rest = square_pixels(to);
    const int ground = foot(rest);
    CHECK(rest != first && ground == 36);
    // 4 4 3 3 2 1 1 1 2 2 2 1 1 1 above the ground, and painted only when
    // the height changes: 5 times in the 13 steps, and once more to rest.
    static const int kHeights[14] = {4, 4, 3, 3, 2, 1, 1, 1, 2, 2, 2, 1, 1, 1};
    for (int step = 0; step < 14; step++) {
        CHECK(ground - feet[static_cast<size_t>(step)] == kHeights[step]);
    }
    CHECK(changes == 5);
    // It stays at rest, and the rest of the window was left alone.
    release();
    wait(500000);
    CHECK(square_pixels(to) == rest && all_but(to) == around && no_stale_pixels());

    // A late tick skips steps: the landing takes as long by the clock.
    click_square("d2");
    press_square("d4");
    CHECK(g.plies() == 4);
    const int second = chess::to_of(g.last());
    CHECK(foot(square_pixels(second)) == ground - 4);
    wait(8 * (1000000 / 60) + 1000);
    CHECK(foot(square_pixels(second)) == ground - 2);
    wait(1000000);
    CHECK(foot(square_pixels(second)) == ground && no_stale_pixels());

    // The game goes back while the piece lands: nothing is left lifted, and
    // the square the piece came back from is as any other.
    release();
    click_square("g1");
    press_square("f3");
    CHECK(g.plies() == 6);
    const int third = chess::to_of(g.last());
    CHECK(foot(square_pixels(third)) < ground);
    release();
    CHECK(foot(square_pixels(third)) < ground);
    letter('z', kCtrl);
    CHECK(g.plies() == 4 && no_stale_pixels());
    wait(1000000);
    CHECK(no_stale_pixels());
    // The user's own piece does not land: it was put there by hand.
    click_square("g1");
    click_square("f3");
    CHECK(foot(square_pixels(chess::square(5, 2))) == ground);

    // The window closes while a piece lands: its timer goes with it.
    desktop.close(*board());
    wait(1000);
    wait(1000000);
    CHECK(board() == nullptr && os::bytes_out() == bytes_before);
    storage.remove("C:\\.config\\chess.ini");
}

/* ---- The computer does not answer at once ---------------------------------------- */

void test_pace()
{
    start_desktop();
    desktop.set_time(9, 41, 0);
    wait(1000);
    const uint32_t bytes_before = os::bytes_out();
    open_chess();
    choose(1, 0);
    chess::Game &g = shell::chess_game();

    // The engine has its move before the click returns (no tasks), and
    // keeps it: the user's piece is seen on its square first, the game
    // waiting for the computer. The move comes 250 to 350 ms later, another
    // length each time.
    bool short_one = false, long_one = false;
    const char *const moves[4][2] = {{"e2", "e4"}, {"d2", "d4"}, {"a2", "a3"}, {"h2", "h3"}};
    for (int i = 0; i < 4; i++) {
        int x, y;
        square_xy(moves[i][0], x, y);
        click(x, y);
        square_xy(moves[i][1], x, y);
        mouse(x, y, 0);
        mouse(x, y, 1); // played on the press; the clock stands still from here
        const int plies = 2 * i + 1;
        CHECK(g.plies() == plies && shell::chess_thinking() && board()->hourglass());
        CHECK(strcmp(shell::chess_status(), "The computer is thinking...") == 0);
        CHECK(no_stale_pixels());
        // The user cannot move for the computer meanwhile.
        desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 0, 0});
        wait(249000);
        CHECK(g.plies() == plies && shell::chess_thinking());
        uint32_t waited = 249000;
        while (g.plies() == plies && waited < 400000) {
            wait(10000);
            waited += 10000;
        }
        CHECK(g.plies() == plies + 1 && !shell::chess_thinking() && !board()->hourglass());
        CHECK(waited >= 250000 && waited <= 360000);
        short_one = short_one || waited < 300000;
        long_one = long_one || waited >= 300000;
        wait(1000000);
    }
    (void)short_one; // which lengths came up depends on the clock: not checked
    (void)long_one;

    // Take back while the answer waits: it is dropped, and does not come
    // later. The same for another game.
    {
        int x, y;
        click_square("g1"); // an answer comes, then:
        square_xy("g1", x, y);
        const int before = g.plies();
        letter('z', kCtrl);
        CHECK(g.plies() == before - 2);
        square_xy("b1", x, y);
        click(x, y);
        square_xy("c3", x, y);
        click(x, y);
        CHECK(g.plies() == before - 1 && shell::chess_thinking());
        letter('z', kCtrl);
        CHECK(g.plies() == before - 2 && !shell::chess_thinking() && !board()->hourglass());
        wait(1000000);
        CHECK(g.plies() == before - 2 && strstr(shell::chess_status(), "Your move.") != nullptr);
        click(x, y); // nothing is picked: a click on an empty square
        square_xy("b1", x, y);
        click(x, y);
        square_xy("c3", x, y);
        click(x, y);
        key(kF2);
        desktop.update();
        choose(1, 0);
        CHECK(g.plies() == 0 && !shell::chess_thinking());
        wait(1000000);
        CHECK(g.plies() == 0);
    }

    // The window closes while an answer waits.
    {
        int x, y;
        square_xy("e2", x, y);
        click(x, y);
        square_xy("e4", x, y);
        click(x, y);
        CHECK(g.plies() == 1 && shell::chess_thinking());
    }
    desktop.close(*board());
    wait(1000);
    wait(1000000);
    CHECK(board() == nullptr && os::bytes_out() == bytes_before);
    storage.remove("C:\\.config\\chess.ini");
}

/* ---- The engine in a background job, on a real thread --------------------------- */

void let_job_finish(msg::Inbox &inbox)
{
    tasks.finish();
    inbox.deliver();
    wait(1000);
}

void test_background()
{
    msg::Inbox inbox(queue);
    const uint32_t bytes_before = os::bytes_out();
    const int started = tasks.started;
    open_chess();
    chess::Game &g = shell::chess_game();

    // The job is held back: the computer is "thinking", the window answers
    // and shows the hourglass, and the user cannot move.
    tasks.hold();
    choose(2, 0);
    click_square("e2");
    click_square("e4");
    CHECK(g.plies() == 1 && shell::chess_thinking() && board()->hourglass());
    CHECK(tasks.started == started + 1 && tasks.last_priority == ports::Priority::Low);
    CHECK(strcmp(shell::chess_status(), "The computer is thinking...") == 0);
    CHECK(job::jobs().running(proc::table().find_by_name("Chess")) == 1);
    click_square("d2");
    click_square("d4");
    CHECK(g.plies() == 1);
    letter('o', kAlt); // the menus work meanwhile
    letter('b');
    letter('b');
    CHECK(kept("board=brown") && no_stale_pixels());
    let_job_finish(inbox);
    CHECK(g.plies() == 2 && !shell::chess_thinking() && !board()->hourglass());
    CHECK(strstr(shell::chess_status(), "Your move.") != nullptr && tasks.ended == started + 1);
    CHECK(no_stale_pixels());

    // Take back while it thinks: the answer on its way is thrown away.
    click_square("d2");
    click_square("d4");
    CHECK(g.plies() == 3 && shell::chess_thinking());
    letter('z', kCtrl);
    CHECK(g.plies() == 2 && !shell::chess_thinking() && !board()->hourglass());
    let_job_finish(inbox);
    CHECK(g.plies() == 2 && g.position().side() == White);

    // A new game as Black while the old answer is still on its way: the
    // old one is dropped and the engine thinks about the new game.
    click_square("d2");
    click_square("d4");
    CHECK(g.plies() == 3 && shell::chess_thinking());
    key(kF2);
    desktop.update();
    choose(2, 1);
    CHECK(g.plies() == 0 && shell::chess_thinking() && shell::chess_user() == Black);
    let_job_finish(inbox); // the old answer: not played
    CHECK(g.plies() == 0 && shell::chess_thinking() && tasks.started == started + 4);
    let_job_finish(inbox); // the new one
    let_it_answer();
    CHECK(g.plies() == 1 && !shell::chess_thinking());

    // The strongest level looks as deep as its time allows: the window's
    // timer tells the job when the ten seconds are up, and the job rests
    // now and then meanwhile.
    key(kF2);
    desktop.update();
    tasks.release();
    choose(5, 1);
    CHECK(g.plies() == 0 && shell::chess_thinking());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    inbox.deliver();
    CHECK(g.plies() == 0 && shell::chess_thinking()); // still at it: nothing but the time stops it
    wait(9 * 1000 * 1000);
    CHECK(shell::chess_thinking());
    wait(1100 * 1000);
    let_job_finish(inbox);
    CHECK(g.plies() == 1 && !shell::chess_thinking() && tasks.rests > 0);

    // The window closed while the engine thinks: the process waits for the
    // job, and what the job worked with goes back when it has returned.
    tasks.hold();
    drag("e7", "e5");
    CHECK(g.plies() == 2 && shell::chess_thinking());
    const proc::Pid pid = proc::table().find_by_name("Chess");
    desktop.close(*board());
    wait(1000);
    wait(1000);
    CHECK(board() == nullptr && proc::table().alive(pid) && os::bytes_out() > bytes_before);
    let_job_finish(inbox);
    wait(1000);
    CHECK(!proc::table().alive(pid) && os::bytes_out() == bytes_before);
    tasks.release();
    storage.remove("C:\\.config\\chess.ini");
}

/* ---- Reference pictures --------------------------------------------------------- */

int write_pictures(const std::string &folder)
{
    auto save = [&](const char *name) {
        desktop.update();
        const std::string path = folder + "/" + name + ".raw";
        FILE *file = fopen(path.c_str(), "wb");
        if (file == nullptr || fwrite(display.pixels(), 1, 640 * 480, file) != 640 * 480) {
            printf("cannot write %s\n", path.c_str());
            failures++;
        }
        if (file != nullptr) {
            fclose(file);
        }
    };
    start_desktop();
    open_chess();
    save("55-chess-new-game");

    // A game under way, the user White with a knight picked.
    choose(1, 0);
    play("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4 b1c3 f6e4 e1g1 e4c3 b2c3 b4c3");
    shell::chess_refresh();
    click_square("f3");
    save("56-chess-playing");
    click_square("f3");

    // The brown board, and a piece in the hand.
    letter('o', kAlt);
    letter('b');
    letter('b');
    save("57-chess-brown");
    {
        int x, y;
        square_xy("c4", x, y);
        mouse(x, y, 0);
        mouse(x, y, 1);
        mouse(x + 52, y - 66, 1);
        save("58-chess-dragging");
        mouse(x, y, 1);
        mouse(x, y, 0);
        key(wm::key::Escape);
    }

    // The other two sets of pieces, the menu open at the second.
    letter('o', kAlt);
    letter('p');
    letter('s');
    letter('o', kAlt);
    letter('p');
    save("59-chess-symbols-menu");
    key(wm::key::Escape);
    key(wm::key::Escape);
    letter('o', kAlt);
    letter('p');
    letter('l');
    save("59b-chess-classic");

    // What a pawn becomes.
    letter('o', kAlt);
    letter('p');
    letter('c');
    letter('o', kAlt);
    letter('b');
    letter('g');
    key(kF2);
    desktop.update();
    choose(1, 0);
    play("a2a4 b7b5 a4b5 a7a6 b5a6 c8b7 a6b7 b8c6");
    shell::chess_refresh();
    drag("b7", "b8");
    save("59c-chess-promotion");
    desktop.close(*desktop.active());

    // The end of a game.
    key(kF2);
    desktop.update();
    choose(1, 0);
    play("e2e4 f7f6 d2d4 g7g5");
    shell::chess_refresh();
    click_square("d1");
    click_square("h5");
    save("59d-chess-mate");

    // In each of the other themes, with pieces taken on both sides.
    key(kF2);
    desktop.update();
    choose(1, 1);
    shell::chess_game().reset();
    play("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4 b1c3 f6e4 e1g1 e4c3 b2c3 b4c3 c4f7");
    shell::chess_refresh();
    for (int theme = 1; theme < gfx::theme_count(); theme++) {
        os::set_theme(theme);
        const std::string name = std::string("59e-chess-theme-") + gfx::theme_at(theme).key;
        save(name.c_str());
    }
    os::set_theme(0);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc > 1) {
        return write_pictures(argv[1]);
    }
    test_game();
    test_landing();
    test_pace();
    msg::set_queue(&queue);
    job::jobs().set_tasks(&tasks);
    test_background();
    job::jobs().set_tasks(nullptr);
    msg::set_queue(nullptr);
    if (failures != 0) {
        printf("%d checks failed\n", failures);
        return 1;
    }
    printf("chess window: all checks passed\n");
    return 0;
}
