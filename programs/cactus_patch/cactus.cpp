#include "core/games/cactus.hpp"

namespace games {

namespace {

int clamp(int v, int low, int high) { return v < low ? low : (v > high ? high : v); }

// xorshift32: small, and the same on the board and on the PC.
uint32_t next(uint32_t &s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

} // namespace

void CactusField::reset(int cols, int rows, int cacti)
{
    cols_ = clamp(cols, 1, kMaxCols);
    rows_ = clamp(rows, 1, kMaxRows);
    cacti_ = clamp(cacti, 0, cols_ * rows_ - 1);
    for (int i = 0; i < cols_ * rows_; i++) {
        tiles_[i] = 0;
    }
    markers_ = 0;
    opened_ = 0;
    hit_ = -1;
    state_ = State::Ready;
}

void CactusField::plant_random(int col, int row, uint32_t seed)
{
    // The tiles that may hold a cactus: all but the first one opened and,
    // if enough are left, its neighbours.
    const int total = cols_ * rows_;
    int around = 0;
    for (int r = row - 1; r <= row + 1; r++) {
        for (int c = col - 1; c <= col + 1; c++) {
            around += inside(c, r);
        }
    }
    const bool spare_neighbours = total - around >= cacti_;
    int free_count = 0;
    for (int i = 0; i < total; i++) {
        const int c = i % cols_, r = i / cols_;
        const int dc = c - col, dr = r - row;
        const bool near = dc >= -1 && dc <= 1 && dr >= -1 && dr <= 1;
        if (spare_neighbours ? !near : (dc != 0 || dr != 0)) {
            pending_[free_count++] = static_cast<uint16_t>(i);
        }
    }
    uint32_t s = seed != 0 ? seed : 0x9E3779B9u;
    for (int i = 0; i < 4; i++) {
        next(s); // close seeds give different fields
    }
    for (int i = 0; i < cacti_; i++) {
        const int pick = i + static_cast<int>(next(s) % static_cast<uint32_t>(free_count - i));
        const uint16_t chosen = pending_[pick];
        pending_[pick] = pending_[i];
        tiles_[chosen] |= kCactus;
    }
    count_neighbours();
    state_ = State::Playing;
}

void CactusField::plant(const uint16_t *tiles, int count)
{
    const int cols = cols_, rows = rows_;
    reset(cols, rows, 0);
    cacti_ = 0;
    for (int i = 0; i < count; i++) {
        if (tiles[i] < cols_ * rows_ && !(tiles_[tiles[i]] & kCactus)) {
            tiles_[tiles[i]] |= kCactus;
            cacti_++;
        }
    }
    count_neighbours();
    state_ = State::Playing;
}

void CactusField::count_neighbours()
{
    for (int row = 0; row < rows_; row++) {
        for (int col = 0; col < cols_; col++) {
            int n = 0;
            for (int r = row - 1; r <= row + 1; r++) {
                for (int c = col - 1; c <= col + 1; c++) {
                    n += (c != col || r != row) && has_cactus(c, r);
                }
            }
            at(col, row) = static_cast<uint8_t>((at(col, row) & 0x0F) | (n << kCountShift));
        }
    }
}

void CactusField::open_from(int col, int row)
{
    // Each tile goes on the list at most once: it is opened as it is added.
    int count = 0;
    at(col, row) |= kOpen;
    opened_++;
    pending_[count++] = static_cast<uint16_t>(row * cols_ + col);
    while (count > 0) {
        const int i = pending_[--count];
        if ((tiles_[i] >> kCountShift) != 0) {
            continue; // a number: the opening stops here
        }
        const int c0 = i % cols_, r0 = i / cols_;
        for (int r = r0 - 1; r <= r0 + 1; r++) {
            for (int c = c0 - 1; c <= c0 + 1; c++) {
                if (inside(c, r) && !(at(c, r) & (kOpen | kMarker | kCactus))) {
                    at(c, r) |= kOpen;
                    opened_++;
                    pending_[count++] = static_cast<uint16_t>(r * cols_ + c);
                }
            }
        }
    }
}

void CactusField::check_won()
{
    if (state_ != State::Playing || opened_ != cols_ * rows_ - cacti_) {
        return;
    }
    state_ = State::Won;
    // Every cactus gets its marker.
    for (int i = 0; i < cols_ * rows_; i++) {
        if (tiles_[i] & kCactus) {
            tiles_[i] |= kMarker;
        }
    }
    markers_ = cacti_;
}

bool CactusField::open(int col, int row, uint32_t seed)
{
    if (!inside(col, row) || over() || (at(col, row) & (kOpen | kMarker))) {
        return false;
    }
    if (state_ == State::Ready) {
        plant_random(col, row, seed);
    }
    if (at(col, row) & kCactus) {
        hit_ = row * cols_ + col;
        state_ = State::Lost;
        return true;
    }
    open_from(col, row);
    check_won();
    return true;
}

bool CactusField::toggle_marker(int col, int row)
{
    if (!inside(col, row) || over() || (at(col, row) & kOpen)) {
        return false;
    }
    at(col, row) ^= kMarker;
    markers_ += (at(col, row) & kMarker) ? 1 : -1;
    return true;
}

bool CactusField::open_around(int col, int row)
{
    if (!inside(col, row) || state_ != State::Playing || !(at(col, row) & kOpen)) {
        return false;
    }
    const int number = at(col, row) >> kCountShift;
    int marked = 0;
    for (int r = row - 1; r <= row + 1; r++) {
        for (int c = col - 1; c <= col + 1; c++) {
            marked += inside(c, r) && (at(c, r) & kMarker);
        }
    }
    if (number == 0 || marked != number) {
        return false;
    }
    bool changed = false;
    // A cactus among them ends the game, but the safe ones still open.
    int hit = -1;
    for (int r = row - 1; r <= row + 1; r++) {
        for (int c = col - 1; c <= col + 1; c++) {
            if (!inside(c, r) || (at(c, r) & (kOpen | kMarker))) {
                continue;
            }
            changed = true;
            if (at(c, r) & kCactus) {
                hit = hit < 0 ? r * cols_ + c : hit;
            } else {
                open_from(c, r);
            }
        }
    }
    if (hit >= 0) {
        hit_ = hit;
        state_ = State::Lost;
    } else {
        check_won();
    }
    return changed;
}

CactusField::Tile CactusField::tile(int col, int row) const
{
    if (!inside(col, row)) {
        return Tile::Covered;
    }
    const uint8_t t = at(col, row);
    if (t & kOpen) {
        return static_cast<Tile>(t >> kCountShift);
    }
    if (state_ == State::Lost) {
        if (row * cols_ + col == hit_) {
            return Tile::CactusHit;
        }
        if (t & kMarker) {
            return (t & kCactus) ? Tile::Marked : Tile::WrongMarker;
        }
        return (t & kCactus) ? Tile::Cactus : Tile::Covered;
    }
    return (t & kMarker) ? Tile::Marked : Tile::Covered;
}

} // namespace games
