// Chess bench: measures the chess engine (core/src/games/chess.cpp) on the
// real board. Not one of the programs that come with the system: it is
// built only when asked for and copied to the board for a board check.
//
//   EXTRA_PROGRAMS=tools/bench/chess_bench/ tools/build_programs.sh <firmware build>
//   python3 tools/dbg.py put build/programs/chess_bench.app 'C:\chess_bench.app'
//
// Opened (Run, or the File manager), it lets each of the five levels think
// about three positions, as a game would, on the program's own task, and
// writes what it measured to C:\chess_bench.txt: for each, the move, how
// deep it looked, how many positions, how long, and the stack it used.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

#include "../../../core/src/games/chess.cpp"
#include "core/app/app.hpp"
#include "core/os/os.hpp"

namespace {

using namespace games::chess;

constexpr size_t kTableBytes = 256 * 1024;
constexpr const char *kPositions[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",               // the first move
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", // a middle game
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 40",                               // an end game
};
constexpr int kPositionCount = 3;
constexpr int kSteps = kLevels * kPositionCount;

uint32_t now_ms(void *)
{
    return static_cast<uint32_t>(os::now_us() / 1000);
}

class BenchWindow : public app::Window {
public:
    BenchWindow() : line_(label({16, 16, 300, 14}, "Starting...")), close_(button({126, 44, 75, 23}, "Close"))
    {
        void *memory = os::allocate(sizeof(Engine));
        table_ = os::allocate(kTableBytes);
        if (memory != nullptr) {
            engine_ = new (memory) Engine();
            engine_->set_table(table_, table_ != nullptr ? kTableBytes : 0);
        }
        add("chess bench: engine %u bytes, table %u bytes%s\n", static_cast<unsigned>(sizeof(Engine)),
            static_cast<unsigned>(table_ != nullptr ? kTableBytes : 0), engine_ != nullptr ? "" : " (NO MEMORY)");
        add("level position move depth nodes ms nodes/s stack\n");
        timer_ = os::start_timer(200000, 200000, &BenchWindow::tick, this);
    }

    ~BenchWindow() override
    {
        if (timer_ != 0) {
            os::stop_timer(timer_);
        }
        os::release(engine_);
        os::release(table_);
    }

    void command(app::Control which) override
    {
        if (which == close_) {
            close();
        }
    }

private:
    void add(const char *format, ...) __attribute__((format(printf, 2, 3)))
    {
        va_list args;
        va_start(args, format);
        if (used_ < sizeof(text_)) {
            const int n = vsnprintf(text_ + used_, sizeof(text_) - used_, format, args);
            if (n > 0) {
                used_ += static_cast<size_t>(n);
                if (used_ > sizeof(text_) - 1) {
                    used_ = sizeof(text_) - 1;
                }
            }
        }
        va_end(args);
    }

    // One think a tick, so the window is painted in between.
    static void tick(void *context)
    {
        static_cast<BenchWindow *>(context)->step();
    }

    void step()
    {
        if (step_ >= kSteps || engine_ == nullptr) {
            os::stop_timer(timer_);
            timer_ = 0;
            add("total: %u nodes in %u ms\n", static_cast<unsigned>(nodes_), static_cast<unsigned>(ms_));
            const ports::FsError e = os::file::write_file("C:\\chess_bench.txt", text_, used_);
            line_.set_text(e == ports::FsError::Ok ? "Done: C:\\chess_bench.txt" : "Done, but the file was not written");
            return;
        }
        const int lvl = step_ / kPositionCount + 1, which = step_ % kPositionCount;
        Position position;
        position.set_fen(kPositions[which]);
        Limits limits = limits_for(lvl, position, static_cast<uint32_t>(step_ + 1));
        Hooks hooks;
        hooks.now_ms = &now_ms;
        engine_->new_game();
        const Result r = engine_->think(position, limits, hooks);
        char move_text[6];
        to_text(r.best, move_text);
        const unsigned ms = r.time_ms != 0 ? r.time_ms : 1;
        add("%d %d %s %d %u %u %u %u\n", lvl, which + 1, move_text, r.depth, static_cast<unsigned>(r.nodes),
            static_cast<unsigned>(r.time_ms), static_cast<unsigned>(static_cast<uint64_t>(r.nodes) * 1000 / ms),
            static_cast<unsigned>(r.stack_bytes));
        nodes_ += r.nodes;
        ms_ += r.time_ms;
        step_++;
        snprintf(status_, sizeof(status_), "Level %d, position %d done (%d of %d)", lvl, which + 1, step_, kSteps);
        line_.set_text(status_);
    }

    app::Label line_;
    app::Button close_;
    Engine *engine_ = nullptr;
    void *table_ = nullptr;
    int timer_ = 0;
    int step_ = 0;
    uint32_t nodes_ = 0, ms_ = 0;
    size_t used_ = 0;
    char status_[64] = "";
    char text_[1536] = "";
};

} // namespace

// 16 KB of stack: the search is the deepest thing the program does, and how
// much it really takes is one of the figures wanted.
APP_INFO(app::file_info("Chess bench", 16 * 1024));

APP_MAIN(int, char **)
{
    app::Look look;
    look.title = "Chess bench";
    look.width = 340;
    look.height = 110;
    look.flags = app::kDialog;
    return app::open<BenchWindow>(look) != app::kNone ? 0 : 1;
}
