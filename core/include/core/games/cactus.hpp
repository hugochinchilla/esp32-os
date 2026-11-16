// Cactus Patch: the rules of the game, with no drawing and no input. A field
// of covered tiles hides some cacti; an open tile shows how many of its
// eight neighbours hold one. Open every tile without a cactus to win.
#pragma once

#include <cstdint>

namespace games {

class CactusField {
public:
    static constexpr int kMaxCols = 30, kMaxRows = 16;
    static constexpr int kMaxTiles = kMaxCols * kMaxRows;

    enum class State : uint8_t {
        Ready,   // nothing opened yet: the cacti are not planted
        Playing,
        Won,
        Lost,
    };

    // What a tile looks like to the player.
    enum class Tile : uint8_t {
        Open0, Open1, Open2, Open3, Open4, Open5, Open6, Open7, Open8,
        Covered,
        Marked,      // a marker planted by the player
        Cactus,      // shown when the game is lost
        CactusHit,   // the one that was stepped on
        WrongMarker, // a marker where there was no cactus, shown when lost
    };

    CactusField() { reset(9, 9, 10); }

    // A new game. Sizes are held to 1..kMax, and the cacti to at most all
    // tiles but one.
    void reset(int cols, int rows, int cacti);

    // Opens a covered, unmarked tile. The first one of a game plants the
    // cacti from `seed`, never on that tile, nor on its neighbours when the
    // field has room. An empty tile opens its neighbours too. Returns true
    // if anything changed.
    bool open(int col, int row, uint32_t seed);
    // Plants or removes a marker on a covered tile.
    bool toggle_marker(int col, int row);
    // On an open number with that many markers around it: opens its other
    // neighbours.
    bool open_around(int col, int row);
    // Tests: plants cacti at the given tiles (index = row * cols + col)
    // instead of at random, and starts the game.
    void plant(const uint16_t *tiles, int count);

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    int cacti() const { return cacti_; }
    State state() const { return state_; }
    bool over() const { return state_ == State::Won || state_ == State::Lost; }
    // Cacti less markers; below zero with too many markers.
    int unmarked() const { return cacti_ - markers_; }
    Tile tile(int col, int row) const;
    bool has_cactus(int col, int row) const { return inside(col, row) && (at(col, row) & kCactus); }

private:
    enum : uint8_t { kCactus = 1, kOpen = 2, kMarker = 4, kCountShift = 4 };

    bool inside(int col, int row) const { return col >= 0 && col < cols_ && row >= 0 && row < rows_; }
    uint8_t at(int col, int row) const { return tiles_[row * cols_ + col]; }
    uint8_t &at(int col, int row) { return tiles_[row * cols_ + col]; }
    void plant_random(int col, int row, uint32_t seed);
    void count_neighbours();
    void open_from(int col, int row);
    void check_won();

    uint8_t tiles_[kMaxTiles];
    uint16_t pending_[kMaxTiles]; // tiles still to open (no recursion)
    int cols_ = 0, rows_ = 0, cacti_ = 0;
    int markers_ = 0, opened_ = 0;
    int hit_ = -1;
    State state_ = State::Ready;
};

} // namespace games
