#include "core/wm/window.hpp"

#include "core/wm/wm.hpp"

namespace wm {

char key::letter(uint16_t usage)
{
    if (usage >= A && usage <= Z) {
        return static_cast<char>('a' + (usage - A));
    }
    if (usage >= Digit1 && usage < Digit0) {
        return static_cast<char>('1' + (usage - Digit1));
    }
    if (usage == Digit0) {
        return '0';
    }
    return 0;
}

char key::us_char(uint16_t usage, bool shift)
{
    if (usage >= A && usage <= Z) {
        return static_cast<char>((shift ? 'A' : 'a') + (usage - A));
    }
    // 0x1E..0x38: digits, Enter..Space (skipped), then punctuation.
    static const char plain[] = "1234567890\0\0\0\0 -=[]\\\0;'`,./";
    static const char shifted[] = "!@#$%^&*()\0\0\0\0 _+{}|\0:\"~<>?";
    if (usage >= Digit1 && usage <= 0x38) {
        return (shift ? shifted : plain)[usage - Digit1];
    }
    return 0;
}

int strip_access(const char *label, char *out, int capacity)
{
    int access = -1, n = 0;
    for (const char *p = label; *p != 0 && n < capacity - 1; p++) {
        if (*p == '&' && p[1] != 0) {
            p++;
            if (*p != '&') {
                access = n;
            }
        }
        out[n++] = *p;
    }
    out[n] = 0;
    return access;
}

char access_letter(const char *label)
{
    for (const char *p = label; *p != 0; p++) {
        if (*p == '&' && p[1] != 0 && p[1] != '&') {
            char c = p[1];
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
    }
    return 0;
}

bool Widget::has_focus() const
{
    return window_ != nullptr && window_->focus() == this && window_->focused();
}

Wm *Widget::manager() const
{
    return window_ != nullptr ? window_->manager() : nullptr;
}

bool Widget::caret_shown() const
{
    Wm *wm = manager();
    return wm == nullptr || wm->caret_visible();
}

void Widget::invalidate_part(const Rect &r)
{
    if (window_ != nullptr) {
        window_->invalidate_client(r.intersect({0, 0, rect.w, rect.h}).offset(rect.x, rect.y));
    }
}

void Widget::invalidate()
{
    if (window_ != nullptr) {
        window_->invalidate_client(rect);
    }
}

Rect Window::client() const
{
    const int top = kClientTop + (menu_bar_ ? kMenuBarHeight : 0);
    return {frame_.x + kBorder, frame_.y + top, frame_.w - 2 * kBorder, frame_.h - top - kBorder};
}

Rect Window::menu_bar_rect() const
{
    if (menu_bar_ == nullptr) {
        return {};
    }
    return {frame_.x + kBorder, frame_.y + kClientTop, frame_.w - 2 * kBorder, kMenuBarHeight};
}

bool Window::focused() const
{
    return wm_ != nullptr && wm_->active() == this;
}

void Window::set_frame(const Rect &r)
{
    if (wm_ != nullptr) {
        wm_->invalidate(frame_);
        wm_->invalidate(r);
    }
    frame_ = r;
}

void Window::set_title(const char *title)
{
    title_ = title;
    if (wm_ != nullptr) {
        wm_->invalidate(frame_);
        wm_->invalidate(wm_->taskbar());
    }
}

void Window::add(Widget &w)
{
    if (widget_count_ < kMaxWidgets) {
        widgets_[widget_count_++] = &w;
        w.window_ = this;
        if (focus_ == nullptr && w.focusable()) {
            focus_ = &w;
        }
        w.invalidate();
    }
}

void Window::set_focus(Widget *w)
{
    if (w == focus_) {
        return;
    }
    if (focus_ != nullptr) {
        focus_->invalidate();
    }
    focus_ = w;
    if (focus_ != nullptr) {
        focus_->invalidate();
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

void Window::set_locked(bool on)
{
    if (locked_ != on) {
        locked_ = on;
        if (wm_ != nullptr) {
            wm_->lock_changed(*this);
        }
    }
}

void Window::set_hourglass(bool on)
{
    if (hourglass_ != on) {
        hourglass_ = on;
        if (wm_ != nullptr) {
            wm_->hourglass_changed();
        }
    }
}

void Window::close()
{
    if (wm_ != nullptr) {
        wm_->close(*this);
    }
}

void Window::remove_last()
{
    if (widget_count_ == 0) {
        return;
    }
    Widget *w = widgets_[--widget_count_];
    widgets_[widget_count_] = nullptr;
    focus_ = focus_ == w ? nullptr : focus_;
    default_ = default_ == w ? nullptr : default_;
    captured_ = captured_ == w ? nullptr : captured_;
    invalidate();
}

bool Window::layered() const
{
    for (int i = 0; i < widget_count_; i++) {
        if (!widgets_[i]->opaque()) {
            return true;
        }
    }
    return false;
}

void Window::paint(gfx::Canvas &c)
{
    // The face colour only where no widget paints, so nothing is drawn
    // twice (with one screen buffer, a fill followed by the widget on top
    // shows as a flash).
    const Rect c_rect = client();
    static gfx::RectList background; // ponytail: one UI task, so one list will do
    background.set({0, 0, c_rect.w, c_rect.h});
    for (int i = 0; i < widget_count_; i++) {
        if (widgets_[i]->opaque()) {
            background.subtract(widgets_[i]->rect);
        }
    }
    if (background.overflowed()) {
        background.set({0, 0, c_rect.w, c_rect.h}); // too many pieces: the plain way
    }
    for (int i = 0; i < background.size(); i++) {
        c.fill(background[i], gfx::face());
    }
    for (int i = 0; i < widget_count_; i++) {
        Widget &w = *widgets_[i];
        if (w.opaque() && w.layered()) {
            c.layered(w.rect, [&](gfx::Canvas &l) {
                gfx::Canvas wc = l.sub(w.rect);
                w.paint(wc);
            });
        } else {
            gfx::Canvas wc = c.sub(w.rect);
            w.paint(wc);
        }
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
                if (w->focusable()) {
                    set_focus(w);
                }
                break;
            }
        }
    }
    if (captured_ != nullptr) {
        Widget *w = captured_;
        if (m.kind == MouseKind::Up) {
            captured_ = nullptr;
        }
        w->mouse({m.kind, m.x - w->rect.x, m.y - w->rect.y, m.double_click});
    }
}

bool Window::key(const KeyMsg &k)
{
    if (focus_ != nullptr && focus_->key(k)) {
        return true;
    }
    if (!k.pressed) {
        return false;
    }
    if (k.usage == key::Tab && widget_count_ > 0) {
        // Next (or with Shift, previous) widget that takes the focus.
        int at = 0;
        for (int i = 0; i < widget_count_; i++) {
            if (widgets_[i] == focus_) {
                at = i;
            }
        }
        const int step = (k.mods & kShift) ? widget_count_ - 1 : 1;
        for (int n = 1; n <= widget_count_; n++) {
            Widget *w = widgets_[(at + n * step) % widget_count_];
            if (w->focusable()) {
                set_focus(w);
                return true;
            }
        }
        return false;
    }
    if ((k.mods & kAlt) && key::letter(k.usage) != 0) {
        // Alt+letter: the widget with that underlined letter. A label
        // passes the focus to the widget after it, as in 98 dialogs.
        const char letter = key::letter(k.usage);
        for (int i = 0; i < widget_count_; i++) {
            Widget *w = widgets_[i];
            if (w->access() != letter) {
                continue;
            }
            if (w->focusable()) {
                set_focus(w);
                w->press();
                return true;
            }
            for (int j = i + 1; j < widget_count_; j++) {
                if (widgets_[j]->focusable()) {
                    set_focus(widgets_[j]);
                    return true;
                }
            }
        }
        return false;
    }
    if (k.usage == key::Enter && default_ != nullptr) {
        default_->press();
        return true;
    }
    return false;
}

void Window::text(const char *utf8)
{
    if (focus_ != nullptr) {
        focus_->text(utf8);
    }
}

} // namespace wm
