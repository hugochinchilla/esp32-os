// Chess: the rules and an engine that plays, written for this project
// (backlog item 50; docs/journal/2026-11-18-chess-engine.md). No drawing, no
// clock, no memory of its own and no task in here: the window (still to be
// built) owns those and reaches the engine through three things.
//
//   Position  a board and whose turn it is: the legal moves, making and
//             taking back a move, check, mate and the draws.
//   Game      a Position played from the first move: the moves so far, what
//             each side has taken, take back.
//   Engine    picks a move. It thinks on a copy of the position, so the
//             window can go on painting its own while a job runs think().
//
// The engine is ordinary code that never sleeps, locks or allocates. What
// keeps it out of the desktop's way is where the window runs it (a job at
// low priority) and the two hooks it calls about every 256 positions: one
// for the time, one that may yield the processor and may say "stop".
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace games {
namespace chess {

enum Color : uint8_t { White = 0, Black = 1 };
enum Type : uint8_t { NoType = 0, Pawn, Knight, Bishop, Rook, Queen, King };

// What is on a square: 0 for nothing, else a type and a colour.
using Piece = uint8_t;
constexpr Piece kEmpty = 0;
constexpr Piece piece(Color c, Type t) { return static_cast<Piece>(t | (8 << c)); }
constexpr Type type_of(Piece p) { return static_cast<Type>(p & 7); }
constexpr Color color_of(Piece p) { return static_cast<Color>((p >> 4) & 1); }
constexpr Color other(Color c) { return static_cast<Color>(c ^ 1); }

// Squares are 0 to 63: a1 is 0, b1 is 1, h1 is 7, a2 is 8, h8 is 63.
constexpr int square(int file, int rank) { return rank * 8 + file; }
constexpr int file_of(int sq) { return sq & 7; }
constexpr int rank_of(int sq) { return sq >> 3; }

// A move: where from, where to and, for a pawn reaching the last rank, what
// it becomes. Castling is the king's move of two squares; taking in passing
// is the pawn's diagonal move to the empty square.
using Move = uint16_t;
constexpr Move kNoMove = 0;
constexpr Move move(int from, int to, Type promotion = NoType)
{
    return static_cast<Move>(from | (to << 6) | (promotion << 12));
}
constexpr int from_of(Move m) { return m & 63; }
constexpr int to_of(Move m) { return (m >> 6) & 63; }
constexpr Type promotion_of(Move m) { return static_cast<Type>((m >> 12) & 7); }

// "e2e4", "e7e8q". `out` holds six bytes.
void to_text(Move m, char *out);

// No position has more legal moves than 218.
constexpr int kMaxMoves = 256;

enum class Status : uint8_t {
    Playing,
    Checkmate,      // the side to move has lost
    Stalemate,      // drawn: no legal move and not in check
    DrawRepetition, // drawn: the same position for the third time
    DrawFifty,      // drawn: fifty moves each without a capture or a pawn move
    DrawMaterial,   // drawn: neither side has enough left to mate
};

// A move with the place the search gives it in the queue.
struct Scored {
    Move move;
    int16_t score;
};

// What make() needs kept to take the move back.
struct Undo {
    uint64_t key;
    Move move;
    Piece captured;
    uint8_t castling;
    uint8_t passing;
    uint8_t halfmove;
};

class Position {
public:
    Position() { reset(); }

    // The first position of a game.
    void reset();
    // A position in the usual one-line form (FEN). False, and the first
    // position, if it cannot be read or a king is missing.
    bool set_fen(const char *fen);

    Piece at(int sq) const { return board_[kTo120[sq]]; }
    Color side() const { return static_cast<Color>(side_); }
    int king(Color c) const;
    bool in_check() const { return attacked(king_[side_], other(side())); }
    // Counted from 1, going up after Black's move.
    int move_number() const { return fullmove_; }
    // How many of that piece the side has on the board.
    int count(Color c, Type t) const { return count_[c][t]; }
    // The side's pieces in pawns: 1, 3, 3, 5, 9.
    int points(Color c) const;

    // Every legal move, into `out` (room for kMaxMoves). Returns how many.
    int legal_moves(Move *out);
    bool legal(Move m);
    // Reads "e2e4" or "e7e8q". kNoMove unless it is a legal move here.
    Move parse(const char *text);
    // Plays a legal move for good. False, and nothing changed, if it is not
    // one.
    bool play(Move m);
    Status status();

    // --- For the engine and the tests ---

    // Plays a move that the generator made. False, and nothing changed, if
    // it leaves the mover's king in check.
    bool make(Move m, Undo &undo);
    void unmake(const Undo &undo);
    // The turn handed over without a move (the search's "null move").
    void make_null(Undo &undo);
    void unmake_null(const Undo &undo);
    // Moves that follow the pieces' ways of moving but may leave the king
    // in check. With `captures`, only captures and promotions to a queen.
    int generate(Scored *out, bool captures) const;
    bool attacked(int sq120, Color by) const;
    // True for a move that neither takes nor promotes.
    bool quiet(Move m) const;
    // The position has been on the board before, since the last capture or
    // pawn move.
    bool repeated(int times = 1) const;
    bool fifty() const { return halfmove_ >= 100; }
    bool bare() const;
    // The side to move has something besides king and pawns.
    bool has_pieces() const;
    // How good the position is for the side to move, in hundredths of a
    // pawn.
    int evaluate() const;
    // A number that stands for the position: equal for equal positions.
    uint64_t key() const { return key_; }
    // The same two figures worked out from the board alone, for the tests.
    uint64_t key_from_scratch() const;
    bool sums_match() const;

private:
    friend class Engine;

    void clear();
    void put(int sq120, Piece p);
    void lift(int sq120);
    void push_key();

    static const uint8_t kTo120[64];

    Piece board_[120];
    uint8_t king_[2];
    uint8_t side_;
    uint8_t castling_; // bit 0 white short, 1 white long, 2 black short, 3 black long
    uint8_t passing_;  // the square a pawn may be taken on in passing, or 0
    uint8_t halfmove_; // half moves since a capture or a pawn move
    uint16_t fullmove_;
    uint8_t count_[2][7];
    int16_t mg_[2]; // material and squares, valued for the middle game
    int16_t eg_[2]; // and for the end game
    uint64_t key_;
    // The keys of the last 256 positions, for repetition. Only the ones
    // since the last capture or pawn move are ever looked at: 100 at most.
    uint32_t ply_;
    uint64_t ring_[256];
};

// A game from the first position.
class Game {
public:
    // Past this many half moves the game goes on but cannot be taken back.
    static constexpr int kMaxKept = 1024;

    Game() { reset(); }
    void reset();

    // The window paints from this and hands it to Engine::think.
    const Position &position() const { return position_; }
    int legal_moves(Move *out) { return position_.legal_moves(out); }
    bool play(Move m);
    // Takes back the last `plies` half moves. False if there are not that
    // many or the game is longer than kMaxKept.
    bool take_back(int plies = 1);
    Status status() { return position_.status(); }

    // Half moves played.
    int plies() const { return plies_; }
    Move last() const { return plies_ > 0 && plies_ <= kMaxKept ? moves_[plies_ - 1] : kNoMove; }
    // How many pieces of that kind `of` has lost. A pawn that became a
    // queen and was then taken counts as a queen.
    int taken(Color of, Type t) const { return taken_[of][t]; }

private:
    void apply(Move m);

    Position position_;
    Move moves_[kMaxKept];
    int plies_;
    uint8_t taken_[2][7];
};

// --- The engine ---

constexpr int kMaxDepth = 32;
constexpr int kMate = 30000;

// How long and how deep to look.
struct Limits {
    int depth = kMaxDepth;  // whole moves ahead are half this
    uint32_t time_ms = 0;   // 0: no limit (needs Hooks::now_ms otherwise)
    uint32_t nodes = 0;     // positions looked at; 0: no limit
    // Mistakes on purpose: the move is drawn from those within this many
    // hundredths of a pawn of the best one. 0: always the best found.
    int noise = 0;
    uint32_t seed = 1;      // for the draw; the same seed, the same move
};

// The five levels of the "New game" dialog, 1 to 5.
constexpr int kLevels = 5;
struct Level {
    uint8_t depth;
    uint16_t time_ms;
    uint16_t noise;
};
const Level &level(int n);
// The limits for a level in this position. The two strongest levels get a
// little noise in the first moves, so that two games do not open alike.
Limits limits_for(int level, const Position &position, uint32_t seed);

// What the engine asks of whoever runs it. All may be null.
struct Hooks {
    // Milliseconds from any fixed moment.
    uint32_t (*now_ms)(void *context) = nullptr;
    // Called about every 256 positions, from inside the search. The place
    // to give the processor away for a moment. True: stop thinking now.
    bool (*poll)(void *context) = nullptr;
    void *context = nullptr;
};

struct Result {
    Move best = kNoMove;  // kNoMove only if there is no legal move
    int score = 0;        // for the side to move; kMate - n is mate in n half moves
    int depth = 0;        // the deepest look that was finished
    uint32_t nodes = 0;
    uint32_t time_ms = 0;
    uint32_t stack_bytes = 0; // the most stack think() used below its caller
    bool stopped = false;     // told to stop (not: out of time)
};

// About 13 KB. The window asks the Memory port for one when a game starts,
// and for the table of positions already seen.
class Engine {
public:
    static constexpr int kMaxPly = 48;
    static constexpr int kMoveStack = 2048;
    static constexpr int kPollNodes = 256;
    // What one remembered position takes of the table.
    static constexpr size_t kEntryBytes = 12;

    Engine();

    // Memory for the table of positions already seen, kept by the caller
    // for as long as the engine is used. None (the default): the engine
    // plays the same way, more slowly. The table is emptied.
    void set_table(void *memory, size_t bytes);
    // Forgets what earlier thinking left behind.
    void new_game();

    // Picks a move for the side to move. Returns when a limit is reached,
    // when stop() is called or when Hooks::poll says so; it always returns
    // a legal move if there is one. Needs about 6 KB of stack on the PC;
    // Result::stack_bytes tells what a run used.
    Result think(const Position &position, const Limits &limits, const Hooks &hooks = Hooks());

    // From any task: makes a think() that is running return soon. A think()
    // that has not started yet is not stopped: use Hooks::poll for that.
    void stop() { stop_.store(true, std::memory_order_relaxed); }

private:
    struct Entry {
        uint32_t lock;
        Move move;
        int16_t score;
        uint8_t depth;
        uint8_t bound; // 1 at most this, 2 at least this, 3 exactly this
    };

    bool tick();
    void check();
    void note_stack();
    int search(int depth, int ply, int alpha, int beta, bool in_check, bool null_ok);
    int quiesce(int ply, int alpha, int beta);
    void score_moves(int first, int n, Move first_choice, int ply);
    void pick(int at, int end);
    Entry *entry() const;
    void remember(int depth, int ply, int score, uint8_t bound, Move m);
    void liked(Move m, int depth, int ply);
    uint32_t random();

    Position pos_;
    Entry *table_ = nullptr;
    uint32_t entries_ = 0;

    Hooks hooks_;
    uint32_t start_ms_ = 0;
    uint32_t time_ms_ = 0;
    uint32_t node_limit_ = 0;
    uint32_t nodes_ = 0;
    uint32_t seed_ = 1;
    uintptr_t stack_top_ = 0;
    uintptr_t stack_low_ = 0;
    std::atomic<bool> stop_{false};
    bool halted_ = false;  // a limit or a stop has been seen: unwind
    bool stopped_ = false; // it was a stop

    int top_ = 0;
    Scored moves_[kMoveStack];
    Undo undo_[kMaxPly + 1];
    Move killers_[kMaxPly + 1][2];
    int16_t history_[12][64];
};

} // namespace chess
} // namespace games
