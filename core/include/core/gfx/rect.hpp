// Rectangles and rectangle lists (regions), used for clipping and for
// working out which parts of the screen need painting.
#pragma once

namespace gfx {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;

    constexpr Rect() = default;
    constexpr Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}

    bool empty() const { return w <= 0 || h <= 0; }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool contains(int px, int py) const
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    Rect offset(int dx, int dy) const { return {x + dx, y + dy, w, h}; }
    Rect inset(int d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect intersect(const Rect &o) const;
    bool overlaps(const Rect &o) const { return !intersect(o).empty(); }
    bool operator==(const Rect &o) const
    {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
    bool operator!=(const Rect &o) const { return !(*this == o); }
};

// The smallest rectangle holding both (an empty one is ignored).
Rect bounding(const Rect &a, const Rect &b);

// A set of rectangles that never overlap each other.
class RectList {
public:
    // ponytail: fixed capacity, enough for a few dozen overlapping windows.
    // When an operation would need more, overflowed() turns true and the
    // caller falls back to something coarser but correct.
    static constexpr int kCapacity = 128;

    void clear()
    {
        count_ = 0;
        overflow_ = false;
    }
    void set(const Rect &r)
    {
        clear();
        add(r);
    }
    // Adds the parts of `r` not already in the list.
    void add(const Rect &r);
    void subtract(const Rect &r);
    void intersect(const Rect &r);

    int size() const { return count_; }
    bool empty() const { return count_ == 0; }
    const Rect &operator[](int i) const { return rects_[i]; }
    bool overflowed() const { return overflow_; }
    long area() const;
    Rect bounds() const;

private:
    bool push(const Rect &r);

    Rect rects_[kCapacity];
    int count_ = 0;
    bool overflow_ = false;
};

} // namespace gfx
