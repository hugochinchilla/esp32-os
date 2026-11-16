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

const char *const kCloseGlyph[] = {"XX    XX", " XX  XX ", "  XXXX  ", "   XX   ",
                                   "  XXXX  ", " XX  XX ", "XX    XX"};
const char *const kMaxGlyph[] = {"XXXXXXXXX", "XXXXXXXXX", "X       X", "X       X", "X       X",
                                 "X       X", "X       X", "X       X", "XXXXXXXXX"};
const char *const kRestoreGlyph[] = {"  XXXXXX", "  XXXXXX", "  X    X", "XXXXXX X", "XXXXXX X",
                                     "X    XXX", "X    X  ", "X    X  ", "XXXXXX  "};
const char *const kMinGlyph[] = {"      ", "      ", "      ", "      ",
                                 "      ", "      ", "XXXXXX", "XXXXXX"};

// Taskbar layout (tools/mockup/desktop.py).
constexpr int kStartW = 58;
constexpr int kQuickX = 66;   // separator before the quick-launch icons
constexpr int kButtonsX = 144; // first window button
constexpr int kButtonW = 150, kButtonGap = 3;
constexpr int kClockW = 60;

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

} // namespace

Wm::Wm(ports::Display &display, ports::Pointer &pointer, ports::Clock &clock)
    : display_(display), pointer_(pointer), clock_(clock), width_(display.width()),
      height_(display.height()), pointer_x_(display.width() / 2),
      pointer_y_(display.height() / 2)
{
}

void Wm::start()
{
    pointer_.set_shape(video::arrow_pointer());
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
    if (count_ == kMaxWindows || w.wm_ != nullptr) {
        return;
    }
    w.wm_ = this;
    w.id_ = next_id_++;
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
        if (capture_ == Capture::Drag) {
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
        if (Window *next = topmost_visible()) {
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

void Wm::activate(Window &w)
{
    if (w.wm_ != this) {
        return;
    }
    if (w.state_ == WindowState::Minimized) {
        restore(w);
        return; // restore() activates
    }
    raise(w);
    if (active_ != &w) {
        if (active_ != nullptr) {
            invalidate(active_->frame_); // its title bar turns grey
        }
        active_ = &w;
        invalidate(w.frame_);
        invalidate(taskbar());
    }
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
    if (w.wm_ != this) {
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
    const Rect title = {w.frame_.x + Window::kBorder, w.frame_.y + Window::kBorder,
                        w.frame_.w - 2 * Window::kBorder, Window::kTitleHeight};
    if (title.contains(x, y)) {
        return Part::Title;
    }
    return Part::Border;
}

Rect Wm::taskbar_button(int index) const
{
    const int ty = height_ - kTaskbarHeight;
    if (index < 0) {
        return {2, ty + 4, kStartW, 22};
    }
    const int room = width_ - kClockW - 6 - kButtonsX;
    int w = kButtonW;
    if (count_ > 0 && count_ * (kButtonW + kButtonGap) > room) {
        w = room / count_ - kButtonGap;
    }
    return {kButtonsX + index * (w + kButtonGap), ty + 4, w, 22};
}

int Wm::taskbar_index_at(int x, int y) const
{
    if (taskbar_button(-1).contains(x, y)) {
        return -1;
    }
    for (int i = 0; i < count_; i++) {
        if (taskbar_button(i).contains(x, y)) {
            return i;
        }
    }
    return -2; // nothing
}

/* ---- Input --------------------------------------------------------------- */

void Wm::key(uint16_t usage, bool pressed)
{
    const uint32_t start = clock_.micros();
    if (active_ != nullptr) {
        active_->key(usage, pressed);
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
    if (x != pointer_x_ || y != pointer_y_) {
        pointer_x_ = x;
        pointer_y_ = y;
        pointer_.set_position(x, y);
        on_move();
    }
    const bool was = buttons_ & 1, now = event.buttons & 1;
    buttons_ = event.buttons;
    if (!was && now) {
        on_press();
    } else if (was && !now) {
        on_release();
    }
    update();
    last_event_us_ = clock_.micros() - start;
}

void Wm::on_press()
{
    const int x = pointer_x_, y = pointer_y_;
    if (taskbar().contains(x, y)) {
        const int index = taskbar_index_at(x, y);
        if (index >= -1) {
            capture_ = Capture::Taskbar;
            captured_index_ = index;
            captured_inside_ = true;
            invalidate(taskbar_button(index));
        }
        return;
    }
    Window *w = window_under(x, y);
    if (w == nullptr) {
        // The desktop takes the focus: no window is active.
        if (active_ != nullptr) {
            invalidate(active_->frame_);
            invalidate(taskbar());
            active_ = nullptr;
        }
        return;
    }
    activate(*w);
    const Part part = part_at(*w, x, y);
    captured_ = w;
    switch (part) {
    case Part::Close:
    case Part::Maximize:
    case Part::Minimize:
        capture_ = Capture::TitleButton;
        captured_part_ = part;
        captured_inside_ = true;
        invalidate(title_button(*w, part));
        break;
    case Part::Title:
        if (w->state_ == WindowState::Maximized) {
            capture_ = Capture::None; // a maximised window stays put
            break;
        }
        capture_ = Capture::Drag;
        grab_dx_ = x - w->frame_.x;
        grab_dy_ = y - w->frame_.y;
        set_outline(w->frame_, true);
        break;
    case Part::Client: {
        capture_ = Capture::Client;
        const Rect c = w->client();
        w->mouse({MouseKind::Down, x - c.x, y - c.y});
        break;
    }
    default:
        capture_ = Capture::None; // the border: resizing comes later
        break;
    }
}

void Wm::on_move()
{
    const int x = pointer_x_, y = pointer_y_;
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
    case Capture::TitleButton:
        invalidate(title_button(*w, captured_part_));
        if (captured_inside_) {
            if (captured_part_ == Part::Close) {
                close(*w);
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
    default:
        break;
    }
}

void Wm::taskbar_click(int index)
{
    if (index < 0) {
        return; // ponytail: the Start menu is a later step
    }
    if (index >= count_) {
        return;
    }
    Window &w = *order_[index];
    if (active_ == &w && w.state_ != WindowState::Minimized) {
        minimize(w);
    } else {
        restore(w);
    }
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
    if (dirty_.empty()) {
        return;
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
    invalidate({0, 0, width_, height_});
    update();
    return last_update_us_;
}

void Wm::paint(bool painter)
{
    // Normally each part of the screen is painted once: every window gets a
    // clip list of its visible part. If a list runs out of room, everything
    // is painted back to front instead (slower, still right).
    const Rect screen = {0, 0, width_, height_};
    const Rect bar = taskbar();
    uint8_t *pixels = display_.pixels();

    region_ = dirty_;
    region_.subtract(bar);
    if (!painter) {
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
        Canvas c(pixels, width_, height_, region_, screen);
        c.fill_rgb(screen, kDesktop);
    }

    for (int i = 0; i < count_; i++) {
        Window &w = *stack_[i];
        if (!visible(w)) {
            continue;
        }
        region_ = dirty_;
        region_.intersect(w.frame_);
        region_.subtract(bar);
        if (!painter) {
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
    region_.intersect(bar);
    if (!region_.empty()) {
        paint_taskbar(region_);
    }
}

void Wm::paint_window(Window &w, const RectList &clip)
{
    uint8_t *pixels = display_.pixels();
    Canvas frame(pixels, width_, height_, clip, w.frame_);
    paint_frame(frame, w);
    const Rect c = w.client();
    Canvas client(pixels, width_, height_, clip, c, c.x, c.y);
    w.paint(client);
}

void Wm::paint_frame(Canvas &c, Window &w)
{
    const Rect &f = w.frame_;
    const int b = Window::kBorder;
    // The border strips and the title area; the client area is the window's.
    c.fill({f.x, f.y, f.w, Window::kClientTop}, gfx::kGrey);
    c.fill({f.x, f.bottom() - b, f.w, b}, gfx::kGrey);
    c.fill({f.x, f.y, b, f.h}, gfx::kGrey);
    c.fill({f.right() - b, f.y, b, f.h}, gfx::kGrey);
    c.raised(f);

    const bool active = active_ == &w;
    const Rect title = {f.x + b, f.y + b, f.w - 2 * b, Window::kTitleHeight};
    c.fill_fade(title, active ? kActiveFrom : kInactiveFrom, active ? kActiveTo : kInactiveTo);
    if (w.icon_ != nullptr) {
        c.blit(*w.icon_, f.x + 5, f.y + 6);
    }
    const Rect minimize = title_button(w, Part::Minimize);
    Canvas text = c.sub({title.x, title.y, minimize.x - 2 - title.x, title.h});
    text.text(22, 3, w.title_, active ? gfx::kWhite : gfx::kGrey, true);

    for (Part p : {Part::Close, Part::Maximize, Part::Minimize}) {
        const Rect r = title_button(w, p);
        const bool held = capture_ == Capture::TitleButton && captured_ == &w &&
                          captured_part_ == p && captured_inside_;
        c.button(r, held ? ButtonLook::Pressed : ButtonLook::Raised);
        const int d = held ? 1 : 0;
        if (p == Part::Close) {
            c.pattern(r.x + 4 + d, r.y + 3 + d, kCloseGlyph, 7, gfx::kBlack);
        } else if (p == Part::Maximize) {
            if (w.state_ == WindowState::Maximized) {
                c.pattern(r.x + 4 + d, r.y + 3 + d, kRestoreGlyph, 9, gfx::kBlack);
            } else {
                c.pattern(r.x + 4 + d, r.y + 3 + d, kMaxGlyph, 9, gfx::kBlack);
            }
        } else {
            c.pattern(r.x + 4 + d, r.y + 3 + d, kMinGlyph, 8, gfx::kBlack);
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

    // Start button: the mascot and "Start".
    const Rect start = taskbar_button(-1);
    const bool start_held = capture_ == Capture::Taskbar && captured_index_ == -1 && captured_inside_;
    c.button(start, start_held ? ButtonLook::Pressed : ButtonLook::Raised);
    const int sd = start_held ? 1 : 0;
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

    // One button per window, in the order they were opened.
    for (int i = 0; i < count_; i++) {
        Window &w = *order_[i];
        const Rect r = taskbar_button(i);
        const bool held = capture_ == Capture::Taskbar && captured_index_ == i && captured_inside_;
        const bool on = active_ == &w && w.state_ != WindowState::Minimized;
        c.button(r, on ? ButtonLook::Checked : (held ? ButtonLook::Pressed : ButtonLook::Raised));
        const int d = (on || held) ? 1 : 0;
        Canvas inner = c.sub(r.inset(3));
        if (w.icon_ != nullptr) {
            inner.blit(*w.icon_, 1 + d, 0 + d);
        }
        inner.text(21 + d, 2 + d, w.title_, gfx::kBlack, on);
    }

    // The clock. ponytail: placeholder until there is a clock.
    const Rect clock = {width_ - kClockW - 2, ty + 4, kClockW, 22};
    c.edge(clock, gfx::kDarkGrey, gfx::kWhite);
    const char *time = "--:--";
    c.text(clock.x + (kClockW - Canvas::text_width(time)) / 2, ty + 9, time, gfx::kBlack);
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
    char number[96];

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
    snprintf(number, sizeof(number), "],\"update_us\":%u,\"event_us\":%u}",
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
