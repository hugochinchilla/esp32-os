// The rules of the snake game (the program is Culebra, in the shell): a
// snake on a grid that grows by one for every berry it eats and loses when
// it runs into a wall or itself. No drawing and no clock in here: the window
// calls step() from a timer and paints what changed().
#pragma once

#include <cstdint>

namespace games {

class Snake {
public:
    static constexpr int kCols = 24;
    static constexpr int kRows = 18;
    static constexpr int kCells = kCols * kRows;
    static constexpr int kStartLength = 3;
    static constexpr int kMaxChanged = 5;

    enum class Dir : uint8_t { Up, Right, Down, Left };
    enum class State : uint8_t {
        Ready,   // a new game, waiting for the first direction
        Playing,
        Paused,
        Over,    // ran into a wall or itself
        Won,     // the snake fills the board
    };
    enum class Kind : uint8_t { Empty, Berry, Body, Head };

    // What is in a cell. For the snake, `links` says which neighbours the
    // segment joins on to: bit 0 up, 1 right, 2 down, 3 left.
    struct Cell {
        Kind kind;
        uint8_t links;
    };

    Snake() { reset(1); }

    // A new game: the snake in the middle heading right, one berry. The
    // same seed always gives the same berries.
    void reset(uint32_t seed);

    // Turns the snake at its next step (and the one after: two turns can be
    // waiting). Turning back on itself is ignored. Starts a new game moving
    // and ends a pause. Returns true if the state changed.
    bool steer(Dir d);
    // Pause or carry on. Returns true if the state changed.
    bool toggle_pause();
    bool pause();

    // Moves the snake one cell. Only does anything while playing. Returns
    // true if a berry was eaten.
    bool step();

    State state() const { return state_; }
    int length() const { return length_; }
    int berries() const { return length_ - kStartLength; }
    Dir heading() const { return heading_; }
    Cell at(int col, int row) const;
    int head_col() const { return body_[head_] % kCols; }
    int head_row() const { return body_[head_] / kCols; }
    int berry_col() const { return berry_ % kCols; }
    int berry_row() const { return berry_ / kCols; }

    // The cells whose picture changed in the last step() or reset() (after
    // reset(): -1, everything).
    int changed_count() const { return changed_count_; }
    int changed_col(int i) const { return changed_[i] % kCols; }
    int changed_row(int i) const { return changed_[i] / kCols; }

    // Tests: puts the berry in a cell (which must be empty).
    void place_berry(int col, int row) { berry_ = static_cast<int16_t>(row * kCols + col); }

private:
    static constexpr uint8_t kOccupied = 0x10;

    void link(int from, int to);
    void unlink(int from, int to);
    void changed(int cell);
    void new_berry();
    uint32_t random();

    State state_ = State::Ready;
    Dir heading_ = Dir::Right;
    Dir turns_[2] = {};       // waiting turns, oldest first
    uint8_t turn_count_ = 0;
    int16_t body_[kCells] = {}; // a ring of cell numbers; the tail is the oldest
    int16_t head_ = 0;          // index in body_
    int16_t length_ = 0;
    int16_t berry_ = -1;        // cell number, -1 when the board is full
    uint8_t grid_[kCells] = {}; // kOccupied and the link bits
    int16_t changed_[kMaxChanged] = {};
    int8_t changed_count_ = 0;
    uint32_t seed_ = 1;
};

} // namespace games
