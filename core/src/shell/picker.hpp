// A grid of cells to pick one from, for dialogs (Display's colours and
// patterns, the colour dialog's sheet).
#pragma once

#include "core/gfx/canvas.hpp"
#include "core/wm/widgets.hpp"

namespace shell {

// A grid of cells to pick one from: the colours, or the patterns. The cell
// chosen has a black and white frame; arrows move it when it has the focus.
class Picker : public wm::Widget {
public:
    using PaintFn = void (*)(gfx::Canvas &c, const gfx::Rect &cell, int index, void *context);
    using ChangeFn = void (*)(void *context);

    // `margin`: from the edge of a cell to its sunken frame.
    Picker(const gfx::Rect &r, int columns, int count, int cell_w, int cell_h, PaintFn paint_cell, ChangeFn changed,
           void *context, int margin = 3)
        : Widget(r), columns_(columns), count_(count), cell_w_(cell_w), cell_h_(cell_h), margin_(margin),
          paint_cell_(paint_cell), changed_(changed), context_(context)
    {
    }
    // No cell chosen (the colour is not one of these); nobody is told.
    void clear()
    {
        if (selected_ != -1) {
            selected_ = -1;
            invalidate();
        }
    }
    void select_quietly(int index)
    {
        if (index != selected_) {
            selected_ = index;
            invalidate();
        }
    }

    int selected() const { return selected_; }
    void select(int index)
    {
        if (index < 0 || index >= count_ || index == selected_) {
            return;
        }
        selected_ = index;
        invalidate();
        if (changed_ != nullptr) {
            changed_(context_);
        }
    }
    gfx::Rect cell(int index) const
    {
        return {(index % columns_) * cell_w_, (index / columns_) * cell_h_, cell_w_, cell_h_};
    }

    void paint(gfx::Canvas &c) override
    {
        c.fill({0, 0, rect.w, rect.h}, gfx::kGrey);
        for (int i = 0; i < count_; i++) {
            const gfx::Rect r = cell(i);
            if (i == selected_) {
                c.edge(r, gfx::kBlack, gfx::kBlack);
                c.edge(r.inset(1), gfx::kWhite, gfx::kWhite);
            }
            const gfx::Rect inner = r.inset(margin_);
            c.edge(inner, gfx::kDarkGrey, gfx::kWhite);
            paint_cell_(c, inner.inset(1), i, context_);
            if (i == selected_ && margin_ < 3) {
                // A tight grid: the mark goes over the cell's own edge.
                c.edge(r, gfx::kBlack, gfx::kBlack);
                c.edge(r.inset(1), gfx::kWhite, gfx::kWhite);
                c.edge(r.inset(2), gfx::kBlack, gfx::kBlack);
            }
        }
        if (has_focus() && selected_ >= 0) {
            c.focus_rect(cell(selected_).inset(margin_ > 2 ? 2 : 0));
        }
    }
    bool wants_mouse() const override { return true; }
    void mouse(const wm::MouseMsg &m) override
    {
        if (m.kind != wm::MouseKind::Down || m.x < 0 || m.y < 0 || m.x >= columns_ * cell_w_) {
            return;
        }
        select(m.y / cell_h_ * columns_ + m.x / cell_w_);
    }
    bool focusable() const override { return true; }
    bool key(const wm::KeyMsg &k) override
    {
        if (!k.pressed) {
            return false;
        }
        switch (k.usage) {
        case wm::key::Left:
            select(selected_ - 1);
            return true;
        case wm::key::Right:
            select(selected_ + 1);
            return true;
        case wm::key::Up:
            select(selected_ - columns_);
            return true;
        case wm::key::Down:
            select(selected_ + columns_);
            return true;
        default:
            return false;
        }
    }

private:
    int columns_, count_, cell_w_, cell_h_, margin_;
    PaintFn paint_cell_;
    ChangeFn changed_;
    void *context_;
    int selected_ = 0;
};

} // namespace shell
