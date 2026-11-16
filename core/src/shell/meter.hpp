// A bar that fills from the left in blocks, in a sunken box: progress, or
// how full something is. Shared by the shell's system programs.
#pragma once

#include "core/wm/window.hpp"

namespace shell {

class Meter : public wm::Widget {
public:
    static constexpr int kBlock = 6, kGap = 2;

    explicit Meter(const gfx::Rect &r, gfx::Colour colour = gfx::rgb(0, 0, 2))
        : Widget(r), colour_(colour)
    {
    }

    int blocks() const { return (rect.w - 4 + kGap) / (kBlock + kGap); }
    int lit() const { return lit_; }
    // How full, as value out of total (shown in whole blocks).
    void set(uint32_t value, uint32_t total)
    {
        const int n = blocks();
        int lit = total == 0 ? 0 : static_cast<int>((static_cast<uint64_t>(value) * n + total / 2) / total);
        lit = lit > n ? n : lit;
        if (lit == 0 && value != 0) {
            lit = 1; // anything at all shows
        }
        if (lit != lit_) {
            lit_ = lit;
            invalidate();
        }
    }

    void paint(gfx::Canvas &c) override
    {
        const gfx::Rect r = {0, 0, rect.w, rect.h};
        c.sunken(r);
        // Every pixel inside once: blocks, then the gaps between them.
        const int w = rect.w - 4, h = rect.h - 4;
        int x = 0;
        for (int i = 0; x < w; i++) {
            const int bw = x + kBlock > w ? w - x : kBlock;
            c.fill({2 + x, 2, bw, h}, i < lit_ ? colour_ : gfx::kWhite);
            x += bw;
            const int gw = x + kGap > w ? w - x : kGap;
            if (gw > 0) {
                c.fill({2 + x, 2, gw, h}, gfx::kWhite);
                x += gw;
            }
        }
    }

private:
    gfx::Colour colour_;
    int lit_ = 0;
};

} // namespace shell
