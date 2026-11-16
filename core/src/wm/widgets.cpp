#include "core/wm/widgets.hpp"

namespace wm {

void Label::paint(gfx::Canvas &c)
{
    c.text(0, (rect.h - gfx::Canvas::text_height()) / 2, text_, gfx::kBlack, bold_);
}

void Button::paint(gfx::Canvas &c)
{
    const Rect r = {0, 0, rect.w, rect.h};
    c.button(r, pressed_ ? gfx::ButtonLook::Pressed : gfx::ButtonLook::Raised);
    const int d = pressed_ ? 1 : 0;
    gfx::Canvas inner = c.sub(r.inset(2));
    inner.text((r.w - 4 - gfx::Canvas::text_width(text_)) / 2 + d,
               (r.h - 4 - gfx::Canvas::text_height()) / 2 + d, text_, gfx::kBlack);
}

void Button::mouse(const MouseMsg &m)
{
    const bool inside = m.x >= 0 && m.y >= 0 && m.x < rect.w && m.y < rect.h;
    const bool was = pressed_;
    switch (m.kind) {
    case MouseKind::Down:
        pressed_ = true;
        break;
    case MouseKind::Move:
        pressed_ = inside;
        break;
    case MouseKind::Up:
        pressed_ = false;
        if (was != pressed_) {
            invalidate();
        }
        if (inside) {
            click();
        }
        return;
    }
    if (was != pressed_) {
        invalidate();
    }
}

void Button::click()
{
    if (on_click_ != nullptr) {
        on_click_(*this, context_);
    }
}

void TextArea::paint(gfx::Canvas &c)
{
    const Rect r = {0, 0, rect.w, rect.h};
    c.sunken(r);
    c.fill(r.inset(2), gfx::kWhite);
    gfx::Canvas inner = c.sub(r.inset(2));
    // One line at a time, copied out so it can end in a zero.
    char line[128];
    const char *p = text_;
    int y = 3;
    while (*p != 0 && y < rect.h) {
        int n = 0;
        while (p[n] != 0 && p[n] != '\n' && n < static_cast<int>(sizeof(line)) - 1) {
            line[n] = p[n];
            n++;
        }
        line[n] = 0;
        inner.text(4, y, line, gfx::kBlack);
        p += n;
        while (*p != 0 && *p != '\n') {
            p++; // the rest of an over-long line
        }
        if (*p == '\n') {
            p++;
        }
        y += 13;
    }
}

} // namespace wm
