// The window manager: overlapping windows in a stacking order, the desktop
// with its icons behind them, the taskbar in front, and pop-up menus (menu
// bars, the Start menu) on top of everything. It works out what is visible
// and asks each window to paint only that, routes the mouse and the
// keyboard, and draws frames, menus and the taskbar in the 98 look.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/gfx/canvas.hpp"
#include "core/gfx/rect.hpp"
#include "core/ports.hpp"
#include "core/proc/table.hpp"
#include "core/wm/window.hpp"

namespace wm {

// Parts of a window frame, for hit testing.
enum class Part : uint8_t { None, Border, Title, MenuBar, Client, Minimize, Maximize, Close };

// Edges grabbed when resizing, as bits.
enum Edge : uint8_t { kLeft = 1, kRight = 2, kTop = 4, kBottom = 8 };

// An icon on the desktop: a 32x32 picture with a label under it.
struct DesktopIcon {
    const char *label;
    const gfx::Bitmap *picture;
    void (*open)(void *context);
    void *context;
    // The file the icon stands for: such icons can be dragged.
    const char *path = nullptr;
    // Takes a dragged file let go on the icon; nullptr: not a drop target.
    void (*drop)(const char *path, void *context) = nullptr;
};

// A screensaver: takes the whole screen over after a while with no input.
// While it shows, the window manager paints nothing and hides the pointer;
// the next key press, click or real mouse movement brings the desktop back
// and is not passed on to any window.
class ScreenSaver {
public:
    // The screen is the saver's from here on.
    virtual void begin(uint8_t *pixels, int width, int height, uint32_t seed) = 0;
    // Called from every Wm::tick() while it shows; draws when a frame is due.
    virtual void tick(uint64_t now_us) = 0;
    virtual void end(uint64_t /*now_us*/) {}
    // When its next frame is due, on the clock tick() gives it; 0: at every
    // tick. The UI loop sleeps until then.
    virtual uint64_t next_frame_us() const { return 0; }

protected:
    ~ScreenSaver() = default;
};

class Wm : public ports::InputSink, public ports::Desktop {
public:
    static constexpr int kMaxWindows = 16;
    static constexpr int kMaxPopups = 4;  // a menu and its open submenus
    static constexpr int kMaxIcons = 24; // in columns, top to bottom, then left to right
    static constexpr int kTaskbarHeight = 28;
    static constexpr int kOutline = 3;        // width of the outline while dragging
    static constexpr uint32_t kDoubleClickUs = 500000;
    static constexpr uint32_t kBlinkUs = 530000;
    static constexpr uint32_t kRepeatDelayUs = 500000, kRepeatUs = 33000;
    static constexpr uint32_t kSubmenuDelayUs = 300000;
    static constexpr int kMaxTimers = 16;
    static constexpr int kSaverWakeMove = 4; // pointer movement (pixels) that ends the screensaver
    using TimerFn = void (*)(void *context);

    Wm(ports::Display &display, ports::Pointer &pointer, ports::Clock &clock);

    // Sets the pointer up and paints the whole screen.
    void start();

    // The process table, and the process that owns the desktop itself (the
    // Start menu, the icons, the taskbar and the manager's own timers). With
    // one set, every window and timer records the process that asked for it
    // (the current one), the manager makes a window's owner the current
    // process whenever it calls into it, and a window program is removed
    // from the table once its last window has closed (at the end of the
    // event or tick, so a dialog can hand over to the next window first).
    // Timers such a process left running are stopped then, and windows made
    // for one showing (Window::set_dispose) are destroyed, which is where a
    // closed program's memory goes back.
    //
    // The same safe point is where signals take effect (proc::Table::signal
    // leaves them waiting on the row). TERM asks each of the process's
    // windows to close, as its close button would: the program may put a
    // question up, or stay. A window held by a dialog is not asked; its
    // dialog comes forward. KILL asks nothing: every window of the process
    // is closed, its menus and timers go, and its row is removed. Processes
    // with the respawn flag are started again once the dead are cleared away.
    void set_processes(proc::Table *table, proc::Pid desktop)
    {
        procs_ = table;
        desktop_pid_ = desktop;
    }
    proc::Table *processes() const { return procs_; }
    // The desktop shell (backlog item 21): the process that owns the taskbar,
    // the Start menu, the quick-launch icons, the tray and the desktop
    // icons. What it gave (set_start_menu, set_desktop_icons...) is called
    // as that process. shell_ended(): it has gone and taken those back; the
    // taskbar is not drawn and takes no clicks until a shell is set again.
    // Windows stay as they are, and so does the work area: a window does not
    // move because the bar under it left for a moment. Without either call
    // the taskbar is the desktop process's, as before.
    void set_shell(proc::Pid pid);
    void shell_ended();
    proc::Pid shell() const { return shell_pid_; }
    bool taskbar_shown() const { return !shell_gone_; }
    // Ends a process as KILL does: its windows, menus and timers go and its
    // row is removed. For a stop function (proc::Table::set_stop) of a
    // process that may have windows.
    void end(proc::Pid pid) { end_process(pid); }
    // How many windows and timers a process owns, and its window that was
    // opened first with a taskbar button (nullptr: none).
    int windows_of(proc::Pid pid) const;
    int timers_of(proc::Pid pid) const;
    Window *main_window_of(proc::Pid pid) const;
    // The owner of a timer, 0 if the id is not running.
    proc::Pid timer_owner(int id) const;

    // Window list. add() puts the window on top and gives it the focus.
    void add(Window &w);
    void close(Window &w);
    // For a window made for one showing (Window::set_dispose) that was never
    // opened: destroyed at the next safe point, as a closed one is.
    void discard(Window &w);
    void activate(Window &w);
    void minimize(Window &w);
    void restore(Window &w);
    void toggle_maximize(Window &w);
    void move(Window &w, int x, int y);
    void resize(Window &w, const Rect &frame);
    // Brings the window at the bottom of the stack to the front (Alt+Tab).
    void cycle_windows();
    // Windows with a taskbar button (dialogs that belong to a window have
    // none), in the order they were opened.
    int taskbar_count() const;
    Window *taskbar_window(int index) const;

    // Ctrl+Alt+Del: the shell's handler (the task manager). Not called while
    // a dialog holds the whole desktop.
    using AttentionFn = void (*)(void *context);
    void set_attention_key(AttentionFn fn, void *context)
    {
        attention_ = fn;
        attention_context_ = context;
    }
    // Hands the whole screen to `paint` for good (the power-down screen):
    // windows, menus and the pointer go, and input is ignored except
    // Ctrl+Alt+Del, which still reaches the attention handler.
    using ScreenFn = void (*)(gfx::Canvas &c, void *context);
    void take_over(ScreenFn paint, void *context);
    bool taken_over() const { return screen_fn_ != nullptr; }
    // Time the UI task has spent handling input, timers and painting since
    // start-up, in µs (wraps): the desktop's share of the processor.
    uint32_t busy_us() const { return busy_us_; }

    // A program is about to keep the desktop waiting (a save, a load): the
    // pointer becomes the hourglass until the matching end_wait(). Calls
    // nest. The pointer is mixed in as the picture goes to the monitor, so
    // the hourglass shows even though nothing is painted meanwhile. Use
    // wm::Wait rather than calling these.
    void begin_wait();
    void end_wait();
    bool waiting() const { return waiting_ > 0; }
    // For Window::set_locked(): a menu of the window goes, and the pointer
    // takes the shape that fits where it is now.
    void lock_changed(Window &w);

    // The desktop and the Start menu, supplied by the shell.
    void set_desktop_icons(const DesktopIcon *icons, int count);
    void set_start_menu(Menu *menu, const char *banner);

    // The desktop behind everything: a colour and, over it, an 8x8 pattern
    // drawn in `ink` (the top bit of a row is its left pixel; all rows 0:
    // the plain colour).
    struct Backdrop {
        gfx::Rgb colour;
        gfx::Rgb ink;
        uint8_t pattern[8];
    };
    void set_backdrop(const Backdrop &backdrop);
    const Backdrop &backdrop() const { return backdrop_; }
    // A right click on the desktop away from the icons calls `menu` with
    // the pointer's place (the shell opens a menu there).
    void set_desktop_menu(void (*menu)(int x, int y, void *context), void *context)
    {
        desktop_menu_ = menu;
        desktop_menu_context_ = context;
    }

    // A 16x16 icon in the tray, left of the clock (nullptr: none). A click
    // on it calls `click`.
    void set_tray_icon(const gfx::Bitmap *icon, void (*click)(void *context) = nullptr,
                       void *context = nullptr);
    Rect tray_icon_rect() const; // empty without an icon

    // Quick launch: small icons between the Start button and the window
    // buttons, each of which does one thing when clicked (the desktop sets
    // them to open a program). The array is copied; at most kMaxQuick, none
    // by default. `click` runs as the desktop's process.
    struct QuickLaunch {
        const gfx::Bitmap *icon; // 16x16
        void (*click)(void *context);
        void *context;
    };
    static constexpr int kMaxQuick = 4;
    void set_quick_launch(const QuickLaunch *items, int count);
    int quick_launch_count() const { return quick_count_; }
    Rect quick_launch_rect(int index) const;

    // Pop-up menus. open_menu() opens a free-standing one at (x, y).
    void open_menu(Menu &menu, int x, int y);
    void open_start_menu();
    void open_menu_bar(Window &w, int entry);
    void close_menus();
    bool menu_open() const { return popup_count_ > 0; }
    bool start_menu_open() const { return popup_count_ > 0 && start_open_; }
    int popup_count() const { return popup_count_; }
    Rect popup_rect(int level) const { return popups_[level].rect; }
    Rect popup_item(int level, int item) const;
    int popup_hover(int level) const { return popups_[level].hover; }

    // Timers on the UI task. tick() fires those that are due and paints;
    // the UI loop calls it after every event and whenever idle_us() runs
    // out. A period of 0 fires once. Returns an id for stop_timer(), 0 if
    // there is no room.
    int start_timer(uint32_t delay_us, uint32_t period_us, TimerFn fn, void *context);
    void stop_timer(int id);
    void tick();
    // How long tick() has nothing to do if no input comes, in µs: until the
    // next timer, the screensaver's next frame or its start. 0: call tick()
    // now. kIdleForever: nothing is waiting for the clock. The UI loop
    // sleeps this long between events.
    static constexpr uint32_t kIdleForever = 0xFFFFFFFF;
    uint32_t idle_us();
    uint64_t now_us(); // since start-up; does not wrap

    // The screensaver (nullptr: none) and the time without input after
    // which it starts by itself (0: never). start_screen_saver() shows it
    // now (a preview).
    void set_screen_saver(ScreenSaver *saver, uint32_t idle_seconds);
    void start_screen_saver();
    void stop_screen_saver();
    bool screen_saver_active() const { return saver_on_; }
    // The screensaver says which part of the screen it changed, for whoever
    // listens for painting (a remote screen).
    void screen_saver_drew(const Rect &r)
    {
        if (damage_fn_ != nullptr) {
            damage_fn_(r, damage_context_);
        }
    }

    // Caret blinking: widgets draw their caret only while this is true.
    bool caret_visible() const { return caret_on_ || !animations_; }
    void caret_moved(); // shows the caret and starts the blink again

    // The window that blocks `w` (a modal dialog), or nullptr.
    Window *blocker(const Window &w) const;

    // Told about every part of the screen as it is painted (a remote screen
    // sends those parts on).
    using DamageFn = void (*)(const Rect &r, void *context);
    void set_damage_listener(DamageFn fn, void *context)
    {
        damage_fn_ = fn;
        damage_context_ = context;
    }

    // Marks part of the screen for painting; update() paints it.
    void invalidate(const Rect &r);
    void update();

    // Input (ports::InputSink). Each event is handled and painted at once.
    void key(uint16_t usage, bool pressed) override;
    void mouse(const ports::MouseEvent &event) override;
    void text(const char *utf8) override;

    // Inspection (ports::Desktop), used by the debug protocol and tests.
    size_t describe_windows(char *out, size_t capacity) override;
    uint32_t repaint_all() override;
    void set_time(int hours, int minutes, int seconds) override;
    void get_time(int &hours, int &minutes, int &seconds) override;
    void set_animations(bool on) override;
    Rect clock_rect() const;

    int window_count() const { return count_; }
    Window *window_at(int z) const { return z < count_ ? stack_[z] : nullptr; } // 0 = bottom
    Window *active() const { return active_; }
    Window *window_under(int x, int y) const;
    Part part_at(const Window &w, int x, int y) const;
    uint8_t edges_at(const Window &w, int x, int y) const;
    Rect title_button(const Window &w, Part which) const;
    Rect menu_bar_entry(const Window &w, int entry) const;
    Rect work_area() const { return {0, 0, width_, height_ - kTaskbarHeight}; }
    Rect taskbar() const { return {0, height_ - kTaskbarHeight, width_, kTaskbarHeight}; }
    // Taskbar buttons: -1 the Start button, 0.. the window buttons.
    Rect taskbar_button(int index) const;
    Rect desktop_icon_rect(int index) const;
    int selected_icon() const { return selected_icon_; }
    int pointer_x() const { return pointer_x_; }
    int pointer_y() const { return pointer_y_; }
    uint8_t modifiers() const { return mods_; }
    bool dragging() const { return capture_ == Capture::Drag || capture_ == Capture::Resize; }

    // Drag and drop of a file: called by a window while the left button is
    // held (from its mouse handler); an outline follows the pointer until
    // the button is let go over a desktop icon with a drop function or a
    // window whose drag_over() takes it, or anywhere else (nothing happens).
    static constexpr int kDragPath = 160;
    void begin_drag(const char *path);
    bool item_dragging() const { return capture_ == Capture::Item; }
    // Files let go on the desktop away from the icons go to `drop`.
    void set_desktop_drop(void (*drop)(const char *path, void *context), void *context)
    {
        desktop_drop_ = drop;
        desktop_drop_context_ = context;
    }
    int drop_icon() const { return drop_icon_; }
    uint32_t last_update_us() const { return last_update_us_; }
    // Time to handle the last input event, painting included.
    uint32_t last_event_us() const { return last_event_us_; }

private:
    enum class Capture : uint8_t { None, Drag, Resize, TitleButton, Client, Taskbar, Menu, Swallow, Icon, Item };

    struct Popup {
        Menu *menu;
        Rect rect;
        int hover;   // item under the pointer or chosen by keys, -1 none
        bool start;  // the Start menu (banner, icons, taller items)
        proc::Pid owner; // the process its actions run for
    };

    struct Timer {
        TimerFn fn;
        void *context;
        uint64_t due;
        uint32_t period;
        int id;
        proc::Pid owner;
    };

    static void blink(void *self);
    static void clock_tick(void *self);
    static void key_repeat(void *self);
    static void submenu_due(void *self);
    static void switch_due(void *self);
    void handle_key(uint16_t usage, bool pressed);
    Window *system_modal() const;

    int stack_index(const Window &w) const;
    Window *topmost_visible() const;
    void raise(Window &w);
    void on_press();
    void on_right_press();
    void on_release();
    void on_move();
    void update_pointer_shape();
    bool double_click(int target);
    void taskbar_click(int index);
    int taskbar_index_at(int x, int y) const;
    void set_outline(const Rect &r, bool shown);
    void xor_outline();
    void set_active(Window *w);

    // Menus.
    void open_popup(Menu &menu, int x, int y, bool start, bool below_point);
    void close_popups_from(int level);
    void set_hover(int level, int item);
    int popup_at(int x, int y) const;
    int item_at(int level, int x, int y) const;
    void choose(int level, int item);
    bool menu_key(const KeyMsg &k);
    int menu_bar_entry_at(const Window &w, int x, int y) const;
    Rect popup_size(const Menu &menu, bool start) const;

    // Painting.
    void subtract_overlays(gfx::RectList &region, bool taskbar_too) const;
    void paint(bool painter);
    void paint_desktop(const gfx::RectList &clip);
    void paint_window(Window &w, const gfx::RectList &clip);
    void paint_frame(gfx::Canvas &c, Window &w);
    void paint_menu_bar(gfx::Canvas &c, Window &w);
    void paint_taskbar(const gfx::RectList &clip);
    void paint_popup(int level, const gfx::RectList &clip);
    bool visible(const Window &w) const { return w.state_ != WindowState::Minimized; }

    ports::Display &display_;
    ports::Pointer &pointer_;
    ports::Clock &clock_;
    int width_, height_;

    Window *stack_[kMaxWindows] = {}; // stacking order, 0 at the bottom
    Window *order_[kMaxWindows] = {}; // order of opening, for the taskbar
    int count_ = 0;
    int next_id_ = 1;
    Window *active_ = nullptr;

    const DesktopIcon *icons_ = nullptr;
    int icon_count_ = 0;
    int selected_icon_ = -1;

    QuickLaunch quick_[kMaxQuick] = {};
    int quick_count_ = 0;
    int buttons_x() const; // where the window buttons start
    const gfx::Bitmap *tray_icon_ = nullptr;
    void (*tray_click_)(void *context) = nullptr;
    void *tray_context_ = nullptr;

    Menu *start_menu_ = nullptr;
    const char *start_banner_ = "";
    Popup popups_[kMaxPopups] = {};
    int popup_count_ = 0;
    bool start_open_ = false;
    Window *menu_window_ = nullptr; // menu bar whose menu is open
    int menu_entry_ = -1;

    gfx::RectList dirty_;
    DamageFn damage_fn_ = nullptr;
    void *damage_context_ = nullptr;
    gfx::RectList region_;

    int pointer_x_, pointer_y_;
    uint8_t buttons_ = 0;
    uint8_t mods_ = 0;
    const video::PointerShape *shape_ = nullptr;
    int waiting_ = 0;

    Capture capture_ = Capture::None;
    Window *captured_ = nullptr;
    Part captured_part_ = Part::None;
    int captured_index_ = 0;   // taskbar button
    bool captured_inside_ = false;
    int grab_dx_ = 0, grab_dy_ = 0;
    uint8_t resize_edges_ = 0;
    Rect resize_from_;
    Rect outline_;
    bool outline_shown_ = false;

    // Drag and drop: the icon pressed (Capture::Icon), the file carried and
    // the target under the pointer (Capture::Item).
    void update_drop_target();
    void set_drop_icon(int icon);
    int pressed_icon_ = -1;
    char drag_path_[kDragPath] = "";
    int drop_icon_ = -1;
    Window *drop_window_ = nullptr;
    bool desktop_target_ = false;
    void (*desktop_drop_)(const char *path, void *context) = nullptr;
    Backdrop backdrop_ = {{58, 110, 165}, {0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}};
    void fill_backdrop(gfx::Canvas &c, const Rect &r) const;
    void (*desktop_menu_)(int x, int y, void *context) = nullptr;
    void *desktop_menu_context_ = nullptr;
    void *desktop_drop_context_ = nullptr;

    // Double clicks: the target of the last press and when it happened.
    int last_target_ = -1;
    uint32_t last_press_us_ = 0;
    int last_press_x_ = 0, last_press_y_ = 0;

    uint32_t last_update_us_ = 0;
    uint32_t last_event_us_ = 0;
    uint32_t busy_us_ = 0;
    AttentionFn attention_ = nullptr;
    void *attention_context_ = nullptr;
    ScreenFn screen_fn_ = nullptr;
    void *screen_context_ = nullptr;
    void event_done(uint32_t start);

    // Processes.
    proc::Table *procs_ = nullptr;
    proc::Pid desktop_pid_ = 0;
    proc::Pid shell_pid_ = 0;
    bool shell_gone_ = false;
    proc::Pid shell_or_desktop() const { return shell_pid_ != 0 ? shell_pid_ : desktop_pid_; }
    proc::Pid current_pid() const { return procs_ != nullptr ? procs_->current() : 0; }
    int start_timer_for(proc::Pid owner, uint32_t delay_us, uint32_t period_us, TimerFn fn, void *context);
    void reap();
    bool deliver_signals();
    void ask_to_close(proc::Pid pid);
    void end_process(proc::Pid pid);
    // Windows that live only while open (Window::set_dispose), closed and
    // waiting for the safe point.
    Window *closed_ = nullptr;
    void keep(Window &w);
    void dispose_closed();

    Timer timers_[kMaxTimers] = {};
    int next_timer_id_ = 1;
    bool clock_started_ = false;
    uint32_t last_raw_us_ = 0;
    uint64_t now64_ = 0;
    bool caret_on_ = true;
    bool animations_ = true;
    int blink_timer_ = 0;
    int64_t time_base_s_ = 0;  // time of day (s) at time_base_us_
    uint64_t time_base_us_ = 0;
    int shown_minute_ = -1;
    uint16_t repeat_usage_ = 0;
    int repeat_timer_ = 0;
    int submenu_timer_ = 0;
    int switch_timer_ = 0; // another item taking over from an open submenu
    int switch_level_ = 0, switch_item_ = 0;
    int submenu_level_ = -1, submenu_item_ = -1;

    ScreenSaver *saver_ = nullptr;
    uint64_t saver_idle_us_ = 0;
    uint64_t last_input_us_ = 0;
    bool saver_on_ = false;
    int saver_x_ = 0, saver_y_ = 0;  // the pointer when the saver started
    bool swallow_button_ = false;    // the press that woke the screen, until let go
};

// The hourglass for as long as this object lives:
//     wm::Wait wait(manager());
//     ... the slow work ...
class Wait {
public:
    explicit Wait(Wm &wm) : wm_(wm) { wm_.begin_wait(); }
    ~Wait() { wm_.end_wait(); }
    Wait(const Wait &) = delete;
    Wait &operator=(const Wait &) = delete;

private:
    Wm &wm_;
};

} // namespace wm
