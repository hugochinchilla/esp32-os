// Chess: the rules counted against the figures every chess program is
// checked with, the draws, the special moves, and the engine: positions
// with one right answer, its limits, its stop, and its five levels.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/games/chess.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

using namespace games::chess;

const char *const kKiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

// How many ways the game can go on for `depth` half moves. The figures for
// the positions below are published and agreed on by every program; one
// wrong rule (a castle through check, a missed capture in passing) shows as
// a wrong count.
uint64_t perft(Position &p, int depth, bool audit = false)
{
    Scored moves[kMaxMoves];
    const int n = p.generate(moves, false);
    uint64_t total = 0;
    for (int i = 0; i < n; i++) {
        Undo undo;
        const uint64_t before = p.key();
        if (!p.make(moves[i].move, undo)) {
            if (audit) {
                CHECK(p.key() == before && p.sums_match());
            }
            continue;
        }
        if (audit) {
            CHECK(p.key() == p.key_from_scratch());
            CHECK(p.sums_match());
        }
        total += depth <= 1 ? 1 : perft(p, depth - 1, audit);
        p.unmake(undo);
        if (audit) {
            CHECK(p.key() == before && p.sums_match());
        }
    }
    return total;
}

uint64_t perft(const char *fen, int depth)
{
    Position p;
    CHECK(p.set_fen(fen));
    return perft(p, depth);
}

void test_perft()
{
    Position start;
    CHECK(perft(start, 1) == 20);
    CHECK(perft(start, 2) == 400);
    CHECK(perft(start, 3) == 8902);
    CHECK(perft(start, 4) == 197281);

    // "Kiwipete": castling both ways for both sides, pins, captures in
    // passing and promotions close by.
    CHECK(perft(kKiwipete, 1) == 48);
    CHECK(perft(kKiwipete, 2) == 2039);
    CHECK(perft(kKiwipete, 3) == 97862);
    CHECK(perft(kKiwipete, 4) == 4085603);

    // An end game where taking in passing would uncover a check.
    const char *three = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    CHECK(perft(three, 1) == 14);
    CHECK(perft(three, 2) == 191);
    CHECK(perft(three, 3) == 2812);
    CHECK(perft(three, 4) == 43238);
    CHECK(perft(three, 5) == 674624);

    // Promotions with and without capture, and checks from them.
    const char *four = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
    CHECK(perft(four, 1) == 6);
    CHECK(perft(four, 2) == 264);
    CHECK(perft(four, 3) == 9467);
    CHECK(perft(four, 4) == 422333);

    const char *five = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
    CHECK(perft(five, 1) == 44);
    CHECK(perft(five, 2) == 1486);
    CHECK(perft(five, 3) == 62379);

    const char *six = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10";
    CHECK(perft(six, 1) == 46);
    CHECK(perft(six, 2) == 2079);
    CHECK(perft(six, 3) == 89890);
}

// The key and the running totals are kept up move by move; after every
// move made and taken back they must equal what the board alone gives.
void test_bookkeeping()
{
    Position p;
    CHECK(p.set_fen(kKiwipete));
    CHECK(perft(p, 3, true) == 97862);
    CHECK(p.set_fen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"));
    CHECK(perft(p, 3, true) == 9467);
    CHECK(p.set_fen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"));
    CHECK(perft(p, 4, true) == 43238);

    // The same position reached by two orders of moves has one key.
    Position a, b;
    for (const char *m : {"g1f3", "g8f6", "b1c3", "b8c6"}) {
        CHECK(a.play(a.parse(m)));
    }
    for (const char *m : {"b1c3", "b8c6", "g1f3", "g8f6"}) {
        CHECK(b.play(b.parse(m)));
    }
    CHECK(a.key() == b.key());
    // A double step nobody can take in passing leaves no mark on the key.
    Position c, d;
    CHECK(c.set_fen("4k3/8/8/8/4P3/8/8/4K3 b - e3 0 1"));
    CHECK(d.set_fen("4k3/8/8/8/4P3/8/8/4K3 b - - 0 1"));
    CHECK(c.key() == d.key());
}

// The position seen from the other side: colours swapped, board turned.
std::string mirrored(const char *fen)
{
    std::string text(fen);
    const size_t space = text.find(' ');
    std::string board = text.substr(0, space), rest = text.substr(space + 1);
    std::string ranks[8];
    int r = 0;
    for (char c : board) {
        if (c == '/') {
            r++;
        } else {
            ranks[r] += static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
    }
    std::string out;
    for (int i = 7; i >= 0; i--) {
        out += ranks[i];
        if (i > 0) {
            out += '/';
        }
    }
    out += rest[0] == 'w' ? " b " : " w ";
    std::string castling = rest.substr(2, rest.find(' ', 2) - 2);
    for (char &c : castling) {
        c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    return out + castling + " - 0 1";
}

void test_evaluation()
{
    Position start;
    CHECK(start.evaluate() == 10); // level, plus the move
    // The same position with the colours swapped is worth the same to the
    // side to move.
    for (const char *fen : {kKiwipete, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                            "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
                            "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", "8/8/8/4k3/8/8/8/KQ6 w - - 0 1",
                            "4k3/pp4pp/8/8/3P4/8/P5PP/2R1K3 b - - 0 1"}) {
        Position a, b;
        CHECK(a.set_fen(fen));
        CHECK(b.set_fen(mirrored(fen).c_str()));
        CHECK(a.evaluate() == b.evaluate());
    }
    // A queen up is good for whoever has it.
    Position p;
    CHECK(p.set_fen("4k3/8/8/8/8/8/8/3QK3 w - - 0 1"));
    CHECK(p.evaluate() > 800);
    CHECK(p.set_fen("4k3/8/8/8/8/8/8/3QK3 b - - 0 1"));
    CHECK(p.evaluate() < -800);
    CHECK(p.points(White) == 9 && p.points(Black) == 0);
}

void play_all(Position &p, std::initializer_list<const char *> moves)
{
    for (const char *m : moves) {
        const Move move = p.parse(m);
        CHECK(move != kNoMove);
        CHECK(p.play(move));
    }
}

void test_endings()
{
    Position p;
    CHECK(p.status() == Status::Playing);
    CHECK(p.side() == White && p.move_number() == 1 && !p.in_check());
    CHECK(p.at(square(4, 0)) == piece(White, King) && p.at(square(3, 7)) == piece(Black, Queen));
    CHECK(p.king(White) == square(4, 0) && p.king(Black) == square(4, 7));

    // The shortest mate there is.
    play_all(p, {"f2f3", "e7e5", "g2g4", "d8h4"});
    CHECK(p.status() == Status::Checkmate && p.side() == White && p.in_check());
    CHECK(p.move_number() == 3);
    Move none[kMaxMoves];
    CHECK(p.legal_moves(none) == 0);

    CHECK(p.set_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
    CHECK(p.status() == Status::Stalemate && !p.in_check());

    CHECK(p.set_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1"));
    CHECK(p.status() == Status::DrawMaterial);
    CHECK(p.set_fen("4k3/8/8/8/8/8/8/4KN2 w - - 0 1"));
    CHECK(p.status() == Status::DrawMaterial);
    CHECK(p.set_fen("4k3/8/8/8/8/8/8/3BKN2 w - - 0 1"));
    CHECK(p.status() == Status::Playing); // bishop and knight can mate
    CHECK(p.set_fen("2b1k3/8/8/8/8/8/8/4KB2 w - - 0 1")); // c8 and f1: both light squares
    CHECK(p.status() == Status::DrawMaterial);
    CHECK(p.set_fen("3bk3/8/8/8/8/8/8/4KB2 w - - 0 1")); // d8 is dark
    CHECK(p.status() == Status::Playing);
    CHECK(p.set_fen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));
    CHECK(p.status() == Status::Playing);

    // The same position three times: the knights go out and back twice.
    p.reset();
    play_all(p, {"g1f3", "g8f6", "f3g1", "f6g8"});
    CHECK(p.status() == Status::Playing && p.repeated(1) && !p.repeated(2));
    play_all(p, {"g1f3", "g8f6", "f3g1"});
    CHECK(p.status() == Status::Playing);
    play_all(p, {"f6g8"});
    CHECK(p.status() == Status::DrawRepetition);
    // Losing the right to castle makes it another position.
    p.reset();
    play_all(p, {"e2e4", "e7e5", "e1e2", "e8e7", "e2e1", "e7e8", "e1e2", "e8e7", "e2e1", "e7e8"});
    CHECK(p.status() == Status::Playing); // the first of these still had the rights
    play_all(p, {"e1e2", "e8e7", "e2e1", "e7e8"});
    CHECK(p.status() == Status::DrawRepetition);

    // Fifty moves each with nothing taken and no pawn moved.
    CHECK(p.set_fen("4k3/8/8/8/8/8/R7/4K3 w - - 99 80"));
    CHECK(p.status() == Status::Playing);
    play_all(p, {"a2a3"});
    CHECK(p.status() == Status::DrawFifty);

    // Positions that cannot be: refused, and the first position given.
    CHECK(!p.set_fen("8/8/8/8/8/8/8/4K3 w - - 0 1"));               // one king
    CHECK(!p.set_fen("4k3/8/8/8/8/8/8/4RK2 w - - 0 1"));            // black in check, white to move
    CHECK(!p.set_fen("4k3/8/8/8/8/8/8/P3K3 w - - 0 1"));            // a pawn on the first rank
    CHECK(p.at(square(0, 1)) == piece(White, Pawn) && p.status() == Status::Playing);
}

int count_moves(Position &p, const char *prefix)
{
    Move moves[kMaxMoves];
    const int n = p.legal_moves(moves);
    int found = 0;
    char text[6];
    for (int i = 0; i < n; i++) {
        to_text(moves[i], text);
        found += strncmp(text, prefix, strlen(prefix)) == 0;
    }
    return found;
}

void test_special_moves()
{
    Position p;
    // Castling: both ways, and the rook comes across.
    CHECK(p.set_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    CHECK(p.parse("e1g1") != kNoMove && p.parse("e1c1") != kNoMove);
    play_all(p, {"e1g1"});
    CHECK(p.at(square(6, 0)) == piece(White, King) && p.at(square(5, 0)) == piece(White, Rook));
    CHECK(p.at(square(7, 0)) == kEmpty && p.at(square(4, 0)) == kEmpty);
    play_all(p, {"e8c8"});
    CHECK(p.at(square(2, 7)) == piece(Black, King) && p.at(square(3, 7)) == piece(Black, Rook));
    CHECK(p.at(square(0, 7)) == kEmpty);
    // Not out of check, not across an attacked square, not with something
    // in between; the rook may cross an attacked square (b1).
    CHECK(p.set_fen("4k3/8/8/8/8/4r3/8/R3K2R w KQ - 0 1"));
    CHECK(p.parse("e1g1") == kNoMove && p.parse("e1c1") == kNoMove);
    CHECK(p.set_fen("4k3/8/8/8/8/5r2/8/R3K2R w KQ - 0 1"));
    CHECK(p.parse("e1g1") == kNoMove && p.parse("e1c1") != kNoMove);
    CHECK(p.set_fen("4k3/8/8/8/8/1r6/8/R3K2R w KQ - 0 1"));
    CHECK(p.parse("e1g1") != kNoMove && p.parse("e1c1") != kNoMove);
    CHECK(p.set_fen("4k3/8/8/8/8/8/8/RN2K2R w KQ - 0 1"));
    CHECK(p.parse("e1c1") == kNoMove && p.parse("e1g1") != kNoMove);
    // Not once the king or that rook has moved, even if it came back.
    CHECK(p.set_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    play_all(p, {"h1h2", "a8a7", "h2h1", "a7a8"});
    CHECK(p.parse("e1g1") == kNoMove && p.parse("e1c1") != kNoMove);
    play_all(p, {"a1a2"});
    CHECK(p.parse("e8c8") == kNoMove && p.parse("e8g8") != kNoMove);
    // A rook taken at home takes the right with it.
    CHECK(p.set_fen("r3k2r/8/8/8/8/8/6B1/4K3 w kq - 0 1"));
    play_all(p, {"g2a8"});
    CHECK(p.parse("e8c8") == kNoMove && p.parse("e8g8") != kNoMove);
    // A right the board does not back is dropped.
    CHECK(p.set_fen("4k3/8/8/8/8/8/8/4K3 w KQkq - 0 1"));
    CHECK(p.parse("e1g1") == kNoMove);

    // Taking in passing: at once or never.
    p.reset();
    play_all(p, {"e2e4", "a7a6", "e4e5", "d7d5"});
    CHECK(p.parse("e5d6") != kNoMove);
    play_all(p, {"e5d6"});
    CHECK(p.at(square(3, 4)) == kEmpty && p.at(square(3, 5)) == piece(White, Pawn));
    CHECK(p.count(Black, Pawn) == 7);
    p.reset();
    play_all(p, {"e2e4", "a7a6", "e4e5", "d7d5", "h2h3", "h7h6"});
    CHECK(p.parse("e5d6") == kNoMove);

    // A pawn on the last rank becomes one of four pieces.
    CHECK(p.set_fen("1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1"));
    CHECK(count_moves(p, "a7a8") == 4 && count_moves(p, "a7b8") == 4);
    CHECK(p.parse("a7a8") == kNoMove); // it has to say which
    play_all(p, {"a7b8n"});
    CHECK(p.at(square(1, 7)) == piece(White, Knight) && p.count(White, Pawn) == 0);
    CHECK(p.count(Black, Knight) == 0);

    // A pinned piece stays put; a check has to be answered.
    CHECK(p.set_fen("4k3/4r3/8/8/8/8/4N3/4K3 w - - 0 1"));
    CHECK(count_moves(p, "e2") == 0);
    CHECK(p.set_fen("4k3/8/8/8/8/8/3PPP2/r3K3 w - - 0 1"));
    Move moves[kMaxMoves];
    CHECK(p.in_check() && p.legal_moves(moves) == 0 && p.status() == Status::Checkmate);

    char text[6];
    to_text(move(square(4, 6), square(4, 7), Queen), text);
    CHECK(strcmp(text, "e7e8q") == 0);
    CHECK(!p.play(move(square(0, 0), square(0, 5)))); // not a move: nothing happens
}

void test_game()
{
    Game g;
    CHECK(g.plies() == 0 && g.last() == kNoMove && !g.take_back());
    Position scratch;
    auto play = [&](const char *text) {
        const Move m = scratch.parse(text);
        CHECK(m != kNoMove && g.play(m));
        scratch.play(m);
    };
    for (const char *m : {"e2e4", "d7d5", "e4d5", "d8d5", "b1c3", "d5e5", "f1e2", "e5g5", "g1f3", "g5g2"}) {
        play(m);
    }
    CHECK(g.plies() == 10 && g.position().key() == scratch.key());
    CHECK(g.taken(Black, Pawn) == 1 && g.taken(White, Pawn) == 2 && g.taken(White, Queen) == 0);
    CHECK(g.position().points(White) == 37 && g.position().points(Black) == 38);
    CHECK(!g.play(move(square(0, 0), square(0, 4))) && g.plies() == 10);

    // Take back: the last half move, then back to after 1. e4 d5.
    CHECK(g.take_back());
    CHECK(g.plies() == 9 && g.taken(White, Pawn) == 1 && g.position().side() == Black);
    CHECK(g.position().at(square(6, 1)) == piece(White, Pawn));
    CHECK(g.take_back(7) && g.plies() == 2);
    CHECK(g.taken(Black, Pawn) == 0 && g.taken(White, Pawn) == 0);
    CHECK(!g.take_back(3));
    Position two;
    play_all(two, {"e2e4", "d7d5"});
    CHECK(g.position().key() == two.key());

    // Taking in passing is counted as a pawn taken.
    g.reset();
    scratch.reset();
    for (const char *m : {"e2e4", "a7a6", "e4e5", "d7d5", "e5d6"}) {
        play(m);
    }
    CHECK(g.taken(Black, Pawn) == 1);
    CHECK(g.status() == Status::Playing);
}

// --- The engine ---

struct Table {
    explicit Table(Engine &engine, size_t bytes = 256 * 1024) : memory(new char[bytes])
    {
        engine.set_table(memory, bytes);
    }
    ~Table() { delete[] memory; }
    char *memory;
};

std::string best(Engine &engine, const char *fen, int depth, int *score = nullptr)
{
    Position p;
    CHECK(p.set_fen(fen));
    Limits limits;
    limits.depth = depth;
    const Result r = engine.think(p, limits);
    CHECK(p.legal(r.best));
    if (score != nullptr) {
        *score = r.score;
    }
    char text[6];
    to_text(r.best, text);
    return text;
}

void test_tactics()
{
    // Each position has one right answer; found with and without the table.
    for (int with_table = 0; with_table < 2; with_table++) {
        Engine engine;
        Table table(engine, with_table ? 256 * 1024 : 0);
        int score = 0;
        // Mate in one, on the back rank.
        CHECK(best(engine, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", 3, &score) == "a1a8");
        CHECK(score == kMate - 1);
        // The same for Black.
        CHECK(best(engine, "r5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1", 3, &score) == "a8a1");
        CHECK(score == kMate - 1);
        // Mate in two: the knight checks, the pawn must take, the bishop mates.
        CHECK(best(engine, "r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 1", 4, &score) ==
              "d5f6");
        CHECK(score == kMate - 3);
        // A queen left hanging is taken.
        CHECK(best(engine, "4k3/8/8/3q4/8/8/3R4/4K3 w - - 0 1", 2) == "d2d5");
        // A knight's fork of king and rook.
        CHECK(best(engine, "r3k3/8/8/3N4/8/8/8/4K3 w - - 0 1", 4) == "d5c7");
        // The pawn takes the knight and becomes a queen with check, rather
        // than just stepping forward.
        CHECK(best(engine, "1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1", 3) == "a7b8q");
        // Being mated next move whatever is played: a legal move still comes
        // back, with the score that says so.
        best(engine, "7k/8/7P/8/8/1r6/r7/7K w - - 0 1", 4, &score);
        CHECK(score == -kMate + 2);
        // A queen up and the move: do not stalemate (Qf7 would), mate comes.
        best(engine, "7k/8/6K1/8/8/8/8/5Q2 w - - 0 1", 4, &score);
        CHECK(score > kMate - 10);
        // Two rooks and a knight against the queen, but the queen can check
        // for ever: the draw is seen (a score of nothing) and a check played.
        CHECK(best(engine, "6k1/5p1p/8/8/8/4Q3/rr3PPP/n5K1 w - - 0 1", 6, &score) == "e3g5");
        CHECK(score == 0);
    }

    // No legal move: no move.
    Engine engine;
    Position p;
    CHECK(p.set_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
    CHECK(engine.think(p, Limits()).best == kNoMove);
}

// The engine against itself from `fen` with `depth` for both. Returns the
// status at the end, at most `max_plies` later.
Status play_out(const char *fen, int depth, int max_plies, int *plies = nullptr)
{
    Engine engine;
    Table table(engine);
    Position p;
    CHECK(p.set_fen(fen));
    Limits limits;
    limits.depth = depth;
    int n = 0;
    while (p.status() == Status::Playing && n < max_plies) {
        const Result r = engine.think(p, limits);
        CHECK(p.play(r.best));
        n++;
    }
    if (plies != nullptr) {
        *plies = n;
    }
    return p.status();
}

void test_mates_with_few_pieces()
{
    // King and queen, then king and rook, against a king in the middle that
    // defends as well as the engine can: mate well before the fifty moves.
    int plies = 0;
    CHECK(play_out("8/8/8/4k3/8/8/8/KQ6 w - - 0 1", 6, 60, &plies) == Status::Checkmate);
    printf("king and queen mate in %d half moves\n", plies);
    CHECK(play_out("8/8/8/4k3/8/8/8/KR6 w - - 0 1", 8, 80, &plies) == Status::Checkmate);
    printf("king and rook mate in %d half moves\n", plies);
    // And a pawn is walked home and the game won.
    CHECK(play_out("8/8/8/8/8/k7/7P/7K w - - 0 1", 8, 120, &plies) == Status::Checkmate);
    printf("king and pawn win in %d half moves\n", plies);
}

struct FakeClock {
    uint32_t now = 1000;
    uint32_t step = 1;
    int polls = 0;
    int stop_after = -1;       // polls before poll() says stop
    Engine *stop_from = nullptr; // or: call stop() on this engine at that poll
};

uint32_t fake_now(void *context)
{
    FakeClock *clock = static_cast<FakeClock *>(context);
    clock->now += clock->step;
    return clock->now;
}

bool fake_poll(void *context)
{
    FakeClock *clock = static_cast<FakeClock *>(context);
    clock->polls++;
    if (clock->stop_after >= 0 && clock->polls > clock->stop_after) {
        if (clock->stop_from != nullptr) {
            clock->stop_from->stop();
            return false;
        }
        return true;
    }
    return false;
}

void test_limits()
{
    Position p;
    CHECK(p.set_fen(kKiwipete));
    Engine engine;
    Table table(engine);

    // Depth: no deeper than asked, and deeper costs more.
    Limits limits;
    limits.depth = 1;
    Result one = engine.think(p, limits);
    CHECK(one.depth == 1 && p.legal(one.best) && !one.stopped);
    engine.new_game();
    limits.depth = 5;
    Result five = engine.think(p, limits);
    CHECK(five.depth == 5 && p.legal(five.best) && five.nodes > one.nodes * 10);
    printf("kiwipete to depth 5: %u positions, %u bytes of stack\n", five.nodes, five.stack_bytes);
#if !defined(__SANITIZE_ADDRESS__)
    // (The checked build pads every frame, so its figure says nothing.)
    CHECK(five.stack_bytes > 0 && five.stack_bytes < 8 * 1024);
#endif

    // The same question twice has the same answer.
    engine.new_game();
    Result again = engine.think(p, limits);
    CHECK(again.best == five.best && again.nodes == five.nodes && again.score == five.score);

    // Positions: stops within one poll of the figure, with a move.
    engine.new_game();
    limits = Limits();
    limits.nodes = 3000;
    Result few = engine.think(p, limits);
    CHECK(few.nodes >= 3000 && few.nodes <= 3000 + Engine::kPollNodes && p.legal(few.best));
    CHECK(few.depth >= 1 && few.depth < kMaxDepth && !few.stopped);

    // Time, on a clock that moves one millisecond each time it is read.
    engine.new_game();
    FakeClock clock;
    Hooks hooks;
    hooks.now_ms = &fake_now;
    hooks.poll = &fake_poll;
    hooks.context = &clock;
    limits = Limits();
    limits.time_ms = 40;
    Result timed = engine.think(p, limits, hooks);
    CHECK(p.legal(timed.best) && !timed.stopped && timed.depth >= 1);
    CHECK(timed.time_ms >= 20 && timed.time_ms <= 60);
    CHECK(clock.polls > 0 && timed.nodes < 40 * Engine::kPollNodes * 2);
    // Without a clock a time limit cannot be kept and is not applied.
    engine.new_game();
    limits.depth = 3;
    Result untimed = engine.think(p, limits);
    CHECK(untimed.depth == 3 && untimed.time_ms == 0);

    // Stop from the poll: at once, with a legal move, and said so.
    engine.new_game();
    clock = FakeClock();
    clock.stop_after = 5;
    limits = Limits();
    Result stopped = engine.think(p, limits, hooks);
    CHECK(stopped.stopped && p.legal(stopped.best));
    CHECK(clock.polls == 6 && stopped.nodes == 6u * Engine::kPollNodes);
    // Stop at the very first poll, before anything is finished.
    engine.new_game();
    clock = FakeClock();
    clock.stop_after = 0;
    stopped = engine.think(p, limits, hooks);
    CHECK(stopped.stopped && p.legal(stopped.best) && stopped.nodes == Engine::kPollNodes);
    // stop(), as another task would call it.
    engine.new_game();
    clock = FakeClock();
    clock.stop_after = 3;
    clock.stop_from = &engine;
    stopped = engine.think(p, limits, hooks);
    CHECK(stopped.stopped && p.legal(stopped.best));
    CHECK(stopped.nodes <= 5u * Engine::kPollNodes);
    // And the engine thinks again afterwards.
    limits.depth = 2;
    clock = FakeClock();
    Result after = engine.think(p, limits, hooks);
    CHECK(!after.stopped && after.depth == 2);

    // The table saves work and does not change what is legal. The move
    // stack never runs out even in a position with 218 moves.
    Engine bare;
    limits = Limits();
    limits.depth = 6;
    Position start;
    Result without = bare.think(start, limits);
    engine.new_game();
    Result with = engine.think(start, limits);
    printf("first position to depth 6: %u positions with the table, %u without\n", with.nodes, without.nodes);
    CHECK(start.legal(without.best) && start.legal(with.best) && with.nodes < without.nodes);
    CHECK(p.set_fen("R6R/3Q4/1Q4Q1/4Q3/2Q4Q/Q4Q2/pp1Q4/kBNN1KB1 w - - 0 1"));
    Move moves[kMaxMoves];
    CHECK(p.legal_moves(moves) == 218);
    limits.depth = 3;
    Result many = bare.think(p, limits);
    CHECK(p.legal(many.best) && many.score > kMate - 10);
}

// A game between two settings. Returns +1 if White wins, -1 if Black, 0 for
// a draw or a game not over after 300 half moves.
int match(const Limits &white, const Limits &black, uint32_t seed)
{
    Engine a, b;
    Table table_a(a, 64 * 1024), table_b(b, 64 * 1024);
    Game game;
    while (game.status() == Status::Playing && game.plies() < 300) {
        const bool whites = game.position().side() == White;
        Limits limits = whites ? white : black;
        limits.seed = seed * 7919 + static_cast<uint32_t>(game.plies()) + 1;
        const Result r = (whites ? a : b).think(game.position(), limits);
        CHECK(game.play(r.best));
    }
    if (game.status() != Status::Checkmate) {
        return 0;
    }
    return game.position().side() == Black ? 1 : -1;
}

void test_levels()
{
    // Five, each asking for more than the one before.
    for (int n = 2; n <= kLevels; n++) {
        CHECK(level(n).depth > level(n - 1).depth);
        CHECK(level(n).time_ms >= level(n - 1).time_ms);
        CHECK(level(n).noise <= level(n - 1).noise);
    }
    CHECK(level(1).noise > 0 && level(kLevels).noise == 0);
    CHECK(&level(0) == &level(1) && &level(99) == &level(kLevels));

    Position start;
    Limits limits = limits_for(1, start, 42);
    CHECK(limits.depth == level(1).depth && limits.noise == level(1).noise && limits.seed == 42);
    CHECK(limits.time_ms == level(1).time_ms);
    // The strongest level varies its first moves a little, and only those.
    CHECK(limits_for(5, start, 1).noise == 15);
    Position later;
    CHECK(later.set_fen("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"));
    CHECK(limits_for(5, later, 1).noise == 0);

    // The same seed, the same move; other seeds, other moves.
    Engine engine;
    Table table(engine);
    limits = limits_for(1, start, 7);
    const Move first = engine.think(start, limits).best;
    engine.new_game();
    CHECK(engine.think(start, limits).best == first);
    int different = 0;
    for (uint32_t seed = 1; seed <= 12; seed++) {
        engine.new_game();
        const Result r = engine.think(start, limits_for(1, start, seed));
        CHECK(start.legal(r.best));
        different += r.best != first;
    }
    CHECK(different >= 3);
    // Noise never passes up a mate, and never walks into one it can see.
    Position mate;
    CHECK(mate.set_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
    for (uint32_t seed = 1; seed <= 8; seed++) {
        Limits noisy;
        noisy.depth = 2;
        noisy.noise = 500;
        noisy.seed = seed;
        CHECK(engine.think(mate, noisy).best == mate.parse("a1a8"));
    }
    // A queen for nothing is more than level 1's mistakes allow it to pass
    // up: every level takes it.
    Position hanging;
    CHECK(hanging.set_fen("4k3/pp3ppp/8/3q4/8/8/PP1R1PPP/4K3 w - - 0 20"));
    int taken_by_1 = 0, taken_by_4 = 0;
    for (uint32_t seed = 1; seed <= 20; seed++) {
        taken_by_1 += engine.think(hanging, limits_for(1, hanging, seed)).best == hanging.parse("d2d5");
        taken_by_4 += engine.think(hanging, limits_for(4, hanging, seed)).best == hanging.parse("d2d5");
    }
    printf("a queen to take: level 1 took it %d times in 20, level 4 %d\n", taken_by_1, taken_by_4);
    CHECK(taken_by_4 == 20 && taken_by_1 == 20);

    // Each level against the one below, both colours, by depth and noise
    // alone (no clock on the PC, so level 5 is given depth 6 here): the
    // higher level does not lose the pair.
    for (int n = 2; n <= kLevels; n++) {
        Limits high = limits_for(n, later, 1), low = limits_for(n - 1, later, 1);
        if (high.depth > 6) {
            high.depth = 6;
        }
        int score = 0;
        for (uint32_t seed = 1; seed <= 2; seed++) {
            score += match(high, low, seed);
            score -= match(low, high, seed + 100);
        }
        printf("level %d against level %d: %+d over 4 games\n", n, n - 1, score);
        CHECK(score > 0);
    }
}

} // namespace

int main()
{
    test_perft();
    test_bookkeeping();
    test_evaluation();
    test_endings();
    test_special_moves();
    test_game();
    test_tactics();
    test_mates_with_few_pieces();
    test_limits();
    test_levels();
    if (failures == 0) {
        printf("chess: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
