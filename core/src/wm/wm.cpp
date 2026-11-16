#include "core/wm/wm.hpp"

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "core/gfx/art.hpp"

namespace wm {

using gfx::ButtonLook;
using gfx::Canvas;
using gfx::Rgb;
using gfx::RectList;

namespace {

// Colours from tools/mockup/desktop.py (98 style).
constexpr Rgb kDesktop = {58, 110, 165};
constexpr Rgb kActiveFrom = {0, 0, 128}, kActiveTo = {16, 132, 208};
constexpr Rgb kInactiveFrom = {128, 128, 128}, kInactiveTo = {192, 192, 192};
constexpr Rgb kHighlight = {0, 0, 128};                         // chosen menu item
constexpr Rgb kBannerTop = {0, 0, 48}, kBannerBottom = {16, 100, 190}; // Start menu

const char *const kCloseGlyph[] = {"XX    XX", " XX  XX ", "  XXXX  ", "   XX   ",
                                   "  XXXX  ", " XX  XX ", "XX    XX"};
const char *const kMaxGlyph[] = {"XXXXXXXXX", "XXXXXXXXX", "X       X", "X       X", "X       X",
                                 "X       X", "X       X", "X       X", "XXXXXXXXX"};
const char *const kRestoreGlyph[] = {"  XXXXXX", "  XXXXXX", "  X    X", "XXXXXX X", "XXXXXX X",
                                     "X    XXX", "X    X  ", "X    X  ", "XXXXXX  "};
const char *const kMinGlyph[] = {"      ", "      ", "      ", "      ",
                                 "      ", "      ", "XXXXXX", "XXXXXX"};
const char *const kCheckGlyph[] = {"      X", "     XX", "X   XXX", "XX XXX ",
                                   "XXXXX  ", " XXX   ", "  X    "};
const char *const kArrowGlyph[] = {"X   ", "XX  ", "XXX ", "XXXX", "XXX ", "XX  ", "X   "};

// Taskbar layout (tools/mockup/desktop.py).
constexpr int kStartW = 58;
constexpr int kQuickX = 66;    // separator before the quick-launch icons
constexpr int kButtonsX = 144; // first window button
constexpr int kButtonW = 150, kButtonGap = 3;
constexpr int kClockW = 60;
constexpr int kTrayW = 20; // a tray icon, left of the clock

// Desktop icons (tools/mockup/desktop.py).
constexpr int kIconX = 6, kIconY = 8, kIconStep = 70;

// Menus.
constexpr int kMenuEdge = 3;
constexpr int kItemH = 18, kIconItemH = 20, kStartItemH = 24, kSeparatorH = 7;
constexpr int kBannerW = 21;

const char *state_name(WindowState s)
{
    switch (s) {
    case WindowState::Minimized:
        return "minimized";
    case WindowState::Maximized:
        return "maximized";
    default:
        return "normal";
    }
}

// Draws a label with its access letter underlined. Returns the x after it.
int draw_label(Canvas &c, int x, int y, const char *label, gfx::Colour colour, bool bold = false)
{
    char plain[96];
    const int access = strip_access(label, plain, sizeof(plain));
    const int end = c.text(x, y, plain, colour, bold);
    if (access >= 0) {
        char before[96];
        memcpy(before, plain, access);
        before[access] = 0;
        const char one[2] = {plain[access], 0};
        c.hline(x + Canvas::text_width(before, bold), y + 11, Canvas::text_width(one, bold), colour);
    }
    return end;
}

int label_width(const char *label)
{
    char plain[96];
    strip_access(label, plain, sizeof(plain));
    return Canvas::text_width(plain);
}

bool selectable(const MenuItem &item)
{
    return !(item.flags & kMenuSeparator) && item.label != nullptr;
}

bool has_icons(const Menu &menu)
{
    for (int i = 0; i < menu.count; i++) {
        if (menu.items[i].icon != nullptr) {
            return true;
        }
    }
    return false;
}

int first_selectable(const Menu &menu)
{
    for (int i = 0; i < menu.count; i++) {
        if (selectable(menu.items[i])) {
            return i;
        }
    }
    return -1;
}

} // namespace

Wm::Wm(ports::Display &display, ports::Pointer &pointer, ports::Clock &clock)
    : display_(display), pointer_(pointer), clock_(clock), width_(display.width()),
      height_(display.height()), pointer_x_(display.width() / 2),
      pointer_y_(display.height() / 2)
{
}

void Wm::start()
{
    blink_timer_ = start_timer(kBlinkUs, kBlinkUs, &Wm::blink, this);
    start_timer(1000000, 1000000, &Wm::clock_tick, this);
    shape_ = &video::arrow_pointer();
    pointer_.set_shape(*shape_);
    pointer_.set_position(pointer_x_, pointer_y_);
    pointer_.set_visible(true);
    invalidate({0, 0, width_, height_});
    update();
}

/* ---- Window list --------------------------------------------------------- */

int Wm::stack_index(const Window &w) const
{
    for (int i = 0; i < count_; i++) {
        if (stack_[i] == &w) {
            return i;
        }
    }
    return -1;
}

Window *Wm::topmost_visible() const
{
    for (int i = count_ - 1; i >= 0; i--) {
        if (visible(*stack_[i])) {
            return stack_[i];
        }
    }
    return nullptr;
}

void Wm::add(Window &w)
{
    if (w.wm_ == this) {
        activate(w);
        return;
    }
    if (count_ == kMaxWindows || w.wm_ != nullptr) {
        return;
    }
    w.wm_ = this;
    w.id_ = next_id_++;
    w.state_ = WindowState::Normal;
    stack_[count_] = &w;
    order_[count_] = &w;
    count_++;
    activate(w);
    invalidate(w.frame_);
    invalidate(taskbar());
}

void Wm::close(Window &w)
{
    const int at = stack_index(w);
    if (at < 0) {
        return;
    }
    if (menu_window_ == &w) {
        close_menus();
    }
    for (int i = at; i + 1 < count_; i++) {
        stack_[i] = stack_[i + 1];
    }
    int o = 0;
    for (int i = 0; i < count_; i++) {
        if (order_[i] != &w) {
            order_[o++] = order_[i];
        }
    }
    count_--;
    if (captured_ == &w) {
        if (outline_shown_) {
            set_outline(outline_, false);
        }
        capture_ = Capture::None;
        captured_ = nullptr;
    }
    invalidate(w.frame_);
    invalidate(taskbar());
    w.wm_ = nullptr;
    w.captured_ = nullptr;
    if (active_ == &w) {
        active_ = nullptr;
        if (w.modal_owner_ != nullptr && w.modal_owner_->wm_ == this) {
            activate(*w.modal_owner_);
        } else if (Window *next = topmost_visible()) {
            activate(*next);
        }
    }
    w.closed();
}

void Wm::raise(Window &w)
{
    const int at = stack_index(w);
    if (at < 0 || at == count_ - 1) {
        return;
    }
    for (int i = at; i + 1 < count_; i++) {
        stack_[i] = stack_[i + 1];
    }
    stack_[count_ - 1] = &w;
    invalidate(w.frame_); // parts that were covered
}

void Wm::set_active(Window *w)
{
    if (active_ == w) {
        return;
    }
    if (active_ != nullptr) {
        invalidate(active_->frame_); // its title bar turns grey
    }
    active_ = w;
    if (w != nullptr) {
        invalidate(w->frame_);
    }
    invalidate(taskbar());
}

void Wm::activate(Window &w)
{
    if (w.wm_ != this) {
        return;
    }
    if (Window *b = blocker(w)) {
        activate(*b); // a modal dialog is in the way: it comes forward instead
        return;
    }
    if (w.state_ == WindowState::Minimized) {
        restore(w);
        return; // restore() activates
    }
    raise(w);
    set_active(&w);
}

void Wm::minimize(Window &w)
{
    if (w.wm_ != this || w.state_ == WindowState::Minimized) {
        return;
    }
    w.was_maximized_ = w.state_ == WindowState::Maximized;
    w.state_ = WindowState::Minimized;
    invalidate(w.frame_);
    invalidate(taskbar());
    if (active_ == &w) {
        active_ = nullptr;
        if (Window *next = topmost_visible()) {
            activate(*next);
        }
    }
}

void Wm::restore(Window &w)
{
    if (w.wm_ != this) {
        return;
    }
    if (w.state_ == WindowState::Minimized) {
        w.state_ = w.was_maximized_ ? WindowState::Maximized : WindowState::Normal;
        invalidate(w.frame_);
        invalidate(taskbar());
    }
    activate(w);
}

void Wm::toggle_maximize(Window &w)
{
    if (w.wm_ != this || !w.resizable_) {
        return;
    }
    invalidate(w.frame_);
    if (w.state_ == WindowState::Maximized) {
        w.frame_ = w.restore_;
        w.state_ = WindowState::Normal;
    } else {
        w.restore_ = w.frame_;
        w.frame_ = work_area();
        w.state_ = WindowState::Maximized;
    }
    w.resized();
    invalidate(w.frame_);
}

void Wm::move(Window &w, int x, int y)
{
    if (w.frame_.x == x && w.frame_.y == y) {
        return;
    }
    invalidate(w.frame_);
    w.frame_.x = x;
    w.frame_.y = y;
    invalidate(w.frame_);
}

void Wm::resize(Window &w, const Rect &frame)
{
    if (w.frame_ == frame) {
        return;
    }
    invalidate(w.frame_);
    w.frame_ = frame;
    w.resized();
    invalidate(w.frame_);
}

void Wm::cycle_windows()
{
    if (count_ >= 2) {
        activate(*stack_[0]);
    }
}

void Wm::set_desktop_icons(const DesktopIcon *icons, int count)
{
    icons_ = icons;
    icon_count_ = count < kMaxIcons ? count : kMaxIcons;
    invalidate(work_area());
}

void Wm::set_start_menu(Menu *menu, const char *banner)
{
    start_menu_ = menu;
    start_banner_ = banner;
}

/* ---- Hit testing --------------------------------------------------------- */

Window *Wm::window_under(int x, int y) const
{
    for (int i = count_ - 1; i >= 0; i--) {
        if (visible(*stack_[i]) && stack_[i]->frame_.contains(x, y)) {
            return stack_[i];
        }
    }
    return nullptr;
}

Rect Wm::title_button(const Window &w, Part which) const
{
    const Rect &f = w.frame_;
    const int close_x = f.right() - 22;
    if (w.dialog_ && which != Part::Close) {
        return {};
    }
    switch (which) {
    case Part::Close:
        return {close_x, f.y + 6, 16, 14};
    case Part::Maximize:
        return {close_x - 18, f.y + 6, 16, 14};
    case Part::Minimize:
        return {close_x - 34, f.y + 6, 16, 14};
    default:
        return {};
    }
}

Part Wm::part_at(const Window &w, int x, int y) const
{
    if (!w.frame_.contains(x, y)) {
        return Part::None;
    }
    for (Part p : {Part::Close, Part::Maximize, Part::Minimize}) {
        if (title_button(w, p).contains(x, y)) {
            return p;
        }
    }
    if (w.client().contains(x, y)) {
        return Part::Client;
    }
    if (w.menu_bar_rect().contains(x, y)) {
        return Part::MenuBar;
    }
    const Rect title = {w.frame_.x + Window::kBorder, w.frame_.y + Window::kBorder,
                        w.frame_.w - 2 * Window::kBorder, Window::kTitleHeight};
    if (title.contains(x, y)) {
        return Part::Title;
    }
    return Part::Border;
}

uint8_t Wm::edges_at(const Window &w, int x, int y) const
{
    const Rect &f = w.frame_;
    if (!w.resizable_ || w.state_ != WindowState::Normal || !f.contains(x, y) ||
        f.inset(Window::kBorder).contains(x, y)) {
        return 0;
    }
    constexpr int kCorner = 16; // along each edge, a corner grabs both ways
    uint8_t e = 0;
    if (x < f.x + Window::kBorder) {
        e |= kLeft;
    }
    if (x >= f.right() - Window::kBorder) {
        e |= kRight;
    }
    if (y < f.y + Window::kBorder) {
        e |= kTop;
    }
    if (y >= f.bottom() - Window::kBorder) {
        e |= kBottom;
    }
    if (e & (kTop | kBottom)) {
        if (x < f.x + kCorner) {
            e |= kLeft;
        }
        if (x >= f.right() - kCorner) {
            e |= kRight;
        }
    }
    if (e & (kLeft | kRight)) {
        if (y < f.y + kCorner) {
            e |= kTop;
        }
        if (y >= f.bottom() - kCorner) {
            e |= kBottom;
        }
    }
    return e;
}

Rect Wm::menu_bar_entry(const Window &w, int entry) const
{
    const Rect bar = w.menu_bar_rect();
    if (bar.empty() || entry < 0 || entry >= w.menu_bar_->count) {
        return {};
    }
    int x = bar.x;
    for (int i = 0; i < entry; i++) {
        x += label_width(w.menu_bar_->entries[i].label) + 12;
    }
    return {x, bar.y, label_width(w.menu_bar_->entries[entry].label) + 12, bar.h};
}

int Wm::menu_bar_entry_at(const Window &w, int x, int y) const
{
    if (w.menu_bar_ == nullptr) {
        return -1;
    }
    for (int i = 0; i < w.menu_bar_->count; i++) {
        if (menu_bar_entry(w, i).contains(x, y)) {
            return i;
        }
    }
    return -1;
}

int Wm::taskbar_count() const
{
    int n = 0;
    for (int i = 0; i < count_; i++) {
        n += !(order_[i]->modal_ && order_[i]->modal_owner_ != nullptr);
    }
    return n;
}

Window *Wm::taskbar_window(int index) const
{
    for (int i = 0; i < count_; i++) {
        if (order_[i]->modal_ && order_[i]->modal_owner_ != nullptr) {
            continue;
        }
        if (index-- == 0) {
            return order_[i];
        }
    }
    return nullptr;
}

Rect Wm::taskbar_button(int index) const
{
    const int ty = height_ - kTaskbarHeight;
    if (index < 0) {
        return {2, ty + 4, kStartW, 22};
    }
    const int room = width_ - kClockW - 6 - kButtonsX - (tray_icon_ != nullptr ? kTrayW : 0);
    const int shown = taskbar_count();
    int w = kButtonW;
    if (shown > 0 && shown * (kButtonW + kButtonGap) > room) {
        w = room / shown - kButtonGap;
    }
    return {kButtonsX + index * (w + kButtonGap), ty + 4, w, 22};
}

int Wm::taskbar_index_at(int x, int y) const
{
    if (taskbar_button(-1).contains(x, y)) {
        return -1;
    }
    for (int i = 0; i < taskbar_count(); i++) {
        if (taskbar_button(i).contains(x, y)) {
            return i;
        }
    }
    return -2; // nothing
}

Rect Wm::desktop_icon_rect(int index) const
{
    return {kIconX - 4, kIconY + index * kIconStep, 72, 50};
}

/* ---- Input --------------------------------------------------------------- */

void Wm::key(uint16_t usage, bool pressed)
{
    const uint32_t start = clock_.micros();
    last_input_us_ = now_us();
    if (saver_on_ && pressed) {
        stop_screen_saver(); // the key only wakes the screen
        return;
    }
    const bool modifier = usage >= key::LeftCtrl && usage <= key::RightGui;
    if (!modifier) {
        // A held key repeats, after a pause, until it is let go.
        if (pressed) {
            stop_timer(repeat_timer_);
            repeat_usage_ = usage;
            repeat_timer_ = start_timer(kRepeatDelayUs, kRepeatUs, &Wm::key_repeat, this);
        } else if (usage == repeat_usage_) {
            stop_timer(repeat_timer_);
            repeat_timer_ = 0;
            repeat_usage_ = 0;
        }
        caret_moved();
    }
    handle_key(usage, pressed);
    update();
    last_event_us_ = clock_.micros() - start;
}

void Wm::key_repeat(void *self)
{
    Wm &wm = *static_cast<Wm *>(self);
    if (wm.repeat_usage_ != 0) {
        wm.caret_moved();
        wm.handle_key(wm.repeat_usage_, true);
    }
}

void Wm::handle_key(uint16_t usage, bool pressed)
{
    uint8_t bit = 0;
    switch (usage) {
    case key::LeftShift:
    case key::RightShift:
        bit = kShift;
        break;
    case key::LeftCtrl:
    case key::RightCtrl:
        bit = kCtrl;
        break;
    case key::LeftAlt:
    case key::RightAlt:
        bit = kAlt;
        break;
    case key::LeftGui:
    case key::RightGui:
        bit = kGui;
        break;
    default:
        break;
    }
    if (bit != 0) {
        mods_ = pressed ? (mods_ | bit) : (mods_ & ~bit);
    }
    const KeyMsg k = {usage, pressed, mods_};
    bool used = false;
    if (pressed) {
        const bool start_key = bit == kGui || (usage == key::Escape && (mods_ & kCtrl));
        if (start_key) {
            if (start_menu_open()) {
                close_menus();
            } else {
                open_start_menu();
            }
            used = true;
        } else if (menu_open()) {
            used = menu_key(k);
        } else if ((mods_ & kAlt) && usage == key::F4) {
            if (active_ != nullptr && active_->close_requested()) {
                close(*active_);
            }
            used = true;
        } else if ((mods_ & kAlt) && usage == key::Tab) {
            cycle_windows();
            used = true;
        } else if ((mods_ & kAlt) && key::letter(usage) != 0 && active_ != nullptr &&
                   active_->menu_bar_ != nullptr) {
            const char letter = key::letter(usage);
            for (int i = 0; i < active_->menu_bar_->count; i++) {
                if (access_letter(active_->menu_bar_->entries[i].label) == letter) {
                    open_menu_bar(*active_, i);
                    set_hover(0, first_selectable(*popups_[0].menu));
                    used = true;
                    break;
                }
            }
        }
    } else if (menu_open()) {
        used = true;
    }
    if (!used && bit == 0 && active_ != nullptr) {
        used = active_->key(k);
        // Keys the window did not use may type a character.
        if (!used && pressed && !(mods_ & (kCtrl | kAlt | kGui))) {
            const char typed[2] = {key::us_char(usage, mods_ & kShift), 0};
            if (typed[0] != 0) {
                active_->text(typed);
            }
        }
    }
}

void Wm::text(const char *utf8)
{
    const uint32_t start = clock_.micros();
    last_input_us_ = now_us();
    if (saver_on_) {
        stop_screen_saver();
        return;
    }
    if (!menu_open() && active_ != nullptr) {
        caret_moved();
        active_->text(utf8);
    }
    update();
    last_event_us_ = clock_.micros() - start;
}

void Wm::mouse(const ports::MouseEvent &event)
{
    const uint32_t start = clock_.micros();
    int x = event.absolute ? event.x : pointer_x_ + event.x;
    int y = event.absolute ? event.y : pointer_y_ + event.y;
    x = x < 0 ? 0 : (x >= width_ ? width_ - 1 : x);
    y = y < 0 ? 0 : (y >= height_ ? height_ - 1 : y);
    uint8_t buttons = event.buttons;
    if (saver_on_) {
        // The hidden pointer follows the mouse. A press or a real movement
        // (not a nudge of the desk) wakes the screen and does nothing else.
        const int dx = x - saver_x_, dy = y - saver_y_;
        pointer_x_ = x;
        pointer_y_ = y;
        pointer_.set_position(x, y);
        if (buttons != 0 || dx <= -kSaverWakeMove || dx >= kSaverWakeMove || dy <= -kSaverWakeMove ||
            dy >= kSaverWakeMove) {
            swallow_button_ = buttons & 1;
            stop_screen_saver();
            update_pointer_shape();
        }
        return;
    }
    if (swallow_button_) {
        if (buttons & 1) {
            buttons &= static_cast<uint8_t>(~1);
        } else {
            swallow_button_ = false;
        }
    }
    if (x != pointer_x_ || y != pointer_y_ || buttons != buttons_) {
        last_input_us_ = now_us();
    }
    if (x != pointer_x_ || y != pointer_y_) {
        pointer_x_ = x;
        pointer_y_ = y;
        pointer_.set_position(x, y);
        on_move();
    }
    const bool was = buttons_ & 1, now = buttons & 1;
    buttons_ = buttons;
    if (!was && now) {
        on_press();
    } else if (was && !now) {
        on_release();
    }
    update_pointer_shape();
    update();
    last_event_us_ = clock_.micros() - start;
}

/* ---- Screensaver ---------------------------------------------------------- */

void Wm::set_screen_saver(ScreenSaver *saver, uint32_t idle_seconds)
{
    if (saver_on_ && saver != saver_) {
        stop_screen_saver();
    }
    saver_ = saver;
    saver_idle_us_ = static_cast<uint64_t>(idle_seconds) * 1000000;
    last_input_us_ = now_us(); // the wait starts again
}

void Wm::start_screen_saver()
{
    if (saver_ == nullptr || saver_on_) {
        return;
    }
    saver_on_ = true;
    saver_x_ = pointer_x_;
    saver_y_ = pointer_y_;
    pointer_.set_visible(false);
    saver_->begin(display_.pixels(), width_, height_, clock_.micros());
}

void Wm::stop_screen_saver()
{
    if (!saver_on_) {
        return;
    }
    saver_on_ = false;
    last_input_us_ = now_us();
    saver_->end(last_input_us_);
    pointer_.set_visible(true);
    invalidate({0, 0, width_, height_});
    update();
}

bool Wm::double_click(int target)
{
    const uint32_t now = clock_.micros();
    const int dx = pointer_x_ - last_press_x_, dy = pointer_y_ - last_press_y_;
    const bool twice = target == last_target_ && now - last_press_us_ < kDoubleClickUs &&
                       dx >= -4 && dx <= 4 && dy >= -4 && dy <= 4;
    last_target_ = twice ? -1 : target; // a third click starts again
    last_press_us_ = now;
    last_press_x_ = pointer_x_;
    last_press_y_ = pointer_y_;
    return twice;
}

void Wm::on_press()
{
    const int x = pointer_x_, y = pointer_y_;
    caret_moved();
    if (Window *sys = system_modal()) {
        // Only the dialog that holds the whole desktop can be used.
        if (!sys->frame_.contains(x, y)) {
            activate(*sys);
            capture_ = Capture::Swallow;
            return;
        }
    }
    if (menu_open()) {
        if (popup_at(x, y) >= 0) {
            capture_ = Capture::Menu;
            return;
        }
        if (menu_window_ != nullptr) {
            const int e = menu_bar_entry_at(*menu_window_, x, y);
            if (e >= 0 && e != menu_entry_) {
                open_menu_bar(*menu_window_, e);
                capture_ = Capture::Menu;
                return;
            }
        }
        // Anywhere else closes the menus, and the click goes no further.
        close_menus();
        capture_ = Capture::Swallow;
        return;
    }
    if (taskbar().contains(x, y)) {
        if (tray_icon_ != nullptr && tray_icon_rect().contains(x, y)) {
            capture_ = Capture::Swallow;
            if (tray_click_ != nullptr) {
                tray_click_(tray_context_);
            }
            return;
        }
        const int index = taskbar_index_at(x, y);
        if (index == -1 && start_menu_ != nullptr) {
            open_start_menu();
            capture_ = Capture::Menu;
        } else if (index >= -1) {
            capture_ = Capture::Taskbar;
            captured_index_ = index;
            captured_inside_ = true;
            invalidate(taskbar_button(index));
        }
        return;
    }
    Window *w = window_under(x, y);
    if (w == nullptr) {
        // The desktop: maybe an icon; no window keeps the focus.
        set_active(nullptr);
        int hit = -1;
        for (int i = 0; i < icon_count_; i++) {
            if (desktop_icon_rect(i).contains(x, y)) {
                hit = i;
            }
        }
        if (hit != selected_icon_) {
            if (selected_icon_ >= 0) {
                invalidate(desktop_icon_rect(selected_icon_));
            }
            selected_icon_ = hit;
            if (hit >= 0) {
                invalidate(desktop_icon_rect(hit));
            }
        }
        if (double_click(hit >= 0 ? hit : -2) && hit >= 0 && icons_[hit].open != nullptr) {
            icons_[hit].open(icons_[hit].context);
        }
        return;
    }
    if (Window *b = blocker(*w)) {
        activate(*b);
        capture_ = Capture::Swallow;
        return;
    }
    activate(*w);
    const Part part = part_at(*w, x, y);
    captured_ = w;
    switch (part) {
    case Part::Close:
    case Part::Maximize:
    case Part::Minimize:
        if (part == Part::Maximize && !w->resizable_) {
            capture_ = Capture::None;
            break;
        }
        capture_ = Capture::TitleButton;
        captured_part_ = part;
        captured_inside_ = true;
        invalidate(title_button(*w, part));
        break;
    case Part::Title:
        if (double_click(1000 + w->id_)) {
            toggle_maximize(*w);
            capture_ = Capture::None;
            break;
        }
        if (w->state_ == WindowState::Maximized) {
            capture_ = Capture::None; // a maximised window stays put
            break;
        }
        capture_ = Capture::Drag;
        grab_dx_ = x - w->frame_.x;
        grab_dy_ = y - w->frame_.y;
        set_outline(w->frame_, true);
        break;
    case Part::MenuBar: {
        const int e = menu_bar_entry_at(*w, x, y);
        capture_ = Capture::None;
        if (e >= 0) {
            open_menu_bar(*w, e);
            capture_ = Capture::Menu;
        }
        break;
    }
    case Part::Client: {
        capture_ = Capture::Client;
        const Rect c = w->client();
        const bool twice = double_click(2000 + w->id_);
        w->mouse({MouseKind::Down, x - c.x, y - c.y, twice});
        break;
    }
    case Part::Border: {
        resize_edges_ = edges_at(*w, x, y);
        capture_ = Capture::None;
        if (resize_edges_ != 0) {
            capture_ = Capture::Resize;
            resize_from_ = w->frame_;
            grab_dx_ = x;
            grab_dy_ = y;
            set_outline(w->frame_, true);
        }
        break;
    }
    default:
        capture_ = Capture::None;
        break;
    }
}

void Wm::on_move()
{
    const int x = pointer_x_, y = pointer_y_;
    if (menu_open()) {
        const int level = popup_at(x, y);
        if (level >= 0) {
            const int item = item_at(level, x, y);
            if (item != popups_[level].hover) {
                set_hover(level, item);
                close_popups_from(level + 1);
                stop_timer(submenu_timer_);
                submenu_timer_ = 0;
                if (item >= 0) {
                    const MenuItem &it = popups_[level].menu->items[item];
                    if (it.submenu != nullptr && !(it.flags & kMenuDisabled)) {
                        // A submenu opens after a short rest on its entry.
                        submenu_level_ = level;
                        submenu_item_ = item;
                        submenu_timer_ = start_timer(kSubmenuDelayUs, 0, &Wm::submenu_due, this);
                    }
                }
            }
        } else if (menu_window_ != nullptr) {
            const int e = menu_bar_entry_at(*menu_window_, x, y);
            if (e >= 0 && e != menu_entry_) {
                open_menu_bar(*menu_window_, e);
            }
        }
        return;
    }
    switch (capture_) {
    case Capture::Drag: {
        // Keep enough of the title bar on the screen to grab it again.
        const Rect &f = captured_->frame_;
        int nx = x - grab_dx_, ny = y - grab_dy_;
        const Rect area = work_area();
        if (nx < 40 - f.w) {
            nx = 40 - f.w;
        }
        if (nx > area.right() - 40) {
            nx = area.right() - 40;
        }
        if (ny < 0) {
            ny = 0;
        }
        if (ny > area.bottom() - Window::kClientTop) {
            ny = area.bottom() - Window::kClientTop;
        }
        set_outline({nx, ny, f.w, f.h}, true);
        break;
    }
    case Capture::Resize: {
        const Rect &f = resize_from_;
        const int dx = x - grab_dx_, dy = y - grab_dy_;
        const int min_w = Window::kMinWidth, min_h = captured_->min_height();
        int left = f.x, top = f.y, right = f.right(), bottom = f.bottom();
        if (resize_edges_ & kLeft) {
            left = f.x + dx < right - min_w ? f.x + dx : right - min_w;
        }
        if (resize_edges_ & kRight) {
            right = f.right() + dx > left + min_w ? f.right() + dx : left + min_w;
        }
        if (resize_edges_ & kTop) {
            top = f.y + dy < bottom - min_h ? f.y + dy : bottom - min_h;
            top = top < 0 ? 0 : top;
        }
        if (resize_edges_ & kBottom) {
            bottom = f.bottom() + dy > top + min_h ? f.bottom() + dy : top + min_h;
        }
        set_outline({left, top, right - left, bottom - top}, true);
        break;
    }
    case Capture::TitleButton: {
        const Rect b = title_button(*captured_, captured_part_);
        const bool inside = b.contains(x, y);
        if (inside != captured_inside_) {
            captured_inside_ = inside;
            invalidate(b);
        }
        break;
    }
    case Capture::Taskbar: {
        const bool inside = taskbar_button(captured_index_).contains(x, y);
        if (inside != captured_inside_) {
            captured_inside_ = inside;
            invalidate(taskbar_button(captured_index_));
        }
        break;
    }
    case Capture::Client: {
        const Rect c = captured_->client();
        captured_->mouse({MouseKind::Move, x - c.x, y - c.y});
        break;
    }
    default:
        break;
    }
}

void Wm::on_release()
{
    const Capture capture = capture_;
    Window *w = captured_;
    capture_ = Capture::None;
    captured_ = nullptr;
    switch (capture) {
    case Capture::Drag: {
        const Rect to = outline_;
        set_outline(outline_, false);
        move(*w, to.x, to.y);
        break;
    }
    case Capture::Resize: {
        const Rect to = outline_;
        set_outline(outline_, false);
        resize(*w, to);
        break;
    }
    case Capture::TitleButton:
        invalidate(title_button(*w, captured_part_));
        if (captured_inside_) {
            if (captured_part_ == Part::Close) {
                if (w->close_requested()) {
                    close(*w);
                }
            } else if (captured_part_ == Part::Maximize) {
                toggle_maximize(*w);
            } else {
                minimize(*w);
            }
        }
        break;
    case Capture::Taskbar:
        invalidate(taskbar_button(captured_index_));
        if (captured_inside_) {
            taskbar_click(captured_index_);
        }
        break;
    case Capture::Client: {
        const Rect c = w->client();
        w->mouse({MouseKind::Up, pointer_x_ - c.x, pointer_y_ - c.y});
        break;
    }
    case Capture::Menu: {
        const int level = popup_at(pointer_x_, pointer_y_);
        if (level >= 0) {
            const int item = item_at(level, pointer_x_, pointer_y_);
            if (item >= 0 && (popups_[level].menu->items[item].submenu == nullptr ||
                              popup_count_ == level + 1)) {
                choose(level, item);
            }
        }
        break;
    }
    default:
        break;
    }
}

void Wm::update_pointer_shape()
{
    uint8_t edges = 0;
    if (capture_ == Capture::Resize) {
        edges = resize_edges_;
    } else if (capture_ == Capture::None && !menu_open()) {
        if (Window *w = window_under(pointer_x_, pointer_y_)) {
            edges = edges_at(*w, pointer_x_, pointer_y_);
        }
    }
    const video::PointerShape *shape = &video::arrow_pointer();
    const bool horizontal = edges & (kLeft | kRight), vertical = edges & (kTop | kBottom);
    if (horizontal && vertical) {
        const bool falling = (edges & kLeft) == ((edges & kTop) ? kLeft : 0);
        shape = falling ? &video::resize_nwse_pointer() : &video::resize_nesw_pointer();
    } else if (horizontal) {
        shape = &video::resize_we_pointer();
    } else if (vertical) {
        shape = &video::resize_ns_pointer();
    }
    if (shape != shape_) {
        shape_ = shape;
        pointer_.set_shape(*shape);
    }
}

void Wm::taskbar_click(int index)
{
    Window *tw = index >= 0 ? taskbar_window(index) : nullptr;
    if (tw == nullptr) {
        return;
    }
    Window &w = *tw;
    if (active_ == &w && w.state_ != WindowState::Minimized) {
        minimize(w);
    } else {
        restore(w);
    }
}

/* ---- Menus ----------------------------------------------------------------- */

Rect Wm::popup_size(const Menu &menu, bool start) const
{
    int widest = 0, h = 2 * kMenuEdge;
    bool arrows = false;
    const bool icons = has_icons(menu);
    for (int i = 0; i < menu.count; i++) {
        const MenuItem &item = menu.items[i];
        if (!selectable(item)) {
            h += kSeparatorH;
            continue;
        }
        h += start ? kStartItemH : (icons ? kIconItemH : kItemH);
        const int w = label_width(item.label);
        widest = w > widest ? w : widest;
        arrows = arrows || item.submenu != nullptr;
    }
    int w = start ? kMenuEdge + kBannerW + 26 + widest + 24 + kMenuEdge
                  : kMenuEdge + (icons ? 26 : 20) + widest + 26 + kMenuEdge;
    const int min_w = start ? 150 : 100;
    (void)arrows;
    return {0, 0, w > min_w ? w : min_w, h};
}

Rect Wm::popup_item(int level, int item) const
{
    const Popup &p = popups_[level];
    const int item_h = p.start ? kStartItemH : (has_icons(*p.menu) ? kIconItemH : kItemH);
    int y = p.rect.y + kMenuEdge;
    for (int i = 0; i < p.menu->count; i++) {
        const bool sep = !selectable(p.menu->items[i]);
        const int h = sep ? kSeparatorH : item_h;
        if (i == item) {
            const int x = p.rect.x + kMenuEdge + (p.start ? kBannerW : 0);
            return {x, y, p.rect.right() - kMenuEdge - x, h};
        }
        y += h;
    }
    return {};
}

void Wm::open_popup(Menu &menu, int x, int y, bool start, bool below_point)
{
    if (popup_count_ == kMaxPopups) {
        return;
    }
    Rect r = popup_size(menu, start);
    r.x = x;
    r.y = start ? y - r.h : y;
    if (r.right() > width_) {
        // A submenu that does not fit opens to the left of its parent.
        r.x = (popup_count_ > 0 && !below_point) ? popups_[popup_count_ - 1].rect.x - r.w + kMenuEdge
                                                 : width_ - r.w;
    }
    r.x = r.x < 0 ? 0 : r.x;
    if (r.bottom() > height_) {
        r.y = height_ - r.h;
    }
    r.y = r.y < 0 ? 0 : r.y;
    popups_[popup_count_++] = {&menu, r, -1, start};
    invalidate(r);
}

void Wm::close_popups_from(int level)
{
    while (popup_count_ > level) {
        popup_count_--;
        invalidate(popups_[popup_count_].rect);
    }
}

void Wm::submenu_due(void *self)
{
    Wm &wm = *static_cast<Wm *>(self);
    wm.submenu_timer_ = 0;
    const int level = wm.submenu_level_, item = wm.submenu_item_;
    if (wm.popup_count_ == level + 1 && wm.popups_[level].hover == item) {
        const Rect r = wm.popup_item(level, item);
        wm.open_popup(*wm.popups_[level].menu->items[item].submenu, r.right() + kMenuEdge,
                      r.y - kMenuEdge, false, false);
    }
}

void Wm::close_menus()
{
    stop_timer(submenu_timer_);
    submenu_timer_ = 0;
    close_popups_from(0);
    if (menu_window_ != nullptr) {
        invalidate(menu_bar_entry(*menu_window_, menu_entry_));
        menu_window_ = nullptr;
        menu_entry_ = -1;
    }
    if (start_open_) {
        start_open_ = false;
        invalidate(taskbar_button(-1));
    }
}

void Wm::open_menu(Menu &menu, int x, int y)
{
    close_menus();
    open_popup(menu, x, y, false, true);
}

void Wm::open_start_menu()
{
    if (start_menu_ == nullptr) {
        return;
    }
    close_menus();
    open_popup(*start_menu_, 0, height_ - kTaskbarHeight, true, false);
    start_open_ = true;
    invalidate(taskbar_button(-1));
}

void Wm::open_menu_bar(Window &w, int entry)
{
    if (w.menu_bar_ == nullptr || entry < 0 || entry >= w.menu_bar_->count) {
        return;
    }
    close_menus();
    w.menu_opening(entry);
    menu_window_ = &w;
    menu_entry_ = entry;
    const Rect r = menu_bar_entry(w, entry);
    invalidate(r);
    open_popup(*w.menu_bar_->entries[entry].menu, r.x, r.bottom(), false, true);
}

void Wm::set_hover(int level, int item)
{
    Popup &p = popups_[level];
    if (p.hover == item) {
        return;
    }
    if (p.hover >= 0) {
        invalidate(popup_item(level, p.hover));
    }
    p.hover = item;
    if (item >= 0) {
        invalidate(popup_item(level, item));
    }
}

int Wm::popup_at(int x, int y) const
{
    for (int i = popup_count_ - 1; i >= 0; i--) {
        if (popups_[i].rect.contains(x, y)) {
            return i;
        }
    }
    return -1;
}

int Wm::item_at(int level, int x, int y) const
{
    const Popup &p = popups_[level];
    for (int i = 0; i < p.menu->count; i++) {
        if (selectable(p.menu->items[i]) && popup_item(level, i).contains(x, y)) {
            return i;
        }
    }
    return -1;
}

void Wm::choose(int level, int item)
{
    const MenuItem &it = popups_[level].menu->items[item];
    if (!selectable(it) || (it.flags & kMenuDisabled)) {
        return;
    }
    if (it.submenu != nullptr) {
        set_hover(level, item);
        close_popups_from(level + 1);
        const Rect r = popup_item(level, item);
        open_popup(*it.submenu, r.right() + kMenuEdge, r.y - kMenuEdge, false, false);
        if (popup_count_ > level + 1) {
            set_hover(level + 1, first_selectable(*it.submenu));
        }
        return;
    }
    void (*action)(void *) = it.action;
    void *context = it.context;
    close_menus();
    if (action != nullptr) {
        action(context);
    }
}

bool Wm::menu_key(const KeyMsg &k)
{
    const int level = popup_count_ - 1;
    Popup &p = popups_[level];
    const Menu &m = *p.menu;
    switch (k.usage) {
    case key::Up:
    case key::Down: {
        const int step = k.usage == key::Down ? 1 : m.count - 1;
        int i = p.hover < 0 ? (k.usage == key::Down ? m.count - 1 : 0) : p.hover;
        for (int n = 0; n < m.count; n++) {
            i = (i + step) % m.count;
            if (selectable(m.items[i])) {
                break;
            }
        }
        close_popups_from(level + 1);
        set_hover(level, i);
        return true;
    }
    case key::Right:
        if (p.hover >= 0 && m.items[p.hover].submenu != nullptr) {
            choose(level, p.hover);
        } else if (menu_window_ != nullptr) {
            Window &w = *menu_window_;
            open_menu_bar(w, (menu_entry_ + 1) % w.menu_bar_->count);
            set_hover(0, first_selectable(*popups_[0].menu));
        }
        return true;
    case key::Left:
        if (level > 0) {
            close_popups_from(level);
        } else if (menu_window_ != nullptr) {
            Window &w = *menu_window_;
            open_menu_bar(w, (menu_entry_ + w.menu_bar_->count - 1) % w.menu_bar_->count);
            set_hover(0, first_selectable(*popups_[0].menu));
        }
        return true;
    case key::Enter:
        if (p.hover >= 0) {
            choose(level, p.hover);
        }
        return true;
    case key::Escape:
        if (level > 0) {
            close_popups_from(level);
        } else {
            close_menus();
        }
        return true;
    default:
        break;
    }
    const char letter = key::letter(k.usage);
    if (letter != 0) {
        for (int i = 0; i < m.count; i++) {
            if (selectable(m.items[i]) && access_letter(m.items[i].label) == letter) {
                choose(level, i);
                return true;
            }
        }
    }
    return true; // menus take every key while open
}

/* ---- Painting ------------------------------------------------------------ */

void Wm::invalidate(const Rect &r)
{
    const Rect clipped = r.intersect({0, 0, width_, height_});
    if (clipped.empty()) {
        return;
    }
    dirty_.add(clipped);
    if (dirty_.overflowed()) {
        // Too many pieces: paint one rectangle around all of them.
        const Rect all = gfx::bounding(dirty_.bounds(), clipped);
        dirty_.set(all);
    }
}

void Wm::update()
{
    if (dirty_.empty() || saver_on_) {
        return; // the screensaver has the screen; all of it is painted when it ends
    }
    const uint32_t start = clock_.micros();
    const bool outline = outline_shown_;
    if (outline) {
        xor_outline(); // take it off; it goes back on top afterwards
    }
    paint(false);
    dirty_.clear();
    if (outline) {
        xor_outline();
    }
    last_update_us_ = clock_.micros() - start;
}

uint32_t Wm::repaint_all()
{
    stop_screen_saver();
    invalidate({0, 0, width_, height_});
    update();
    return last_update_us_;
}

void Wm::subtract_overlays(RectList &region, bool taskbar_too) const
{
    if (taskbar_too) {
        region.subtract(taskbar());
    }
    for (int i = 0; i < popup_count_; i++) {
        region.subtract(popups_[i].rect);
    }
}

void Wm::paint(bool painter)
{
    // Normally each part of the screen is painted once: every layer gets a
    // clip list of its visible part. If a list runs out of room, everything
    // is painted back to front instead (slower, still right).
    region_ = dirty_;
    if (!painter) {
        subtract_overlays(region_, true);
        for (int i = 0; i < count_; i++) {
            if (visible(*stack_[i])) {
                region_.subtract(stack_[i]->frame_);
            }
        }
        if (region_.overflowed()) {
            paint(true);
            return;
        }
    }
    if (!region_.empty()) {
        paint_desktop(region_);
    }

    for (int i = 0; i < count_; i++) {
        Window &w = *stack_[i];
        if (!visible(w)) {
            continue;
        }
        region_ = dirty_;
        region_.intersect(w.frame_);
        if (!painter) {
            subtract_overlays(region_, true);
            for (int j = i + 1; j < count_; j++) {
                if (visible(*stack_[j])) {
                    region_.subtract(stack_[j]->frame_);
                }
            }
            if (region_.overflowed()) {
                paint(true);
                return;
            }
        }
        if (!region_.empty()) {
            paint_window(w, region_);
        }
    }

    region_ = dirty_;
    region_.intersect(taskbar());
    if (!painter) {
        subtract_overlays(region_, false);
    }
    if (!region_.empty()) {
        paint_taskbar(region_);
    }

    for (int level = 0; level < popup_count_; level++) {
        region_ = dirty_;
        region_.intersect(popups_[level].rect);
        if (!painter) {
            for (int j = level + 1; j < popup_count_; j++) {
                region_.subtract(popups_[j].rect);
            }
        }
        if (!region_.empty()) {
            paint_popup(level, region_);
        }
    }
}

void Wm::paint_desktop(const RectList &clip)
{
    const Rect screen = {0, 0, width_, height_};
    Canvas c(display_.pixels(), width_, height_, clip, screen);
    c.fill_rgb(screen, kDesktop);
    const Rect bounds = clip.bounds();
    for (int i = 0; i < icon_count_; i++) {
        const Rect r = desktop_icon_rect(i);
        if (!r.overlaps(bounds)) {
            continue;
        }
        const DesktopIcon &icon = icons_[i];
        const int x = kIconX, y = kIconY + i * kIconStep;
        const bool selected = i == selected_icon_;
        if (icon.picture != nullptr) {
            if (selected) {
                c.blit_selected(*icon.picture, x + 16, y, gfx::rgb(0, 0, 2));
            } else {
                c.blit(*icon.picture, x + 16, y);
            }
        }
        const int tw = Canvas::text_width(icon.label);
        const int tx = x + 32 - tw / 2;
        if (selected) {
            c.fill_rgb({tx - 1, y + 33, tw + 2, 14}, kHighlight);
            c.text(tx, y + 34, icon.label, gfx::kWhite);
        } else {
            c.text(tx + 1, y + 35, icon.label, gfx::kBlack);
            c.text(tx, y + 34, icon.label, gfx::kWhite);
        }
    }
}

void Wm::paint_window(Window &w, const RectList &clip)
{
    uint8_t *pixels = display_.pixels();
    Canvas frame(pixels, width_, height_, clip, w.frame_);
    paint_frame(frame, w);
    if (w.menu_bar_ != nullptr) {
        paint_menu_bar(frame, w);
    }
    const Rect c = w.client();
    Canvas client(pixels, width_, height_, clip, c, c.x, c.y);
    w.paint(client);
}

void Wm::paint_frame(Canvas &c, Window &w)
{
    const Rect &f = w.frame_;
    const int b = Window::kBorder;
    const Rect title = {f.x + b, f.y + b, f.w - 2 * b, Window::kTitleHeight};
    // Each pixel once: the grey border inside the two-pixel edge, around the
    // title bar, client area and menu bar; the fade around the buttons.
    static RectList parts; // ponytail: one UI task
    parts.set(f.inset(2));
    parts.subtract(w.client());
    parts.subtract(title);
    parts.subtract(w.menu_bar_rect());
    for (int i = 0; i < parts.size(); i++) {
        c.fill(parts[i], gfx::kGrey);
    }
    c.raised(f);

    const bool active = active_ == &w;
    parts.set(title);
    for (Part p : {Part::Close, Part::Maximize, Part::Minimize}) {
        parts.subtract(title_button(w, p));
    }
    for (int i = 0; i < parts.size(); i++) {
        c.fill_fade(parts[i], active ? kActiveFrom : kInactiveFrom, active ? kActiveTo : kInactiveTo, false,
                    &title);
    }
    if (w.icon_ != nullptr) {
        c.blit(*w.icon_, f.x + 5, f.y + 6);
    }
    const Rect first = title_button(w, w.dialog_ ? Part::Close : Part::Minimize);
    Canvas text = c.sub({title.x, title.y, first.x - 2 - title.x, title.h});
    text.text(22, 3, w.title_, active ? gfx::kWhite : gfx::kGrey, true);

    for (Part p : {Part::Close, Part::Maximize, Part::Minimize}) {
        const Rect r = title_button(w, p);
        if (r.empty()) {
            continue;
        }
        const bool held = capture_ == Capture::TitleButton && captured_ == &w &&
                          captured_part_ == p && captured_inside_;
        c.button(r, held ? ButtonLook::Pressed : ButtonLook::Raised);
        const int d = held ? 1 : 0;
        // A window that cannot be resized shows its maximise button greyed.
        const gfx::Colour ink = (p == Part::Maximize && !w.resizable_) ? gfx::kDarkGrey : gfx::kBlack;
        if (p == Part::Close) {
            c.pattern(r.x + 4 + d, r.y + 3 + d, kCloseGlyph, 7, ink);
        } else if (p == Part::Maximize) {
            if (w.state_ == WindowState::Maximized) {
                c.pattern(r.x + 4 + d, r.y + 3 + d, kRestoreGlyph, 9, ink);
            } else {
                if (!w.resizable_) {
                    c.pattern(r.x + 5, r.y + 4, kMaxGlyph, 9, gfx::kWhite);
                }
                c.pattern(r.x + 4 + d, r.y + 3 + d, kMaxGlyph, 9, ink);
            }
        } else {
            c.pattern(r.x + 4 + d, r.y + 3 + d, kMinGlyph, 8, ink);
        }
    }
}

void Wm::paint_menu_bar(Canvas &c, Window &w)
{
    const Rect bar = w.menu_bar_rect();
    c.fill(bar, gfx::kGrey);
    for (int i = 0; i < w.menu_bar_->count; i++) {
        const Rect r = menu_bar_entry(w, i);
        const bool open = menu_window_ == &w && menu_entry_ == i;
        if (open) {
            c.edge({r.x, r.y + 1, r.w, r.h - 1}, gfx::kDarkGrey, gfx::kWhite);
        }
        const int d = open ? 1 : 0;
        draw_label(c, r.x + 6 + d, r.y + 2 + d, w.menu_bar_->entries[i].label, gfx::kBlack);
    }
}

void Wm::paint_popup(int level, const RectList &clip)
{
    const Popup &p = popups_[level];
    const Rect &r = p.rect;
    Canvas c(display_.pixels(), width_, height_, clip, r);
    c.fill(r, gfx::kGrey);
    c.raised(r);
    if (p.start) {
        const Rect banner = {r.x + kMenuEdge, r.y + kMenuEdge, kBannerW, r.h - 2 * kMenuEdge};
        c.fill_fade(banner, kBannerTop, kBannerBottom, true);
        c.text_up(banner.x + (kBannerW - 12) / 2, banner.bottom() - 6, start_banner_, gfx::kWhite, true);
    }
    for (int i = 0; i < p.menu->count; i++) {
        const MenuItem &item = p.menu->items[i];
        const Rect ir = popup_item(level, i);
        if (!selectable(item)) {
            c.hline(ir.x + 1, ir.y + 3, ir.w - 2, gfx::kDarkGrey);
            c.hline(ir.x + 1, ir.y + 4, ir.w - 2, gfx::kWhite);
            continue;
        }
        const bool hover = i == p.hover;
        const bool disabled = item.flags & kMenuDisabled;
        if (hover) {
            c.fill_rgb(ir, kHighlight);
        }
        const gfx::Colour ink = hover ? (disabled ? gfx::kGrey : gfx::kWhite) : gfx::kBlack;
        const bool icons = p.start || has_icons(*p.menu);
        const int tx = icons ? ir.x + 26 : ir.x + 17;
        const int ty = ir.y + (ir.h - 12) / 2;
        if (icons && item.icon != nullptr) {
            c.blit(*item.icon, ir.x + 4, ir.y + (ir.h - 16) / 2);
        }
        if (item.flags & kMenuChecked) {
            c.pattern(ir.x + 5, ir.y + (ir.h - 7) / 2, kCheckGlyph, 7, disabled && !hover ? gfx::kDarkGrey : ink);
        }
        if (disabled && !hover) {
            draw_label(c, tx + 1, ty + 1, item.label, gfx::kWhite);
            draw_label(c, tx, ty, item.label, gfx::kDarkGrey);
        } else {
            draw_label(c, tx, ty, item.label, ink);
        }
        if (item.submenu != nullptr) {
            c.pattern(ir.right() - 10, ir.y + (ir.h - 7) / 2, kArrowGlyph, 7,
                      disabled && !hover ? gfx::kDarkGrey : ink);
        }
    }
}

void Wm::paint_taskbar(const RectList &clip)
{
    uint8_t *pixels = display_.pixels();
    const Rect bar = taskbar();
    const int ty = bar.y;
    Canvas c(pixels, width_, height_, clip, bar);
    c.fill(bar, gfx::kGrey);
    c.hline(0, ty + 1, width_, gfx::kWhite);

    // Start button: the mascot and "Start", pressed in while its menu is open.
    const Rect start = taskbar_button(-1);
    const bool start_in = start_open_ ||
                          (capture_ == Capture::Taskbar && captured_index_ == -1 && captured_inside_);
    c.button(start, start_in ? ButtonLook::Pressed : ButtonLook::Raised);
    const int sd = start_in ? 1 : 0;
    c.blit(gfx::mascot_16(), 5 + sd, ty + 7 + sd);
    c.text(25 + sd, ty + 9 + sd, "Start", gfx::kBlack, true);

    // Quick-launch icons between two grooves. ponytail: they do nothing yet.
    c.vline(kQuickX, ty + 5, 20, gfx::kDarkGrey);
    c.vline(kQuickX + 1, ty + 5, 20, gfx::kWhite);
    c.blit(gfx::folder_icon_16(), kQuickX + 8, ty + 7);
    c.blit(gfx::text_icon_16(), kQuickX + 30, ty + 7);
    c.blit(gfx::network_icon_16(), kQuickX + 52, ty + 7);
    c.vline(kButtonsX - 4, ty + 5, 20, gfx::kDarkGrey);
    c.vline(kButtonsX - 3, ty + 5, 20, gfx::kWhite);

    // One button per window, in the order they were opened. A window whose
    // dialog is open shows pressed while the dialog has the focus.
    for (int i = 0; i < taskbar_count(); i++) {
        Window &w = *taskbar_window(i);
        const Rect r = taskbar_button(i);
        const bool held = capture_ == Capture::Taskbar && captured_index_ == i && captured_inside_;
        const bool owns_active = active_ != nullptr && active_->modal_owner_ == &w;
        const bool on = (active_ == &w || owns_active) && w.state_ != WindowState::Minimized;
        c.button(r, on ? ButtonLook::Checked : (held ? ButtonLook::Pressed : ButtonLook::Raised));
        const int d = (on || held) ? 1 : 0;
        Canvas inner = c.sub(r.inset(3));
        if (w.icon_ != nullptr) {
            inner.blit(*w.icon_, 1 + d, 0 + d);
        }
        inner.text(21 + d, 2 + d, w.title_, gfx::kBlack, on);
    }

    // The clock, 12-hour as in the target picture.
    const Rect clock = clock_rect();
    if (tray_icon_ != nullptr) {
        const Rect icon = tray_icon_rect();
        c.edge({icon.x, clock.y, kTrayW + kClockW, clock.h}, gfx::kDarkGrey, gfx::kWhite);
        c.blit(*tray_icon_, icon.x + 3, icon.y + 3);
    } else {
        c.edge(clock, gfx::kDarkGrey, gfx::kWhite);
    }
    int h, m, s;
    get_time(h, m, s);
    char time[12];
    snprintf(time, sizeof(time), "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM");
    c.text(clock.x + (kClockW - Canvas::text_width(time)) / 2, ty + 9, time, gfx::kBlack);
}

/* ---- Timers and clock ------------------------------------------------------- */

uint64_t Wm::now_us()
{
    const uint32_t raw = clock_.micros();
    if (!clock_started_) {
        clock_started_ = true;
        last_raw_us_ = raw;
    }
    now64_ += static_cast<uint32_t>(raw - last_raw_us_);
    last_raw_us_ = raw;
    return now64_;
}

int Wm::start_timer(uint32_t delay_us, uint32_t period_us, TimerFn fn, void *context)
{
    for (Timer &t : timers_) {
        if (t.id == 0) {
            t = {fn, context, now_us() + delay_us, period_us, next_timer_id_++};
            if (next_timer_id_ <= 0) {
                next_timer_id_ = 1;
            }
            return t.id;
        }
    }
    return 0;
}

void Wm::stop_timer(int id)
{
    if (id == 0) {
        return;
    }
    for (Timer &t : timers_) {
        if (t.id == id) {
            t.id = 0;
        }
    }
}

void Wm::tick()
{
    const uint64_t now = now_us();
    for (Timer &t : timers_) {
        if (t.id == 0 || now < t.due) {
            continue;
        }
        const TimerFn fn = t.fn;
        void *context = t.context;
        if (t.period != 0) {
            t.due += t.period;
            if (t.due <= now) {
                t.due = now + t.period; // fell behind: no burst of catch-up calls
            }
        } else {
            t.id = 0;
        }
        fn(context);
    }
    if (saver_on_) {
        saver_->tick(now);
    } else if (saver_ != nullptr && saver_idle_us_ != 0 && buttons_ == 0 && capture_ == Capture::None &&
               now - last_input_us_ >= saver_idle_us_) {
        start_screen_saver();
    }
    update();
}

void Wm::blink(void *self)
{
    Wm &wm = *static_cast<Wm *>(self);
    wm.caret_on_ = !wm.caret_on_;
    if (wm.animations_ && wm.active_ != nullptr && wm.active_->focus() != nullptr) {
        Widget *w = wm.active_->focus();
        w->invalidate_part(w->caret_rect()); // only the caret
    }
}

void Wm::caret_moved()
{
    if (!caret_on_ && active_ != nullptr && active_->focus() != nullptr) {
        Widget *w = active_->focus();
        w->invalidate_part(w->caret_rect());
    }
    caret_on_ = true;
    stop_timer(blink_timer_);
    if (blink_timer_ != 0) {
        blink_timer_ = start_timer(kBlinkUs, kBlinkUs, &Wm::blink, this);
    }
}

void Wm::set_animations(bool on)
{
    animations_ = on;
    if (active_ != nullptr && active_->focus() != nullptr) {
        active_->focus()->invalidate();
    }
    update();
}

Rect Wm::clock_rect() const
{
    return {width_ - kClockW - 2, height_ - kTaskbarHeight + 4, kClockW, 22};
}

Rect Wm::tray_icon_rect() const
{
    const Rect clock = clock_rect();
    return tray_icon_ != nullptr ? Rect{clock.x - kTrayW, clock.y, kTrayW, clock.h} : Rect{};
}

void Wm::set_tray_icon(const gfx::Bitmap *icon, void (*click)(void *context), void *context)
{
    tray_click_ = click;
    tray_context_ = context;
    if (icon == tray_icon_) {
        return;
    }
    // A first or a last icon moves the window buttons: the whole bar.
    const bool whole = (icon == nullptr) != (tray_icon_ == nullptr);
    tray_icon_ = icon;
    invalidate(whole ? taskbar() : tray_icon_rect());
}

void Wm::set_time(int hours, int minutes, int seconds)
{
    time_base_s_ = hours * 3600 + minutes * 60 + seconds;
    time_base_us_ = now_us();
    invalidate(clock_rect());
    update();
}

void Wm::get_time(int &hours, int &minutes, int &seconds)
{
    const int64_t s = (time_base_s_ + static_cast<int64_t>((now_us() - time_base_us_) / 1000000)) % 86400;
    hours = static_cast<int>(s / 3600);
    minutes = static_cast<int>(s / 60 % 60);
    seconds = static_cast<int>(s % 60);
}

void Wm::clock_tick(void *self)
{
    Wm &wm = *static_cast<Wm *>(self);
    int h, m, s;
    wm.get_time(h, m, s);
    if (h * 60 + m != wm.shown_minute_) {
        wm.shown_minute_ = h * 60 + m;
        wm.invalidate(wm.clock_rect());
    }
}

/* ---- Modal dialogs ---------------------------------------------------------- */

Window *Wm::system_modal() const
{
    for (int i = count_ - 1; i >= 0; i--) {
        if (stack_[i]->modal_ && stack_[i]->system_modal_) {
            return stack_[i];
        }
    }
    return nullptr;
}

Window *Wm::blocker(const Window &w) const
{
    for (int i = count_ - 1; i >= 0; i--) {
        const Window *d = stack_[i];
        if (d == &w || !d->modal_) {
            continue;
        }
        if (d->system_modal_ || d->modal_owner_ == &w) {
            return stack_[i];
        }
    }
    return nullptr;
}

/* ---- Drag outline ---------------------------------------------------------- */

void Wm::set_outline(const Rect &r, bool shown)
{
    if (outline_shown_) {
        xor_outline();
    }
    outline_ = r;
    outline_shown_ = shown;
    if (outline_shown_) {
        xor_outline();
    }
}

// A dotted frame drawn by inverting every other pixel, so drawing it again
// takes it off exactly. Nothing else is painted while it is on.
void Wm::xor_outline()
{
    const Rect &r = outline_;
    const int t = kOutline;
    const Rect bands[4] = {
        {r.x, r.y, r.w, t},
        {r.x, r.bottom() - t, r.w, t},
        {r.x, r.y + t, t, r.h - 2 * t},
        {r.right() - t, r.y + t, t, r.h - 2 * t},
    };
    uint8_t *pixels = display_.pixels();
    const Rect screen = {0, 0, width_, height_};
    for (const Rect &band : bands) {
        const Rect s = band.intersect(screen);
        for (int y = s.y; y < s.bottom(); y++) {
            uint8_t *row = pixels + y * width_;
            for (int x = s.x + ((s.x + y) & 1); x < s.right(); x += 2) {
                row[x] ^= 0xFF;
            }
        }
    }
}

/* ---- Inspection ------------------------------------------------------------ */

size_t Wm::describe_windows(char *out, size_t capacity)
{
    if (capacity == 0) {
        return 0;
    }
    size_t used = 0;
    bool full = false;
    auto put = [&](const char *s, size_t n) {
        if (full || used + n >= capacity) {
            full = true;
            return;
        }
        memcpy(out + used, s, n);
        used += n;
    };
    auto puts = [&](const char *s) { put(s, strlen(s)); };
    char number[128];

    puts("{\"windows\":[");
    for (int z = 0; z < count_; z++) {
        const Window &w = *stack_[z];
        if (z > 0) {
            puts(",");
        }
        snprintf(number, sizeof(number), "{\"id\":%d,\"z\":%d,\"title\":\"", w.id_, z);
        puts(number);
        for (const char *p = w.title_; *p != 0; p++) {
            if (*p == '"' || *p == '\\') {
                const char esc[2] = {'\\', *p};
                put(esc, 2);
            } else if (static_cast<uint8_t>(*p) < 0x20) {
                puts(" ");
            } else {
                put(p, 1);
            }
        }
        snprintf(number, sizeof(number),
                 "\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"state\":\"%s\",\"focused\":%s}",
                 w.frame_.x, w.frame_.y, w.frame_.w, w.frame_.h, state_name(w.state_),
                 active_ == &w ? "true" : "false");
        puts(number);
    }
    snprintf(number, sizeof(number),
             "],\"menus\":%d,\"start_menu\":%s,\"selected_icon\":%d,\"update_us\":%u,\"event_us\":%u}",
             popup_count_, start_open_ ? "true" : "false", selected_icon_,
             static_cast<unsigned>(last_update_us_), static_cast<unsigned>(last_event_us_));
    puts(number);
    if (full) {
        // Too long for the buffer: say so in valid JSON instead.
        const char *msg = "{\"error\":\"too many windows\"}";
        used = strlen(msg) < capacity ? strlen(msg) : 0;
        memcpy(out, msg, used);
    }
    out[used] = 0;
    return used;
}

} // namespace wm
