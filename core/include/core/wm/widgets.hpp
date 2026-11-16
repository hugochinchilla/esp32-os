// The first widgets: label, push button, read-only text area.
#pragma once

#include "core/wm/window.hpp"

namespace wm {

class Label : public Widget {
public:
    Label(const Rect &r, const char *text, bool bold = false)
        : Widget(r), text_(text), bold_(bold)
    {
    }
    void set_text(const char *text)
    {
        text_ = text;
        invalidate();
    }
    void paint(gfx::Canvas &c) override;

private:
    const char *text_;
    bool bold_;
};

class Button : public Widget {
public:
    using Callback = void (*)(Button &button, void *context);

    Button(const Rect &r, const char *text, Callback on_click = nullptr, void *context = nullptr)
        : Widget(r), text_(text), on_click_(on_click), context_(context)
    {
    }
    void paint(gfx::Canvas &c) override;
    bool wants_mouse() const override { return true; }
    void mouse(const MouseMsg &m) override;
    bool pressed() const { return pressed_; }
    // Acts as if clicked (keyboard shortcuts).
    void click();

private:
    const char *text_;
    Callback on_click_;
    void *context_;
    bool pressed_ = false;
};

// Lines of text ('\n' between them) in a sunken white box. Read-only, no
// scrolling yet: lines that do not fit are cut off.
class TextArea : public Widget {
public:
    TextArea(const Rect &r, const char *text) : Widget(r), text_(text) {}
    void set_text(const char *text)
    {
        text_ = text;
        invalidate();
    }
    void paint(gfx::Canvas &c) override;

private:
    const char *text_;
};

} // namespace wm
