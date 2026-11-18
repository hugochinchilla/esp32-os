// Chess: the rules and the engine. See core/games/chess.hpp for what the
// window uses, and docs/journal/2026-11-18-chess-engine.md for why it is
// built this way.
//
// The board is an array of 120 squares: the 64 of the board in the middle
// of a 10 by 12 grid whose border squares are marked "off the board", so a
// piece that slides or jumps finds the edge by looking at what is there.
// Square a1 is 21, h1 is 28, a8 is 91; one rank up is +10.
#include "core/games/chess.hpp"

#include <cstring>

namespace games {
namespace chess {

namespace {

constexpr Piece kOff = 0x20;
constexpr int kInf = 32000;
// Scores past this are "mate in so many": they carry a distance.
constexpr int kMateBound = kMate - 1000;

constexpr int kKnightSteps[8] = {-21, -19, -12, -8, 8, 12, 19, 21};
constexpr int kKingSteps[8] = {-11, -9, 9, 11, -10, -1, 1, 10}; // four diagonal, four straight
constexpr int kA1 = 21, kE1 = 25, kH1 = 28, kA8 = 91, kE8 = 95, kH8 = 98;

constexpr Piece bit_of(Color c) { return static_cast<Piece>(8 << c); }
constexpr int imin(int a, int b) { return a < b ? a : b; }
constexpr int iabs(int a) { return a < 0 ? -a : a; }

// 120 to 64. Border squares give 0; nothing asks for them.
struct To64 {
    uint8_t v[120];
};
constexpr To64 make_to64()
{
    To64 t{};
    for (int s = 0; s < 64; s++) {
        t.v[21 + (s & 7) + (s >> 3) * 10] = static_cast<uint8_t>(s);
    }
    return t;
}
constexpr To64 kTo64 = make_to64();

// What a piece is worth and how much it likes each square, once for the
// middle game and once for the end game, from White's side (Black looks
// the table up upside down). Made from a few rules rather than typed in:
// the middle is better than the edge, pawns want to advance, a king hides
// in a corner while the queens are on and walks to the middle afterwards.
struct Tables {
    int16_t mg[7][64];
    int16_t eg[7][64];
};
constexpr int kValueMg[7] = {0, 100, 320, 330, 500, 900, 0};
constexpr int kValueEg[7] = {0, 120, 310, 330, 520, 920, 0};

constexpr Tables make_tables()
{
    Tables t{};
    for (int sq = 0; sq < 64; sq++) {
        const int f = sq & 7, r = sq >> 3;
        const int cf = imin(f, 7 - f);          // 0 at the edge files, 3 in the middle
        const int centre = cf + imin(r, 7 - r); // 0 in a corner, 6 in the middle
        int mg = 0, eg = 0;

        if (r >= 1 && r <= 6) {
            mg = (r - 1) * 4;
            eg = (r - 1) * 6;
            if (cf == 3) {
                mg += r == 1 ? -12 : r == 2 ? 6 : r == 3 ? 22 : r == 4 ? 24 : 0;
            } else if (cf == 2 && (r == 3 || r == 4)) {
                mg += 8;
            }
        }
        t.mg[Pawn][sq] = static_cast<int16_t>(kValueMg[Pawn] + mg);
        t.eg[Pawn][sq] = static_cast<int16_t>(kValueEg[Pawn] + eg);

        t.mg[Knight][sq] = static_cast<int16_t>(kValueMg[Knight] + centre * 10 - 35 - (r == 0 ? 8 : 0));
        t.eg[Knight][sq] = static_cast<int16_t>(kValueEg[Knight] + centre * 8 - 28);

        t.mg[Bishop][sq] = static_cast<int16_t>(kValueMg[Bishop] + centre * 5 - 14 - (r == 0 ? 8 : 0));
        t.eg[Bishop][sq] = static_cast<int16_t>(kValueEg[Bishop] + centre * 4 - 12);

        t.mg[Rook][sq] = static_cast<int16_t>(kValueMg[Rook] + cf * 4 - 4 + (r == 6 ? 15 : 0));
        t.eg[Rook][sq] = static_cast<int16_t>(kValueEg[Rook] + (r == 6 ? 10 : 0));

        t.mg[Queen][sq] = static_cast<int16_t>(kValueMg[Queen] + centre * 2 - 6);
        t.eg[Queen][sq] = static_cast<int16_t>(kValueEg[Queen] + centre * 5 - 15);

        if (r == 0) {
            mg = (f == 1 || f == 6) ? 30 : (f == 0 || f == 2 || f == 7) ? 20 : (f == 4) ? 0 : -8;
        } else {
            mg = r == 1 ? -25 : -50;
        }
        t.mg[King][sq] = static_cast<int16_t>(mg);
        t.eg[King][sq] = static_cast<int16_t>(centre * 14 - 40);
    }
    return t;
}
constexpr Tables kTables = make_tables();

constexpr int kPassedMg[8] = {0, 5, 10, 15, 30, 50, 80, 0};
constexpr int kPassedEg[8] = {0, 10, 20, 35, 60, 100, 150, 0};

// A fixed random number for every (piece, square), for each set of castling
// rights, for each file a pawn can be taken on in passing, and for Black to
// move. A position's key is the exclusive-or of those that apply, so a move
// changes it with a few operations.
struct Zobrist {
    uint64_t piece[12][64];
    uint64_t castling[16];
    uint64_t passing[8];
    uint64_t side;
};
constexpr uint64_t next_random(uint64_t &state)
{
    // splitmix64
    state += 0x9E3779B97F4A7C15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
constexpr Zobrist make_zobrist()
{
    Zobrist z{};
    uint64_t state = 0x43484943484Cull; // "CHICHL"
    for (int p = 0; p < 12; p++) {
        for (int s = 0; s < 64; s++) {
            z.piece[p][s] = next_random(state);
        }
    }
    for (int i = 0; i < 16; i++) {
        z.castling[i] = next_random(state);
    }
    for (int i = 0; i < 8; i++) {
        z.passing[i] = next_random(state);
    }
    z.side = next_random(state);
    return z;
}
constexpr Zobrist kZobrist = make_zobrist();

inline int index_of(Piece p)
{
    return color_of(p) * 6 + type_of(p) - 1;
}

// The castling rights that are left after something moves from or to `sq`.
inline uint8_t rights_kept(int sq)
{
    switch (sq) {
    case kA1: return 0x0F & ~2;
    case kE1: return 0x0F & ~3;
    case kH1: return 0x0F & ~1;
    case kA8: return 0x0F & ~8;
    case kE8: return 0x0F & ~12;
    case kH8: return 0x0F & ~4;
    default: return 0x0F;
    }
}

inline void set(Move &out, Move m) { out = m; }
inline void set(Scored &out, Move m)
{
    out.move = m;
    out.score = 0;
}

// The generator, for a list of moves or of moves with scores.
template <typename Out>
int generate_into(const Piece *board, Color us, uint8_t castling, uint8_t passing, const Position &position,
                  Out *out, bool captures)
{
    int n = 0;
    const Color them = other(us);
    const Piece mine = bit_of(us), theirs = bit_of(them);
    const int up = us == White ? 10 : -10;

    auto add = [&](int from, int to, Type promotion = NoType) {
        if (n < kMaxMoves) {
            set(out[n++], move(kTo64.v[from], kTo64.v[to], promotion));
        }
    };

    for (int s = 0; s < 64; s++) {
        const int from = 21 + (s & 7) + (s >> 3) * 10;
        const Piece p = board[from];
        if (!(p & mine)) {
            continue;
        }
        switch (type_of(p)) {
        case Pawn: {
            const bool last = us == White ? from >= 81 : from <= 38; // one step from promoting
            const bool home = us == White ? from <= 38 : from >= 81;
            int to = from + up;
            if (board[to] == kEmpty) {
                if (last) {
                    add(from, to, Queen);
                    if (!captures) {
                        add(from, to, Rook);
                        add(from, to, Bishop);
                        add(from, to, Knight);
                    }
                } else if (!captures) {
                    add(from, to);
                    if (home && board[to + up] == kEmpty) {
                        add(from, to + up);
                    }
                }
            }
            for (int side = -1; side <= 1; side += 2) {
                to = from + up + side;
                if (board[to] & theirs) {
                    if (last) {
                        add(from, to, Queen);
                        if (!captures) {
                            add(from, to, Rook);
                            add(from, to, Bishop);
                            add(from, to, Knight);
                        }
                    } else {
                        add(from, to);
                    }
                } else if (passing != 0 && to == passing) {
                    add(from, to);
                }
            }
            break;
        }
        case Knight:
            for (int step : kKnightSteps) {
                const Piece t = board[from + step];
                if ((t & theirs) || (t == kEmpty && !captures)) {
                    add(from, from + step);
                }
            }
            break;
        case King:
            for (int step : kKingSteps) {
                const Piece t = board[from + step];
                if ((t & theirs) || (t == kEmpty && !captures)) {
                    add(from, from + step);
                }
            }
            if (!captures) {
                // The king may not castle out of check or across an attacked
                // square; the square it lands on is checked like any move.
                const int home = us == White ? kE1 : kE8;
                const int rights = us == White ? castling & 3 : (castling >> 2) & 3;
                if (from == home && rights != 0) {
                    if ((rights & 1) && board[home + 1] == kEmpty && board[home + 2] == kEmpty &&
                        !position.attacked(home, them) && !position.attacked(home + 1, them)) {
                        add(home, home + 2);
                    }
                    if ((rights & 2) && board[home - 1] == kEmpty && board[home - 2] == kEmpty &&
                        board[home - 3] == kEmpty && !position.attacked(home, them) &&
                        !position.attacked(home - 1, them)) {
                        add(home, home - 2);
                    }
                }
            }
            break;
        default: {
            // Bishop: the four diagonals. Rook: the four straight lines.
            // Queen: all eight.
            const int first = type_of(p) == Rook ? 4 : 0;
            const int end = type_of(p) == Bishop ? 4 : 8;
            for (int d = first; d < end; d++) {
                const int step = kKingSteps[d];
                int to = from + step;
                while (board[to] == kEmpty) {
                    if (!captures) {
                        add(from, to);
                    }
                    to += step;
                }
                if (board[to] & theirs) {
                    add(from, to);
                }
            }
            break;
        }
        }
    }
    return n;
}

constexpr Level kLevelTable[kLevels] = {
    {1, 1000, 250},
    {2, 1000, 120},
    {3, 2000, 50},
    {5, 4000, 0},
    {kMaxDepth, 10000, 0},
};

} // namespace

const uint8_t Position::kTo120[64] = {
    21, 22, 23, 24, 25, 26, 27, 28, 31, 32, 33, 34, 35, 36, 37, 38, 41, 42, 43, 44, 45, 46,
    47, 48, 51, 52, 53, 54, 55, 56, 57, 58, 61, 62, 63, 64, 65, 66, 67, 68, 71, 72, 73, 74,
    75, 76, 77, 78, 81, 82, 83, 84, 85, 86, 87, 88, 91, 92, 93, 94, 95, 96, 97, 98,
};

void to_text(Move m, char *out)
{
    static const char kLetters[] = " pnbrqk";
    int n = 0;
    out[n++] = static_cast<char>('a' + file_of(from_of(m)));
    out[n++] = static_cast<char>('1' + rank_of(from_of(m)));
    out[n++] = static_cast<char>('a' + file_of(to_of(m)));
    out[n++] = static_cast<char>('1' + rank_of(to_of(m)));
    if (promotion_of(m) != NoType) {
        out[n++] = kLetters[promotion_of(m)];
    }
    out[n] = '\0';
}

const Level &level(int n)
{
    return kLevelTable[n < 1 ? 0 : n > kLevels ? kLevels - 1 : n - 1];
}

Limits limits_for(int n, const Position &position, uint32_t seed)
{
    const Level &l = level(n);
    Limits limits;
    limits.depth = l.depth;
    limits.time_ms = l.time_ms;
    limits.noise = l.noise;
    limits.seed = seed;
    if (limits.noise < 15 && position.move_number() <= 6) {
        limits.noise = 15;
    }
    return limits;
}

// --- Position ---

void Position::clear()
{
    memset(board_, kOff, sizeof(board_));
    for (int s = 0; s < 64; s++) {
        board_[kTo120[s]] = kEmpty;
    }
    king_[White] = king_[Black] = 0;
    side_ = White;
    castling_ = 0;
    passing_ = 0;
    halfmove_ = 0;
    fullmove_ = 1;
    memset(count_, 0, sizeof(count_));
    mg_[0] = mg_[1] = eg_[0] = eg_[1] = 0;
    key_ = 0;
    ply_ = 0;
}

void Position::put(int sq, Piece p)
{
    board_[sq] = p;
    const int c = color_of(p), t = type_of(p);
    const int s = kTo64.v[sq];
    const int rel = c == White ? s : s ^ 56;
    count_[c][t]++;
    mg_[c] = static_cast<int16_t>(mg_[c] + kTables.mg[t][rel]);
    eg_[c] = static_cast<int16_t>(eg_[c] + kTables.eg[t][rel]);
    key_ ^= kZobrist.piece[index_of(p)][s];
}

void Position::lift(int sq)
{
    const Piece p = board_[sq];
    board_[sq] = kEmpty;
    const int c = color_of(p), t = type_of(p);
    const int s = kTo64.v[sq];
    const int rel = c == White ? s : s ^ 56;
    count_[c][t]--;
    mg_[c] = static_cast<int16_t>(mg_[c] - kTables.mg[t][rel]);
    eg_[c] = static_cast<int16_t>(eg_[c] - kTables.eg[t][rel]);
    key_ ^= kZobrist.piece[index_of(p)][s];
}

void Position::push_key()
{
    ring_[ply_ & 255] = key_;
}

void Position::reset()
{
    set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

bool Position::set_fen(const char *fen)
{
    static const char kLetters[] = " pnbrqk";
    clear();
    bool ok = true;
    int file = 0, rank = 7;
    const char *c = fen;
    for (; *c != '\0' && *c != ' '; c++) {
        if (*c == '/') {
            file = 0;
            rank--;
        } else if (*c >= '1' && *c <= '8') {
            file += *c - '0';
        } else {
            const char lower = static_cast<char>(*c | 0x20);
            const char *found = strchr(kLetters + 1, lower);
            if (found == nullptr || file > 7 || rank < 0) {
                ok = false;
                break;
            }
            const Type t = static_cast<Type>(found - kLetters);
            const Color color = *c == lower ? Black : White;
            const int sq = kTo120[square(file, rank)];
            put(sq, piece(color, t));
            if (t == King) {
                king_[color] = static_cast<uint8_t>(sq);
            }
            file++;
        }
    }
    ok = ok && count_[White][King] == 1 && count_[Black][King] == 1;
    // No pawns on the first or last rank: the tables have no place for them.
    for (int f = 0; f < 8 && ok; f++) {
        ok = type_of(board_[kA1 + f]) != Pawn && type_of(board_[kA8 + f]) != Pawn;
    }
    if (ok && *c == ' ') {
        c++;
        side_ = *c == 'b' ? Black : White;
        if (*c != '\0') {
            c++;
        }
        if (*c == ' ') {
            c++;
        }
        for (; *c != '\0' && *c != ' '; c++) {
            // A right counts only with the king and the rook at home.
            if (*c == 'K' && board_[kE1] == piece(White, King) && board_[kH1] == piece(White, Rook)) {
                castling_ |= 1;
            } else if (*c == 'Q' && board_[kE1] == piece(White, King) && board_[kA1] == piece(White, Rook)) {
                castling_ |= 2;
            } else if (*c == 'k' && board_[kE8] == piece(Black, King) && board_[kH8] == piece(Black, Rook)) {
                castling_ |= 4;
            } else if (*c == 'q' && board_[kE8] == piece(Black, King) && board_[kA8] == piece(Black, Rook)) {
                castling_ |= 8;
            }
        }
        if (*c == ' ') {
            c++;
        }
        if (c[0] >= 'a' && c[0] <= 'h' && (c[1] == '3' || c[1] == '6')) {
            const int sq = kTo120[square(c[0] - 'a', c[1] - '1')];
            // Kept only if a pawn stands ready to take there, so that two
            // positions with the same moves have the same key.
            const int down = side_ == White ? -10 : 10;
            const Piece taker = piece(side(), Pawn);
            if (board_[sq] == kEmpty && board_[sq + down] == piece(other(side()), Pawn) &&
                (board_[sq + down - 1] == taker || board_[sq + down + 1] == taker)) {
                passing_ = static_cast<uint8_t>(sq);
            }
        }
        while (*c != '\0' && *c != ' ') {
            c++;
        }
        int numbers[2] = {0, 1};
        for (int i = 0; i < 2 && *c == ' '; i++) {
            c++;
            int value = 0;
            while (*c >= '0' && *c <= '9') {
                value = value * 10 + (*c - '0');
                c++;
            }
            numbers[i] = value;
        }
        halfmove_ = static_cast<uint8_t>(numbers[0] > 200 ? 200 : numbers[0]);
        fullmove_ = static_cast<uint16_t>(numbers[1] < 1 ? 1 : numbers[1]);
    }
    // The side that has just moved may not have left its king in check.
    if (ok && attacked(king_[side_ ^ 1], side())) {
        ok = false;
    }
    if (!ok) {
        reset();
        return false;
    }
    key_ = key_from_scratch();
    push_key();
    return true;
}

uint64_t Position::key_from_scratch() const
{
    uint64_t key = 0;
    for (int s = 0; s < 64; s++) {
        const Piece p = board_[kTo120[s]];
        if (p != kEmpty) {
            key ^= kZobrist.piece[index_of(p)][s];
        }
    }
    key ^= kZobrist.castling[castling_];
    if (passing_ != 0) {
        key ^= kZobrist.passing[kTo64.v[passing_] & 7];
    }
    if (side_ == Black) {
        key ^= kZobrist.side;
    }
    return key;
}

bool Position::sums_match() const
{
    int mg[2] = {0, 0}, eg[2] = {0, 0};
    uint8_t count[2][7];
    memset(count, 0, sizeof(count));
    for (int s = 0; s < 64; s++) {
        const Piece p = board_[kTo120[s]];
        if (p != kEmpty) {
            const int c = color_of(p), t = type_of(p);
            const int rel = c == White ? s : s ^ 56;
            mg[c] += kTables.mg[t][rel];
            eg[c] += kTables.eg[t][rel];
            count[c][t]++;
        }
    }
    return mg[0] == mg_[0] && mg[1] == mg_[1] && eg[0] == eg_[0] && eg[1] == eg_[1] &&
           memcmp(count, count_, sizeof(count)) == 0 && board_[king_[White]] == piece(White, King) &&
           board_[king_[Black]] == piece(Black, King);
}

int Position::king(Color c) const
{
    return kTo64.v[king_[c]];
}

int Position::points(Color c) const
{
    return count_[c][Pawn] + 3 * (count_[c][Knight] + count_[c][Bishop]) + 5 * count_[c][Rook] +
           9 * count_[c][Queen];
}

bool Position::attacked(int sq, Color by) const
{
    const Piece bit = bit_of(by);
    // A white pawn attacks the two squares diagonally above it.
    const int down = by == White ? -10 : 10;
    const Piece pawn = static_cast<Piece>(Pawn | bit);
    if (board_[sq + down - 1] == pawn || board_[sq + down + 1] == pawn) {
        return true;
    }
    const Piece knight = static_cast<Piece>(Knight | bit);
    for (int step : kKnightSteps) {
        if (board_[sq + step] == knight) {
            return true;
        }
    }
    const Piece king = static_cast<Piece>(King | bit);
    const Piece queen = static_cast<Piece>(Queen | bit);
    for (int d = 0; d < 8; d++) {
        const int step = kKingSteps[d];
        int at = sq + step;
        Piece p = board_[at];
        if (p == king) {
            return true;
        }
        while (p == kEmpty) {
            at += step;
            p = board_[at];
        }
        if (p == queen || p == static_cast<Piece>((d < 4 ? Bishop : Rook) | bit)) {
            return true;
        }
    }
    return false;
}

int Position::generate(Scored *out, bool captures) const
{
    return generate_into(board_, side(), castling_, passing_, *this, out, captures);
}

bool Position::quiet(Move m) const
{
    const int from = kTo120[from_of(m)], to = kTo120[to_of(m)];
    if (board_[to] != kEmpty || promotion_of(m) != NoType) {
        return false;
    }
    // A pawn that changes file onto an empty square takes in passing.
    return !(type_of(board_[from]) == Pawn && file_of(from_of(m)) != file_of(to_of(m)));
}

bool Position::make(Move m, Undo &undo)
{
    const int from = kTo120[from_of(m)], to = kTo120[to_of(m)];
    const Type promotion = promotion_of(m);
    const Piece p = board_[from];
    const Color us = side(), them = other(us);
    const int up = us == White ? 10 : -10;

    undo.key = key_;
    undo.move = m;
    undo.captured = board_[to];
    undo.castling = castling_;
    undo.passing = passing_;
    undo.halfmove = halfmove_;

    if (passing_ != 0) {
        key_ ^= kZobrist.passing[kTo64.v[passing_] & 7];
    }
    key_ ^= kZobrist.castling[castling_];
    const int could_pass = passing_;
    passing_ = 0;
    if (halfmove_ < 255) {
        halfmove_++;
    }

    if (undo.captured != kEmpty) {
        lift(to);
        halfmove_ = 0;
    }
    if (type_of(p) == Pawn) {
        halfmove_ = 0;
        if (could_pass != 0 && to == could_pass) {
            undo.captured = board_[to - up];
            lift(to - up);
        } else if (to - from == 2 * up) {
            const Piece taker = piece(them, Pawn);
            if (board_[to - 1] == taker || board_[to + 1] == taker) {
                passing_ = static_cast<uint8_t>(from + up);
            }
        }
    }
    lift(from);
    put(to, promotion != NoType ? piece(us, promotion) : p);
    if (type_of(p) == King) {
        king_[us] = static_cast<uint8_t>(to);
        if (to - from == 2) {
            const Piece rook = board_[to + 1];
            lift(to + 1);
            put(to - 1, rook);
        } else if (from - to == 2) {
            const Piece rook = board_[to - 2];
            lift(to - 2);
            put(to + 1, rook);
        }
    }
    castling_ &= rights_kept(from) & rights_kept(to);
    key_ ^= kZobrist.castling[castling_];
    if (passing_ != 0) {
        key_ ^= kZobrist.passing[kTo64.v[passing_] & 7];
    }
    side_ ^= 1;
    key_ ^= kZobrist.side;
    if (us == Black) {
        fullmove_++;
    }
    ply_++;
    push_key();

    if (attacked(king_[us], them)) {
        unmake(undo);
        return false;
    }
    return true;
}

void Position::unmake(const Undo &undo)
{
    const Move m = undo.move;
    const int from = kTo120[from_of(m)], to = kTo120[to_of(m)];
    ply_--;
    side_ ^= 1;
    const Color us = side();
    const int up = us == White ? 10 : -10;
    if (us == Black) {
        fullmove_--;
    }

    const Piece p = promotion_of(m) != NoType ? piece(us, Pawn) : board_[to];
    lift(to);
    put(from, p);
    if (type_of(p) == King) {
        king_[us] = static_cast<uint8_t>(from);
        if (to - from == 2) {
            const Piece rook = board_[to - 1];
            lift(to - 1);
            put(to + 1, rook);
        } else if (from - to == 2) {
            const Piece rook = board_[to + 1];
            lift(to + 1);
            put(to - 2, rook);
        }
    }
    if (undo.captured != kEmpty) {
        if (type_of(p) == Pawn && undo.passing != 0 && to == undo.passing) {
            put(to - up, undo.captured);
        } else {
            put(to, undo.captured);
        }
    }
    castling_ = undo.castling;
    passing_ = undo.passing;
    halfmove_ = undo.halfmove;
    key_ = undo.key;
}

void Position::make_null(Undo &undo)
{
    undo.key = key_;
    undo.passing = passing_;
    undo.halfmove = halfmove_;
    if (passing_ != 0) {
        key_ ^= kZobrist.passing[kTo64.v[passing_] & 7];
        passing_ = 0;
    }
    // Nothing before a null move counts as a repetition of what follows.
    halfmove_ = 0;
    side_ ^= 1;
    key_ ^= kZobrist.side;
    ply_++;
    push_key();
}

void Position::unmake_null(const Undo &undo)
{
    ply_--;
    side_ ^= 1;
    passing_ = undo.passing;
    halfmove_ = undo.halfmove;
    key_ = undo.key;
}

int Position::legal_moves(Move *out)
{
    const int n = generate_into(board_, side(), castling_, passing_, *this, out, false);
    int kept = 0;
    Undo undo;
    for (int i = 0; i < n; i++) {
        if (make(out[i], undo)) {
            unmake(undo);
            out[kept++] = out[i];
        }
    }
    return kept;
}

bool Position::legal(Move m)
{
    Move moves[kMaxMoves];
    const int n = legal_moves(moves);
    for (int i = 0; i < n; i++) {
        if (moves[i] == m) {
            return true;
        }
    }
    return false;
}

Move Position::parse(const char *text)
{
    Move moves[kMaxMoves];
    const int n = legal_moves(moves);
    char name[6];
    for (int i = 0; i < n; i++) {
        to_text(moves[i], name);
        if (strcmp(name, text) == 0) {
            return moves[i];
        }
    }
    return kNoMove;
}

bool Position::play(Move m)
{
    if (!legal(m)) {
        return false;
    }
    Undo undo;
    return make(m, undo);
}

bool Position::repeated(int times) const
{
    const uint32_t back = halfmove_ < ply_ ? halfmove_ : ply_;
    int found = 0;
    for (uint32_t i = 2; i <= back; i += 2) {
        if (ring_[(ply_ - i) & 255] == key_ && ++found >= times) {
            return true;
        }
    }
    return false;
}

bool Position::bare() const
{
    for (int c = 0; c < 2; c++) {
        if (count_[c][Pawn] != 0 || count_[c][Rook] != 0 || count_[c][Queen] != 0) {
            return false;
        }
    }
    const int knights = count_[White][Knight] + count_[Black][Knight];
    const int bishops = count_[White][Bishop] + count_[Black][Bishop];
    if (knights + bishops <= 1) {
        return true;
    }
    if (knights == 0 && count_[White][Bishop] == 1 && count_[Black][Bishop] == 1) {
        // A bishop each on squares of the same colour.
        int shade[2] = {0, 0}, n = 0;
        for (int s = 0; s < 64 && n < 2; s++) {
            if (type_of(board_[kTo120[s]]) == Bishop) {
                shade[n++] = (file_of(s) + rank_of(s)) & 1;
            }
        }
        return shade[0] == shade[1];
    }
    return false;
}

bool Position::has_pieces() const
{
    const uint8_t *c = count_[side_];
    return c[Knight] + c[Bishop] + c[Rook] + c[Queen] != 0;
}

Status Position::status()
{
    Move moves[kMaxMoves];
    if (legal_moves(moves) == 0) {
        return in_check() ? Status::Checkmate : Status::Stalemate;
    }
    if (bare()) {
        return Status::DrawMaterial;
    }
    if (repeated(2)) {
        return Status::DrawRepetition;
    }
    if (fifty()) {
        return Status::DrawFifty;
    }
    return Status::Playing;
}

int Position::evaluate() const
{
    int mg = mg_[White] - mg_[Black];
    int eg = eg_[White] - eg_[Black];

    // The pawns of each file: how many, the rearmost and the foremost rank.
    // Index 0 and 9 are the files off the board.
    uint8_t pawns[2][10];
    int8_t low[2][10], high[2][10];
    memset(pawns, 0, sizeof(pawns));
    memset(low, 8, sizeof(low));
    memset(high, -1, sizeof(high));
    uint8_t pawn_at[16], rook_at[20];
    int pawn_count = 0, rook_count = 0;
    for (int s = 0; s < 64; s++) {
        const Piece p = board_[kTo120[s]];
        if (p == kEmpty) {
            continue;
        }
        if (type_of(p) == Pawn) {
            const int c = color_of(p), f = (s & 7) + 1, r = s >> 3;
            pawns[c][f]++;
            if (r < low[c][f]) {
                low[c][f] = static_cast<int8_t>(r);
            }
            if (r > high[c][f]) {
                high[c][f] = static_cast<int8_t>(r);
            }
            if (pawn_count < 16) {
                pawn_at[pawn_count++] = static_cast<uint8_t>(s);
            }
        } else if (type_of(p) == Rook && rook_count < 20) {
            rook_at[rook_count++] = static_cast<uint8_t>(s);
        }
    }

    for (int i = 0; i < pawn_count; i++) {
        const int s = pawn_at[i];
        const int c = color_of(board_[kTo120[s]]), f = (s & 7) + 1, r = s >> 3;
        int m = 0, e = 0;
        bool passed;
        if (c == White) {
            passed = high[Black][f - 1] <= r && high[Black][f] <= r && high[Black][f + 1] <= r;
        } else {
            passed = low[White][f - 1] >= r && low[White][f] >= r && low[White][f + 1] >= r;
        }
        if (passed) {
            const int rel = c == White ? r : 7 - r;
            m += kPassedMg[rel];
            e += kPassedEg[rel];
        }
        if (pawns[c][f - 1] == 0 && pawns[c][f + 1] == 0) { // nobody beside it to defend it
            m -= 10;
            e -= 14;
        }
        if (pawns[c][f] > 1) { // two on one file
            m -= 8;
            e -= 12;
        }
        if (c == White) {
            mg += m;
            eg += e;
        } else {
            mg -= m;
            eg -= e;
        }
    }

    for (int i = 0; i < rook_count; i++) {
        const int s = rook_at[i];
        const int c = color_of(board_[kTo120[s]]), f = (s & 7) + 1;
        if (pawns[c][f] == 0) { // a file with no pawn of its own in the way
            const int bonus = pawns[c ^ 1][f] == 0 ? 20 : 10;
            mg += c == White ? bonus : -bonus;
            eg += c == White ? bonus / 2 : -bonus / 2;
        }
    }

    for (int c = 0; c < 2; c++) {
        const int sign = c == White ? 1 : -1;
        if (count_[c][Bishop] >= 2) {
            mg += sign * 30;
            eg += sign * 40;
        }
        // Pawns in front of a king that has stayed at home.
        const int s = kTo64.v[king_[c]];
        const int f = (s & 7) + 1, r = c == White ? s >> 3 : 7 - (s >> 3);
        if (r <= 1) {
            int shield = 0;
            for (int x = f - 1; x <= f + 1; x++) {
                if (x < 1 || x > 8) {
                    continue;
                }
                const int nearest = c == White ? low[White][x] : 7 - high[Black][x];
                shield += nearest == r + 1 ? 10 : nearest == r + 2 ? 4 : -8;
            }
            mg += sign * shield;
        }
    }

    // With the other side down to a bare king or nearly, and no pawns to
    // queen: push that king to the edge and bring ours next to it.
    for (int c = 0; c < 2; c++) {
        const int weak = c ^ 1;
        if (count_[weak][Pawn] != 0) {
            continue;
        }
        const int lead = (points(static_cast<Color>(c)) - count_[c][Pawn]) - points(static_cast<Color>(weak));
        if (lead < 4) {
            continue;
        }
        const int k = kTo64.v[king_[weak]], own = kTo64.v[king_[c]];
        const int centre = imin(k & 7, 7 - (k & 7)) + imin(k >> 3, 7 - (k >> 3));
        const int apart = iabs((k & 7) - (own & 7)) + iabs((k >> 3) - (own >> 3));
        const int bonus = (6 - centre) * 12 + (14 - apart) * 5;
        eg += c == White ? bonus : -bonus;
    }

    int phase = count_[0][Knight] + count_[1][Knight] + count_[0][Bishop] + count_[1][Bishop] +
                2 * (count_[0][Rook] + count_[1][Rook]) + 4 * (count_[0][Queen] + count_[1][Queen]);
    if (phase > 24) {
        phase = 24;
    }
    const int score = (mg * phase + eg * (24 - phase)) / 24;
    return (side_ == White ? score : -score) + 10; // a little for having the move
}

// --- Game ---

void Game::reset()
{
    position_.reset();
    plies_ = 0;
    memset(taken_, 0, sizeof(taken_));
}

void Game::apply(Move m)
{
    Piece victim = position_.at(to_of(m));
    const Piece mover = position_.at(from_of(m));
    if (victim == kEmpty && type_of(mover) == Pawn && file_of(from_of(m)) != file_of(to_of(m))) {
        victim = piece(other(color_of(mover)), Pawn); // taken in passing
    }
    if (victim != kEmpty) {
        taken_[color_of(victim)][type_of(victim)]++;
    }
    Undo undo;
    position_.make(m, undo);
}

bool Game::play(Move m)
{
    if (!position_.legal(m)) {
        return false;
    }
    apply(m);
    if (plies_ < kMaxKept) {
        moves_[plies_] = m;
    }
    plies_++;
    return true;
}

bool Game::take_back(int plies)
{
    if (plies < 1 || plies > plies_ || plies_ > kMaxKept) {
        return false;
    }
    // Played again from the start: the game keeps the moves, not every
    // position it has been through.
    const int keep = plies_ - plies;
    position_.reset();
    memset(taken_, 0, sizeof(taken_));
    for (int i = 0; i < keep; i++) {
        apply(moves_[i]);
    }
    plies_ = keep;
    return true;
}

// --- Engine ---

static_assert(sizeof(Scored) == 4, "a scored move is four bytes");

Engine::Engine()
{
    static_assert(sizeof(Entry) == kEntryBytes, "kEntryBytes is out of date");
    new_game();
}

void Engine::set_table(void *memory, size_t bytes)
{
    table_ = static_cast<Entry *>(memory);
    entries_ = memory != nullptr ? static_cast<uint32_t>(bytes / sizeof(Entry)) : 0;
    if (entries_ == 0) {
        table_ = nullptr;
    }
    new_game();
}

void Engine::new_game()
{
    if (table_ != nullptr) {
        memset(static_cast<void *>(table_), 0, static_cast<size_t>(entries_) * sizeof(Entry));
    }
    memset(killers_, 0, sizeof(killers_));
    memset(history_, 0, sizeof(history_));
}

uint32_t Engine::random()
{
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
}

// Counts a position. False once the search has to unwind.
inline bool Engine::tick()
{
    if (halted_) {
        return false;
    }
    if ((++nodes_ & (kPollNodes - 1)) == 0) {
        check();
    }
    return !halted_;
}

void Engine::check()
{
    if (stop_.load(std::memory_order_relaxed) || (hooks_.poll != nullptr && hooks_.poll(hooks_.context))) {
        halted_ = stopped_ = true;
        return;
    }
    if (node_limit_ != 0 && nodes_ >= node_limit_) {
        halted_ = true;
    }
    if (time_ms_ != 0 && hooks_.now_ms(hooks_.context) - start_ms_ >= time_ms_) {
        halted_ = true;
    }
}

inline void Engine::note_stack()
{
    char mark;
    const uintptr_t here = reinterpret_cast<uintptr_t>(&mark);
    if (here < stack_low_) {
        stack_low_ = here;
    }
}

Engine::Entry *Engine::entry() const
{
    if (table_ == nullptr) {
        return nullptr;
    }
    const uint32_t low = static_cast<uint32_t>(pos_.key());
    return &table_[(static_cast<uint64_t>(low) * entries_) >> 32];
}

void Engine::remember(int depth, int ply, int score, uint8_t bound, Move m)
{
    Entry *e = entry();
    if (e == nullptr) {
        return;
    }
    const uint32_t lock = static_cast<uint32_t>(pos_.key() >> 32);
    if (e->lock == lock && e->depth > depth && bound != 3) {
        return; // what is there was worked out more deeply
    }
    // A mate is stored as its distance from this position, not from the
    // root of the search that found it.
    if (score > kMateBound) {
        score += ply;
    } else if (score < -kMateBound) {
        score -= ply;
    }
    if (m == kNoMove && e->lock == lock) {
        m = e->move;
    }
    e->lock = lock;
    e->move = m;
    e->score = static_cast<int16_t>(score);
    e->depth = static_cast<uint8_t>(depth);
    e->bound = bound;
}

void Engine::score_moves(int first, int n, Move first_choice, int ply)
{
    for (int i = first; i < first + n; i++) {
        const Move m = moves_[i].move;
        int s;
        if (m == first_choice) {
            s = 32000;
        } else {
            const Piece mover = pos_.at(from_of(m));
            const Piece victim = pos_.at(to_of(m));
            const Type promotion = promotion_of(m);
            if (victim != kEmpty) {
                // The most valuable victim first, by the least valuable taker.
                s = 20000 + type_of(victim) * 16 - type_of(mover) + (promotion == Queen ? 64 : 0);
            } else if (promotion != NoType) {
                s = promotion == Queen ? 19000 : 100;
            } else if (type_of(mover) == Pawn && file_of(from_of(m)) != file_of(to_of(m))) {
                s = 20000 + Pawn * 16 - Pawn;
            } else if (m == killers_[ply][0]) {
                s = 15000;
            } else if (m == killers_[ply][1]) {
                s = 14000;
            } else {
                s = history_[index_of(mover)][to_of(m)];
            }
        }
        moves_[i].score = static_cast<int16_t>(s);
    }
}

// Brings the best of what is left to `at`.
void Engine::pick(int at, int end)
{
    int best = at;
    for (int i = at + 1; i < end; i++) {
        if (moves_[i].score > moves_[best].score) {
            best = i;
        }
    }
    if (best != at) {
        const Scored t = moves_[at];
        moves_[at] = moves_[best];
        moves_[best] = t;
    }
}

// A quiet move refuted the other side's play here: try it early next time.
void Engine::liked(Move m, int depth, int ply)
{
    if (killers_[ply][0] != m) {
        killers_[ply][1] = killers_[ply][0];
        killers_[ply][0] = m;
    }
    int16_t &h = history_[index_of(pos_.at(from_of(m)))][to_of(m)];
    h = static_cast<int16_t>(h + depth * depth);
    if (h > 12000) {
        for (auto &row : history_) {
            for (int16_t &value : row) {
                value = static_cast<int16_t>(value / 2);
            }
        }
    }
}

// The end of a line: captures are followed until the position is calm, so
// that a piece hanging at the last move is not taken for a gain.
int Engine::quiesce(int ply, int alpha, int beta)
{
    if (!tick()) {
        return 0;
    }
    note_stack();
    const int stand = pos_.evaluate();
    if (stand >= beta || ply >= kMaxPly || kMoveStack - top_ < kMaxMoves) {
        return stand;
    }
    if (stand > alpha) {
        alpha = stand;
    }
    const int first = top_;
    const int n = pos_.generate(moves_ + first, true);
    top_ = first + n;
    score_moves(first, n, kNoMove, ply);
    int best = stand;
    for (int i = first; i < first + n; i++) {
        pick(i, first + n);
        const Move m = moves_[i].move;
        // Not worth a look if even winning the piece for nothing leaves
        // the score short.
        const Type victim = type_of(pos_.at(to_of(m)));
        if (promotion_of(m) == NoType && stand + kValueEg[victim == NoType ? Pawn : victim] + 200 <= alpha) {
            continue;
        }
        if (!pos_.make(m, undo_[ply])) {
            continue;
        }
        const int score = -quiesce(ply + 1, -beta, -alpha);
        pos_.unmake(undo_[ply]);
        if (halted_) {
            break;
        }
        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) {
                    break;
                }
            }
        }
    }
    top_ = first;
    return best;
}

// The search: the best score the side to move can be sure of, looking
// `depth` half moves ahead, given that it already has `alpha` elsewhere and
// the other side will not allow more than `beta`.
int Engine::search(int depth, int ply, int alpha, int beta, bool in_check, bool null_ok)
{
    if (pos_.repeated() || pos_.fifty() || pos_.bare()) {
        return 0;
    }
    if (in_check) {
        depth++; // a check is answered before the line is judged
    }
    if (depth <= 0) {
        return quiesce(ply, alpha, beta);
    }
    if (!tick()) {
        return 0;
    }
    note_stack();
    if (ply >= kMaxPly || kMoveStack - top_ < kMaxMoves) {
        return pos_.evaluate();
    }

    const bool narrow = beta - alpha == 1;
    const int alpha_before = alpha;
    Move first_choice = kNoMove;
    const Entry *e = entry();
    if (e != nullptr && e->lock == static_cast<uint32_t>(pos_.key() >> 32)) {
        first_choice = e->move;
        if (narrow && e->depth >= depth) {
            int score = e->score;
            if (score > kMateBound) {
                score -= ply;
            } else if (score < -kMateBound) {
                score += ply;
            }
            if (e->bound == 3 || (e->bound == 2 && score >= beta) || (e->bound == 1 && score <= alpha)) {
                return score;
            }
        }
    }

    // If handing the turn over still leaves the score above beta, a real
    // move will too. Not with only king and pawns, where having to move
    // can be what loses.
    if (null_ok && narrow && !in_check && depth >= 3 && pos_.has_pieces() && pos_.evaluate() >= beta) {
        pos_.make_null(undo_[ply]);
        const int score = -search(depth - 3, ply + 1, -beta, -beta + 1, false, false);
        pos_.unmake_null(undo_[ply]);
        if (halted_) {
            return 0;
        }
        if (score >= beta) {
            return score > kMateBound ? beta : score;
        }
    }

    const int first = top_;
    const int n = pos_.generate(moves_ + first, false);
    top_ = first + n;
    score_moves(first, n, first_choice, ply);

    int best = -kInf;
    Move best_move = kNoMove;
    int legal = 0;
    for (int i = first; i < first + n; i++) {
        pick(i, first + n);
        const Move m = moves_[i].move;
        const bool quiet = pos_.quiet(m);
        if (!pos_.make(m, undo_[ply])) {
            continue;
        }
        legal++;
        const bool checks = pos_.in_check();
        int score;
        if (legal == 1) {
            score = -search(depth - 1, ply + 1, -beta, -alpha, checks, true);
        } else {
            // Later moves are unlikely to be better: look at the quiet ones
            // less deeply and with a window of nothing, and look again
            // properly only if one surprises.
            int less = 0;
            if (depth >= 3 && legal > 3 && quiet && !in_check && !checks) {
                less = legal > 8 ? 2 : 1;
            }
            score = -search(depth - 1 - less, ply + 1, -alpha - 1, -alpha, checks, true);
            if (less != 0 && score > alpha && !halted_) {
                score = -search(depth - 1, ply + 1, -alpha - 1, -alpha, checks, true);
            }
            if (score > alpha && score < beta && !halted_) {
                score = -search(depth - 1, ply + 1, -beta, -alpha, checks, true);
            }
        }
        pos_.unmake(undo_[ply]);
        if (halted_) {
            top_ = first;
            return 0;
        }
        if (score > best) {
            best = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) {
                    if (quiet) {
                        liked(m, depth, ply);
                    }
                    break;
                }
            }
        }
    }
    top_ = first;

    if (legal == 0) {
        return in_check ? -kMate + ply : 0; // mated, or stalemate
    }
    remember(depth, ply, best, best >= beta ? 2 : best > alpha_before ? 3 : 1, best_move);
    return best;
}

Result Engine::think(const Position &position, const Limits &limits, const Hooks &hooks)
{
    char mark;
    stack_top_ = stack_low_ = reinterpret_cast<uintptr_t>(&mark);
    pos_ = position;
    hooks_ = hooks;
    stop_.store(false, std::memory_order_relaxed);
    halted_ = stopped_ = false;
    nodes_ = 0;
    node_limit_ = limits.nodes;
    time_ms_ = hooks.now_ms != nullptr ? limits.time_ms : 0;
    start_ms_ = hooks.now_ms != nullptr ? hooks.now_ms(hooks.context) : 0;
    seed_ = limits.seed != 0 ? limits.seed : 1;
    memset(killers_, 0, sizeof(killers_));

    Result result;

    // The legal moves, at the bottom of the move stack for the whole search.
    int n = 0;
    {
        const int all = pos_.generate(moves_, false);
        for (int i = 0; i < all; i++) {
            if (pos_.make(moves_[i].move, undo_[0])) {
                pos_.unmake(undo_[0]);
                moves_[n++] = moves_[i];
            }
        }
    }
    if (n == 0) {
        return result;
    }
    top_ = n;
    const Entry *e = entry();
    score_moves(0, n, e != nullptr && e->lock == static_cast<uint32_t>(pos_.key() >> 32) ? e->move : kNoMove, 0);
    for (int i = 0; i < n; i++) {
        pick(i, n);
    }
    result.best = moves_[0].move;

    int max_depth = limits.depth < 1 ? 1 : limits.depth > kMaxDepth ? kMaxDepth : limits.depth;
    if (n == 1) {
        max_depth = 1; // nothing to choose
    }

    // One half move deeper each time round, the best move so far tried
    // first: the early rounds cost little and put the moves in a good order
    // for the later ones, and there is always an answer when time runs out.
    for (int depth = 1; depth <= max_depth; depth++) {
        int alpha = -kInf;
        int best = -1;
        for (int i = 0; i < n; i++) {
            pos_.make(moves_[i].move, undo_[0]);
            const bool checks = pos_.in_check();
            int score;
            if (i == 0) {
                score = -search(depth - 1, 1, -kInf, -alpha, checks, true);
            } else {
                score = -search(depth - 1, 1, -alpha - 1, -alpha, checks, true);
                if (score > alpha && !halted_) {
                    score = -search(depth - 1, 1, -kInf, -alpha, checks, true);
                }
            }
            pos_.unmake(undo_[0]);
            if (halted_) {
                break;
            }
            if (score > alpha) {
                alpha = score;
                best = i;
            }
        }
        // A round cut short still counts as far as it got: the first move
        // it tried was the best of the round before.
        if (best >= 0) {
            result.best = moves_[best].move;
            result.score = alpha;
            const Scored chosen = moves_[best];
            for (int i = best; i > 0; i--) {
                moves_[i] = moves_[i - 1];
            }
            moves_[0] = chosen;
        }
        if (halted_) {
            break;
        }
        result.depth = depth;
        remember(depth, 0, alpha, 3, result.best);
        if (alpha >= kMate - depth || alpha <= -kMate + depth) {
            break; // a mate, and no shorter one to find
        }
        // Another round takes several times as long as this one did.
        if (time_ms_ != 0 && (hooks_.now_ms(hooks_.context) - start_ms_) * 2 >= time_ms_) {
            break;
        }
    }

    // Mistakes on purpose: every move within `noise` of the best gets a
    // random bonus below `noise`, and the highest total is played. Never
    // when there is a mate to give or to avoid.
    if (limits.noise > 0 && n > 1 && result.depth >= 1 && !halted_ && result.score < kMateBound &&
        result.score > -kMateBound) {
        const int floor = result.score - limits.noise;
        Move chosen = result.best;
        int chosen_score = result.score;
        int chosen_total = result.score + static_cast<int>(random() % static_cast<uint32_t>(limits.noise));
        for (int i = 1; i < n; i++) {
            pos_.make(moves_[i].move, undo_[0]);
            const int score = -search(result.depth - 1, 1, -kInf, -floor, pos_.in_check(), true);
            pos_.unmake(undo_[0]);
            if (halted_) {
                break;
            }
            if (score > floor && score < kMateBound) {
                const int total = score + static_cast<int>(random() % static_cast<uint32_t>(limits.noise));
                if (total > chosen_total) {
                    chosen_total = total;
                    chosen_score = score;
                    chosen = moves_[i].move;
                }
            }
        }
        result.best = chosen;
        result.score = chosen_score;
    }

    result.nodes = nodes_;
    result.time_ms = hooks.now_ms != nullptr ? hooks.now_ms(hooks.context) - start_ms_ : 0;
    result.stack_bytes = static_cast<uint32_t>(stack_top_ - stack_low_);
    result.stopped = stopped_;
    return result;
}

} // namespace chess
} // namespace games
