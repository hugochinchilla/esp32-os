#include "core/gfx/rect.hpp"

namespace gfx {

Rect Rect::intersect(const Rect &o) const
{
    int x0 = x > o.x ? x : o.x;
    int y0 = y > o.y ? y : o.y;
    int x1 = right() < o.right() ? right() : o.right();
    int y1 = bottom() < o.bottom() ? bottom() : o.bottom();
    if (x1 <= x0 || y1 <= y0) {
        return {};
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

Rect bounding(const Rect &a, const Rect &b)
{
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    int x0 = a.x < b.x ? a.x : b.x;
    int y0 = a.y < b.y ? a.y : b.y;
    int x1 = a.right() > b.right() ? a.right() : b.right();
    int y1 = a.bottom() > b.bottom() ? a.bottom() : b.bottom();
    return {x0, y0, x1 - x0, y1 - y0};
}

bool RectList::push(const Rect &r)
{
    if (r.empty()) {
        return true;
    }
    if (count_ == kCapacity) {
        overflow_ = true;
        return false;
    }
    rects_[count_++] = r;
    return true;
}

void RectList::subtract(const Rect &cut)
{
    // Each rectangle that meets `cut` is replaced by up to four pieces: the
    // bands above and below it, and the parts left and right of it. New
    // pieces go at the end and never meet `cut`, so they are not looked at.
    const int original = count_;
    for (int i = 0; i < original; i++) {
        const Rect r = rects_[i];
        const Rect c = r.intersect(cut);
        if (c.empty()) {
            continue;
        }
        rects_[i] = Rect(); // removed; compacted below
        push({r.x, r.y, r.w, c.y - r.y});
        push({r.x, c.bottom(), r.w, r.bottom() - c.bottom()});
        push({r.x, c.y, c.x - r.x, c.h});
        push({c.right(), c.y, r.right() - c.right(), c.h});
    }
    int out = 0;
    for (int i = 0; i < count_; i++) {
        if (!rects_[i].empty()) {
            rects_[out++] = rects_[i];
        }
    }
    count_ = out;
}

void RectList::intersect(const Rect &r)
{
    int out = 0;
    for (int i = 0; i < count_; i++) {
        Rect c = rects_[i].intersect(r);
        if (!c.empty()) {
            rects_[out++] = c;
        }
    }
    count_ = out;
}

void RectList::add(const Rect &r)
{
    if (r.empty()) {
        return;
    }
    // Only the parts of `r` not yet covered are added, so the list never
    // overlaps itself. Worked out in a small local list.
    Rect pieces[16];
    int n = 1;
    pieces[0] = r;
    for (int i = 0; i < count_ && n > 0; i++) {
        const Rect &have = rects_[i];
        int m = n;
        for (int j = 0; j < m; j++) {
            const Rect p = pieces[j];
            const Rect c = p.intersect(have);
            if (c.empty()) {
                continue;
            }
            pieces[j] = Rect();
            const Rect parts[4] = {
                {p.x, p.y, p.w, c.y - p.y},
                {p.x, c.bottom(), p.w, p.bottom() - c.bottom()},
                {p.x, c.y, c.x - p.x, c.h},
                {c.right(), c.y, p.right() - c.right(), c.h},
            };
            for (const Rect &part : parts) {
                if (part.empty()) {
                    continue;
                }
                if (n == 16) {
                    overflow_ = true;
                    return;
                }
                pieces[n++] = part;
            }
        }
        int out = 0;
        for (int j = 0; j < n; j++) {
            if (!pieces[j].empty()) {
                pieces[out++] = pieces[j];
            }
        }
        n = out;
    }
    for (int j = 0; j < n; j++) {
        push(pieces[j]);
    }
}

long RectList::area() const
{
    long total = 0;
    for (int i = 0; i < count_; i++) {
        total += static_cast<long>(rects_[i].w) * rects_[i].h;
    }
    return total;
}

Rect RectList::bounds() const
{
    Rect b;
    for (int i = 0; i < count_; i++) {
        b = bounding(b, rects_[i]);
    }
    return b;
}

} // namespace gfx
