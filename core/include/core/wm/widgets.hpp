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
    bool focusable() const override { return enabled_; }
    bool key(const KeyMsg &k) override;
    void press() override { click(); }
    char access() const override { return enabled_ ? access_letter(text_) : 0; }
    bool pressed() const { return pressed_; }
    // A button that is not enabled is greyed out: its caption is etched, and
    // the mouse, the keyboard and its Alt+letter do nothing.
    void set_enabled(bool on);
    bool enabled() const { return enabled_; }
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
    bool enabled_ = true;
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
    using Callback = void (*)(RadioButton &button, void *context);

    // `on_change` is told when this one becomes the chosen one.
    RadioButton(const Rect &r, const char *text, int *group, int value, Callback on_change = nullptr,
                void *context = nullptr)
        : Widget(r), text_(text), group_(group), value_(value), on_change_(on_change), context_(context)
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
    Callback on_change_;
    void *context_;
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
    Rect caret_rect() const override;

    // The text as UTF-8 (at most 2 bytes per character plus the zero).
    void get(char *out, int capacity) const;
    void set(const char *utf8);
    void select_all()
    {
        anchor_ = 0;
        caret_ = length_;
        keep_caret_visible();
        invalidate();
    }
    // A password: every character is shown as '*'.
    void set_hidden(bool on)
    {
        hidden_ = on;
        invalidate();
    }
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
    bool hidden_ = false;
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
    // A vertical bar takes the notches down, a horizontal one those to the
    // right; false if there are none for it or it has nowhere to go.
    bool wheel(const WheelMsg &m) override;
    bool window_bar() const override { return parent_ == nullptr; }
    // How far one notch of the wheel goes: three of the arrows' steps where
    // a step is a line, an eighth of the view where it is a pixel.
    int wheel_step() const
    {
        const int step = page_ / 8 < 3 ? 3 : page_ / 8;
        return step > page_ ? page_ : step;
    }
    // Values run from 0 to `maximum`; `page` is how much one view shows.
    void set_range(int maximum, int page);
    void set_value(int value);
    // The same without calling back (when the owner moved itself).
    void set_value_quietly(int value)
    {
        value = value < 0 ? 0 : (value > maximum_ ? maximum_ : value);
        if (value != value_) {
            value_ = value;
            invalidate();
        }
    }
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
    using IconFn = const gfx::Bitmap *(*)(int index, void *context);
    static constexpr int kRowHeight = 14;
    static constexpr int kIconRowHeight = 18;

    ListBox(const Rect &r, const char *const *items, int count, Callback on_select = nullptr,
            void *context = nullptr);
    // New contents (the strings must stay valid); nothing selected.
    void set_items(const char *const *items, int count);
    // A 16x16 icon before each item.
    void set_icons(IconFn icon) { icon_ = icon; }
    // Double click or Enter on an item.
    void set_on_activate(Callback fn) { on_activate_ = fn; }
    int count() const { return count_; }
    void paint(gfx::Canvas &c) override;
    bool wants_mouse() const override { return true; }
    void mouse(const MouseMsg &m) override;
    bool wheel(const WheelMsg &m) override
    {
        bar_.wheel(m);
        return true; // a list that fits stays still, and so does the rest
    }
    bool focusable() const override { return true; }
    bool key(const KeyMsg &k) override;
    int selected() const { return selected_; }
    void select(int index);
    int top() const { return bar_.value(); }
    int row_height() const { return icon_ != nullptr ? kIconRowHeight : kRowHeight; }
    int visible_rows() const { return (rect.h - 4) / row_height(); }
    void layout(); // after the rect changed

private:
    static void scrolled(ScrollBar &, void *context);
    Rect bar_rect() const;

    const char *const *items_;
    int count_;
    Callback on_select_;
    void *context_;
    IconFn icon_ = nullptr;
    Callback on_activate_ = nullptr;
    int selected_ = -1;
    ScrollBar bar_;
    bool in_bar_ = false;
};

// An etched frame with a caption, around a group of widgets.
class GroupFrame : public Widget {
public:
    GroupFrame(const Rect &r, const char *caption) : Widget(r), caption_(caption) {}
    void paint(gfx::Canvas &c) override;
    bool opaque() const override { return false; } // only its lines: the window fills inside

private:
    const char *caption_;
};

// Tabs: a row of titles along the top and, under it, the frame of the page
// they choose. It draws the titles and the frame only. The widgets of a
// page are the window's own, added after this one and lying over it; its
// owner shows those of the chosen page and hides the others (an empty rect)
// when on_change tells of another one.
class Tabs : public Widget {
public:
    using Callback = void (*)(Tabs &tabs, void *context);
    static constexpr int kMax = 6;
    static constexpr int kStrip = 20; // the height of the row of titles

    Tabs(const Rect &r, const char *const *titles, int count, Callback on_change = nullptr, void *context = nullptr)
        : Widget(r), titles_(titles), count_(count < kMax ? count : kMax), on_change_(on_change), context_(context)
    {
    }
    void paint(gfx::Canvas &c) override;
    bool opaque() const override { return false; } // only its lines: the window fills the page
    bool wants_mouse() const override { return true; }
    void mouse(const MouseMsg &m) override;
    bool focusable() const override { return true; }
    bool key(const KeyMsg &k) override;
    bool turn_page(int step) override;
    int count() const { return count_; }
    int selected() const { return selected_; }
    void select(int index);
    // Where a title is, in the widget's coordinates, as it is when it is not
    // the chosen one (which is drawn two pixels larger all round).
    Rect tab(int index) const;
    // The inside of the page's frame, in the window's client coordinates.
    Rect page() const { return {rect.x + 2, rect.y + kStrip + 2, rect.w - 4, rect.h - kStrip - 4}; }

private:
    const char *const *titles_;
    int count_;
    Callback on_change_;
    void *context_;
    int selected_ = 0;
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
