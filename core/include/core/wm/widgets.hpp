// Widgets: label, push button, check box, radio button, single-line text
// field, scroll bar, list box, group frame and read-only text area.
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
    char access() const override { return access_letter(text_); }

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
    bool focusable() const override { return true; }
    bool key(const KeyMsg &k) override;
    void press() override { click(); }
    char access() const override { return access_letter(text_); }
    bool pressed() const { return pressed_; }
    void set_label(const char *text)
    {
        text_ = text;
        invalidate();
    }
    // The window's default button gets the extra dark frame.
    void set_default(bool on)
    {
        default_ = on;
        invalidate();
    }
    void click();

private:
    const char *text_;
    Callback on_click_;
    void *context_;
    bool pressed_ = false;
    bool default_ = false;
};

class CheckBox : public Widget {
public:
    using Callback = void (*)(CheckBox &box, void *context);

    CheckBox(const Rect &r, const char *text, bool checked = false, Callback on_change = nullptr,
             void *context = nullptr)
        : Widget(r), text_(text), checked_(checked), on_change_(on_change), context_(context)
    {
    }
    void paint(gfx::Canvas &c) override;
    bool wants_mouse() const override { return true; }
    void mouse(const MouseMsg &m) override;
    bool focusable() const override { return true; }
    bool key(const KeyMsg &k) override;
    void press() override { set_checked(!checked_); }
    char access() const override { return access_letter(text_); }
    bool checked() const { return checked_; }
    void set_checked(bool on);

private:
    const char *text_;
    bool checked_;
    Callback on_change_;
    void *context_;
    bool held_ = false;
};

// Radio buttons share an int: each button sets it to its own value.
class RadioButton : public Widget {
public:
    RadioButton(const Rect &r, const char *text, int *group, int value)
        : Widget(r), text_(text), group_(group), value_(value)
    {
    }
    void paint(gfx::Canvas &c) override;
    bool wants_mouse() const override { return true; }
    void mouse(const MouseMsg &m) override;
    bool focusable() const override { return true; }
    bool key(const KeyMsg &k) override;
    void press() override { select(); }
    char access() const override { return access_letter(text_); }
    bool selected() const { return *group_ == value_; }
    void select();

private:
    const char *text_;
    int *group_;
    int value_;
    bool held_ = false;
};

// One line of editable text. Latin-1 inside, UTF-8 in and out.
class TextField : public Widget {
public:
    static constexpr int kCapacity = 80;

    TextField(const Rect &r, const char *initial = "");
    void paint(gfx::Canvas &c) override;
    bool wants_mouse() const override { return true; }
    void mouse(const MouseMsg &m) override;
    bool focusable() const override { return true; }
    bool key(const KeyMsg &k) override;
    void text(const char *utf8) override;

    // The text as UTF-8 (at most 2 bytes per character plus the zero).
    void get(char *out, int capacity) const;
    void set(const char *utf8);
    int caret() const { return caret_; }
    int selection_start() const { return caret_ < anchor_ ? caret_ : anchor_; }
    int selection_end() const { return caret_ < anchor_ ? anchor_ : caret_; }
    int length() const { return length_; }

private:
    void insert(const uint8_t *chars, int count);
    void erase_selection();
    void move_caret(int to, bool extend);
    int index_at(int x) const;
    void keep_caret_visible();

    uint8_t chars_[kCapacity];
    int length_ = 0;
    int caret_ = 0;
    int anchor_ = 0; // other end of the selection
    int scroll_ = 0; // first character shown
    bool dragging_ = false;
};

class ScrollBar : public Widget {
public:
    using Callback = void (*)(ScrollBar &bar, void *context);
    static constexpr int kThickness = 16;

    ScrollBar(const Rect &r, bool vertical, Callback on_change = nullptr, void *context = nullptr)
        : Widget(r), vertical_(vertical), on_change_(on_change), context_(context)
    {
    }
    void paint(gfx::Canvas &c) override;
    bool wants_mouse() const override { return true; }
    void mouse(const MouseMsg &m) override;
    // Values run from 0 to `maximum`; `page` is how much one view shows.
    void set_range(int maximum, int page);
    void set_value(int value);
    int value() const { return value_; }
    int maximum() const { return maximum_; }
    // Where the thumb is, in the bar's own coordinates (for tests).
    Rect thumb() const;
    // A bar inside another widget (a list box) uses that widget's window.
    void set_parent(Widget *parent) { parent_ = parent; }

private:
    int length() const { return vertical_ ? rect.h : rect.w; }
    void changed();
    void step();
    static void repeat(void *self);
    Wm *host() const { return parent_ != nullptr ? parent_->manager() : manager(); }

    bool vertical_;
    Callback on_change_;
    void *context_;
    int maximum_ = 100, page_ = 10, value_ = 0;
    int held_ = 0;        // 0 none, -1 / 1 arrows, -2 / 2 track, 3 thumb
    int grab_ = 0;        // where the thumb was taken, along the bar
    int grab_value_ = 0;
    int last_along_ = 0;  // pointer position, for repeating track presses
    int timer_ = 0;
    Widget *parent_ = nullptr;
};

class ListBox : public Widget {
public:
    using Callback = void (*)(ListBox &list, void *context);
    static constexpr int kRowHeight = 14;

    ListBox(const Rect &r, const char *const *items, int count, Callback on_select = nullptr,
            void *context = nullptr);
    void paint(gfx::Canvas &c) override;
    bool wants_mouse() const override { return true; }
    void mouse(const MouseMsg &m) override;
    bool focusable() const override { return true; }
    bool key(const KeyMsg &k) override;
    int selected() const { return selected_; }
    void select(int index);
    int top() const { return bar_.value(); }
    int visible_rows() const { return (rect.h - 4) / kRowHeight; }

private:
    static void scrolled(ScrollBar &, void *context);
    Rect bar_rect() const;

    const char *const *items_;
    int count_;
    Callback on_select_;
    void *context_;
    int selected_ = -1;
    ScrollBar bar_;
    bool in_bar_ = false;
};

// An etched frame with a caption, around a group of widgets.
class GroupFrame : public Widget {
public:
    GroupFrame(const Rect &r, const char *caption) : Widget(r), caption_(caption) {}
    void paint(gfx::Canvas &c) override;

private:
    const char *caption_;
};

// Lines of text ('\n' between them) in a sunken white box. Read-only, no
// scrolling: lines that do not fit are cut off.
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
