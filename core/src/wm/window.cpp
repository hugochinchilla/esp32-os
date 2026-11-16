#include "core/wm/window.hpp"

#include "core/wm/wm.hpp"

namespace wm {

void Widget::invalidate()
{
    if (window_ != nullptr) {
        window_->invalidate_client(rect);
    }
}

Rect Window::client() const
{
    return {frame_.x + kBorder, frame_.y + kClientTop, frame_.w - 2 * kBorder,
            frame_.h - kClientTop - kBorder};
}

bool Window::focused() const
{
    return wm_ != nullptr && wm_->active() == this;
}

void Window::add(Widget &w)
{
    if (widget_count_ < kMaxWidgets) {
        widgets_[widget_count_++] = &w;
        w.window_ = this;
        w.invalidate();
    }
}

void Window::invalidate()
{
    if (wm_ != nullptr) {
        wm_->invalidate(frame_);
    }
}

void Window::invalidate_client(const Rect &r)
{
    if (wm_ != nullptr) {
        const Rect c = client();
        wm_->invalidate(r.offset(c.x, c.y).intersect(c));
    }
}

void Window::close()
{
    if (wm_ != nullptr) {
        wm_->close(*this);
    }
}

void Window::paint(gfx::Canvas &c)
{
    const Rect c_rect = client();
    c.fill({0, 0, c_rect.w, c_rect.h}, gfx::kGrey);
    for (int i = 0; i < widget_count_; i++) {
        gfx::Canvas wc = c.sub(widgets_[i]->rect);
        widgets_[i]->paint(wc);
    }
}

void Window::mouse(const MouseMsg &m)
{
    if (m.kind == MouseKind::Down) {
        captured_ = nullptr;
        for (int i = widget_count_ - 1; i >= 0; i--) {
            Widget *w = widgets_[i];
            if (w->wants_mouse() && w->rect.contains(m.x, m.y)) {
                captured_ = w;
                break;
            }
        }
    }
    if (captured_ != nullptr) {
        Widget *w = captured_;
        if (m.kind == MouseKind::Up) {
            captured_ = nullptr;
        }
        w->mouse({m.kind, m.x - w->rect.x, m.y - w->rect.y});
    }
}

void Window::key(uint16_t, bool) {}

} // namespace wm
