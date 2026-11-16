#include "core/games/snake.hpp"

namespace games {

namespace {

constexpr int kDx[4] = {0, 1, 0, -1};
constexpr int kDy[4] = {-1, 0, 1, 0};

// The link bit in cell `from` for its neighbour `to`.
uint8_t bit_towards(int from, int to)
{
    const int d = to - from;
    if (d == -Snake::kCols) {
        return 1;
    }
    if (d == 1) {
        return 2;
    }
    if (d == Snake::kCols) {
        return 4;
    }
    return 8;
}

} // namespace

uint32_t Snake::random()
{
    // xorshift32: small, and the same on every machine.
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
}

void Snake::link(int from, int to)
{
    grid_[from] |= bit_towards(from, to);
    grid_[to] |= bit_towards(to, from);
}

void Snake::unlink(int from, int to)
{
    grid_[from] &= static_cast<uint8_t>(~bit_towards(from, to));
    grid_[to] &= static_cast<uint8_t>(~bit_towards(to, from));
}

void Snake::changed(int cell)
{
    if (changed_count_ >= 0 && changed_count_ < kMaxChanged) {
        changed_[changed_count_++] = static_cast<int16_t>(cell);
    }
}

void Snake::reset(uint32_t seed)
{
    seed_ = seed != 0 ? seed : 1;
    state_ = State::Ready;
    heading_ = Dir::Right;
    turn_count_ = 0;
    for (int i = 0; i < kCells; i++) {
        grid_[i] = 0;
    }
    const int row = kRows / 2, col = kCols / 2 - kStartLength;
    length_ = kStartLength;
    for (int i = 0; i < kStartLength; i++) {
        body_[i] = static_cast<int16_t>(row * kCols + col + i);
        grid_[body_[i]] = kOccupied;
        if (i > 0) {
            link(body_[i - 1], body_[i]);
        }
    }
    head_ = kStartLength - 1;
    new_berry();
    changed_count_ = -1;
}

void Snake::new_berry()
{
    const int free_cells = kCells - length_;
    if (free_cells <= 0) {
        berry_ = -1;
        return;
    }
    int n = static_cast<int>(random() % static_cast<uint32_t>(free_cells));
    for (int i = 0; i < kCells; i++) {
        if (grid_[i] == 0 && n-- == 0) {
            berry_ = static_cast<int16_t>(i);
            return;
        }
    }
}

bool Snake::steer(Dir d)
{
    if (state_ == State::Over || state_ == State::Won) {
        return false;
    }
    // Compared with the last turn waiting, so two quick turns (up, then
    // left) both count and neither can fold the snake back on itself.
    const Dir last = turn_count_ > 0 ? turns_[turn_count_ - 1] : heading_;
    const bool opposite = ((static_cast<int>(d) + 2) & 3) == static_cast<int>(last);
    if (d != last && !opposite && turn_count_ < 2) {
        turns_[turn_count_++] = d;
    }
    if (state_ != State::Playing) {
        state_ = State::Playing;
        return true;
    }
    return false;
}

bool Snake::pause()
{
    if (state_ != State::Playing) {
        return false;
    }
    state_ = State::Paused;
    return true;
}

bool Snake::toggle_pause()
{
    if (state_ == State::Paused) {
        state_ = State::Playing;
        return true;
    }
    return pause();
}

bool Snake::step()
{
    changed_count_ = 0;
    if (state_ != State::Playing) {
        return false;
    }
    if (turn_count_ > 0) {
        heading_ = turns_[0];
        turns_[0] = turns_[1];
        turn_count_--;
    }
    const int old_head = body_[head_];
    const int col = old_head % kCols + kDx[static_cast<int>(heading_)];
    const int row = old_head / kCols + kDy[static_cast<int>(heading_)];
    if (col < 0 || col >= kCols || row < 0 || row >= kRows) {
        state_ = State::Over;
        return false;
    }
    const int target = row * kCols + col;
    const bool eats = target == berry_;
    const int tail_index = (head_ - length_ + 1 + kCells) % kCells;
    const int tail = body_[tail_index];
    // The head may follow the tail into the cell it is leaving.
    if (grid_[target] != 0 && target != tail) {
        state_ = State::Over;
        return false;
    }
    if (!eats) {
        const int next = body_[(tail_index + 1) % kCells];
        unlink(tail, next);
        grid_[tail] = 0;
        length_--;
        changed(tail);
        if (next != old_head) {
            changed(next); // the new tail end
        }
    }
    head_ = static_cast<int16_t>((head_ + 1) % kCells);
    body_[head_] = static_cast<int16_t>(target);
    grid_[target] = kOccupied;
    link(old_head, target);
    length_++;
    changed(old_head);
    changed(target);
    if (eats) {
        new_berry();
        if (berry_ < 0) {
            state_ = State::Won;
        } else {
            changed(berry_);
        }
    }
    return eats;
}

Snake::Cell Snake::at(int col, int row) const
{
    const int cell = row * kCols + col;
    if (grid_[cell] != 0) {
        return {cell == body_[head_] ? Kind::Head : Kind::Body, static_cast<uint8_t>(grid_[cell] & 0x0F)};
    }
    return {cell == berry_ ? Kind::Berry : Kind::Empty, 0};
}

} // namespace games
