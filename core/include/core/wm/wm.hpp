// The window server: overlapping windows in a stacking order, the background
// behind them, and pop-up menus (menu bars, the Start menu) on top of
// everything. It works out what is visible and asks each window to paint
// only that, routes the mouse and the keyboard, and draws frames and menus
// in the 98 look.
//
// What is on the desktop besides windows is not its business: the icons and
// the taskbar are panels (below), surfaces that the desktop shell supplies
// and draws (core/desk, backlog item 21). The server keeps a strip at the
// bottom of the screen free of windows for a bar, and knows nothing of what
// goes in it.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/gfx/canvas.hpp"
#include "core/gfx/rect.hpp"
#include "core/ports.hpp"
#include "core/proc/table.hpp"
#include "core/sys/frames.hpp"
#include "core/wm/window.hpp"

namespace wm {

// Parts of a window frame, for hit testing.
enum class Part : uint8_t { None, Border, Title, MenuBar, Client, Minimize, Maximize, Close };

// Edges grabbed when resizing, as bits.
enum Edge : uint8_t { kLeft = 1, kRight = 2, kTop = 4, kBottom = 8 };

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

// A panel: a part of the screen that is not a window and belongs to the
// desktop shell. There are two places for one. The desktop panel has the
// work area behind every window (the icons); the bar panel has the strip
// under the work area, in front of the windows (the taskbar). The server
// asks a panel to paint, hands it the mouse and files dragged over it, and
// tells it news it may want to show. All coordinates are the screen's.
// Every call is made as the process that set the panel.
enum class PanelPlace : uint8_t { Desktop, Bar };
enum class PanelNews : uint8_t {
    Windows, // a window opened, closed, was minimized or restored, or another one is in use
    Menu,    // the Start menu opened or closed
    Clock,   // the time of day shows another minute, or was set
    Screen,  // the screen has another size (Wm::set_video_mode): the work area and the bar moved
};

class Wm;

class Panel {
public:
    // Paints the parts of its place in `clip` (Wm::canvas() gives the
    // canvas). The desktop panel paints the background too, where it does
    // not paint something else (Wm::fill_backdrop), so each pixel is
    // painted once.
    virtual void paint(const gfx::RectList &clip) = 0;
    // The left button: Down in its place, then Move and Up wherever the
    // pointer goes until the button comes up.
    virtual void mouse(const MouseMsg & /*m*/) {}
    virtual void right_click(int /*x*/, int /*y*/) {}
    // A file is being dragged over it (the desktop panel only): true if
    // letting go here would do something. nullptr: the drag has left.
    virtual bool drag_over(const char * /*path*/, int /*x*/, int /*y*/) { return false; }
    virtual void dropped(const char * /*path*/, int /*x*/, int /*y*/) {}
    virtual void news(PanelNews /*what*/) {}
    // What is selected in it, for the debug protocol's window list (-1:
    // nothing).
    virtual int selected() const { return -1; }

protected:
    ~Panel() = default;
};

class Wm : public ports::InputSink, public ports::Desktop {
public:
    // No limit on the windows open at once (the owner's decision,
    // 2026-11-17: the machine may run out of memory instead). The lists
    // start with room for this many and grow out of the Memory port
    // (set_memory); with no port, or no memory left, the next window does
    // not open.
    static constexpr int kFirstWindows = 16;
    static constexpr int kMaxPopups = 4;  // a menu and its open submenus
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
    // Where the lists of windows grow from once more than kFirstWindows are
    // open. Not counted as any program's.
    void set_memory(ports::Memory *memory) { memory_ = memory; }

    // Sets the pointer up and paints the whole screen. With an arrival
    // other than AtOnce the desktop fades in instead (backlog item 74), the
    // way Shut Down fades it out: the screen shows the plain desktop (at
    // once, or what it shows now fades into it first), rests, and then
    // what the desktop has on it comes up in kFadeLevels steps. Until that
    // is over nothing else paints and input is ignored. The steps are
    // timers: they begin when tick() is first called.
    enum class Arrival : uint8_t { AtOnce, FromColour, FromPicture };
    void start(Arrival arrival = Arrival::AtOnce);
    bool arriving() const { return arrive_stage_ != Arriving::No; }
    static constexpr uint32_t kFadeStepUs = 60000; // one level of colour
    static constexpr int kFadeLevels = 7;          // red and green have eight levels
    static constexpr int kFadeRestSteps = 5;       // the plain desktop stays this many steps
    ~Wm();

    // The process table, and the process that owns the server itself (its
    // own timers, and the panels' calls when a panel names no owner). With
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
    proc::Pid desktop_pid() const { return desktop_pid_; }
    // The panels of the desktop shell. `owner` is the process the panel is
    // called as. nullptr takes the panel away: its place shows the
    // background and takes no clicks. Windows stay as they are, and so does
    // the work area: a window does not move because the bar under it left
    // for a moment.
    void set_panel(PanelPlace place, Panel *panel, proc::Pid owner = proc::kNone);
    Panel *panel(PanelPlace place) const { return panels_[place == PanelPlace::Bar ? 1 : 0].panel; }
    proc::Pid panel_owner(PanelPlace place) const { return panels_[place == PanelPlace::Bar ? 1 : 0].owner; }
    // For a panel's paint(): a canvas on the screen, in screen coordinates,
    // clipped to `clip` and to `bounds`; and the background.
    gfx::Canvas canvas(const gfx::RectList &clip, const Rect &bounds);
    void fill_backdrop(gfx::Canvas &c, const Rect &r) const;
    // Ends a process as KILL does: its windows, menus and timers go and its
    // row is removed. For a stop function (proc::Table::set_stop) of a
    // process that may have windows.
    void end(proc::Pid pid) { end_process(pid); }
    // How many windows and timers a process owns, and its window that was
    // opened first with a taskbar button (nullptr: none).
    int windows_of(proc::Pid pid) const;
    // Asks for every window of the process to be painted again.
    void invalidate_owned(proc::Pid pid);
    int timers_of(proc::Pid pid) const;
    Window *main_window_of(proc::Pid pid) const;
    // The open window with this number (Window::id(), given when it opens
    // and not given again); nullptr once it has closed.
    Window *window(int id) const;
    // A number for a window that is not open yet (Window::set_id()): the
    // same series as the ones given at opening.
    int new_id() { return next_id_++; }
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
    // The logo key with D: every window is minimised and no window has the
    // focus. Asked again while no window shows, it restores the windows it
    // put away, stacked as they were. Does nothing while a dialog holds the
    // whole desktop.
    void show_desktop();
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
    // Alt+F4 while no window has the focus: the shell's handler (the Shut
    // Down dialog). Not called while a dialog holds the whole desktop, and
    // not for a key that repeats.
    using LeaveFn = void (*)(void *context);
    void set_leave_key(LeaveFn fn, void *context)
    {
        leave_ = fn;
        leave_context_ = context;
    }
    // Hands the whole screen to `paint` for good (the power-down screen):
    // windows, menus and the pointer go, and input is ignored except
    // Ctrl+Alt+Del, which still reaches the attention handler.
    using ScreenFn = void (*)(gfx::Canvas &c, void *context);
    void take_over(ScreenFn paint, void *context);
    bool taken_over() const { return screen_fn_ != nullptr; }
    // Lends the whole screen to one window (a full-screen game; backlog
    // item 81): nothing is painted by the window manager, the pointer goes,
    // and every key, the modifiers too, goes to the window's key() as it
    // is pressed and let go; the mouse reaches it as movement
    // (Window::mouse_moved). Returns the picture, one RRRGGGBB byte per
    // pixel, width() by height(); nullptr if the screen is not free to
    // lend. The loan ends with return_screen(), when the window closes,
    // when the screen's mode changes, on Ctrl+Alt+Del, which then opens
    // what it always opens, and on the logo key, which minimises the
    // window: the window finds out with screen_lent_to().
    // `coarse`: the window would rather have a picture of 320x240 that the
    // screen makes large by itself (ports::Display::set_coarse), if this
    // screen has one; lent_width() and lent_height() say what it got. The
    // desktop keeps its own size meanwhile and is painted again afterwards.
    uint8_t *lend_screen(Window &to, bool coarse = false);
    void return_screen();
    Window *screen_lent_to() const { return lent_; }
    int screen_width() const { return width_; }
    int screen_height() const { return height_; }
    int lent_width() const { return display_.width(); }
    int lent_height() const { return display_.height(); }
    // The first part of the same, for a picture that fades away (Shut
    // Down): the screen is taken, and what it shows stays as it is until
    // fade_step() changes it or take_over() names who paints it.
    void hold_screen();
    // On a taken screen: every pixel moves one level of red and of green,
    // and one of blue if `blue`, towards what the plain desktop shows at
    // its place (`to_backdrop`) or towards the colour `to`. In place: there
    // is no second picture to mix from. Blue has half the levels, so a fade
    // of seven steps asks for it on the even ones. False: nothing changed,
    // the picture is there.
    bool fade_step(bool to_backdrop, gfx::Colour to, bool blue);
    // Time the UI task has spent handling input, timers and painting since
    // start-up, in µs (wraps): the desktop's share of the processor.
    uint32_t busy_us() const { return busy_us_; }

    // Whether the desktop keeps up with the monitor: every key, mouse
    // event, typed text and tick is measured against one video frame
    // (core/sys/frames.hpp). Whoever runs other work on the desktop's task
    // (the messages from other tasks) puts a Work around it.
    sys::Frames *frames() override { return &frames_; }
    class Work {
    public:
        Work(Wm &wm, sys::Frames::Work work);
        ~Work();
        Work(const Work &) = delete;
        Work &operator=(const Work &) = delete;

    private:
        Wm &wm_;
    };

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
    // For Window::set_hourglass(): the pointer takes the shape that fits
    // where it is now, without waiting for the mouse to move.
    void hourglass_changed() { update_pointer_shape(); }

    // The Start menu, supplied by the shell: what the Start key opens, at
    // the bottom left, with `banner` down its side. nullptr: none.
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
    // The look of everything (a row of gfx's table of themes); all of the
    // screen is painted again if it changes.
    void set_theme(int index);
    const Backdrop &backdrop() const { return backdrop_; }
    // Pop-up menus. open_menu() opens a free-standing one at (x, y).
    void open_menu(Menu &menu, int x, int y);
    // Called as the Start menu is about to open, so that whoever owns it
    // can bring it up to date (the programs found in a folder).
    void set_start_menu_opening(void (*opening)(void *context), void *context)
    {
        start_opening_ = opening;
        start_opening_context_ = context;
    }
    void open_start_menu();
    void open_menu_bar(Window &w, int entry);
    void close_menus();
    bool menu_open() const { return popup_count_ > 0; }
    bool start_menu_open() const { return popup_count_ > 0 && start_open_; }
    int popup_count() const { return popup_count_; }
    Rect popup_rect(int level) const { return popups_[level].rect; }
    const Menu *popup_menu(int level) const { return popups_[level].menu; }
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

    // The screen's modes (ports::Display; backlog item 68). set_video_mode()
    // asks for another one: it is changed at the next tick(), when no
    // program's code is running. Every window is then brought onto the new
    // screen (a maximised one fills it again), the panels are told
    // (PanelNews::Screen), the frame budget becomes the new mode's and
    // everything is painted. False: there is no such mode.
    int video_mode_count() const override { return display_.mode_count(); }
    ports::VideoMode video_mode_at(int index) const override { return display_.mode_at(index); }
    // The one asked for, from the moment it is asked.
    int video_mode() const override { return mode_wanted_ >= 0 ? mode_wanted_ : display_.mode(); }
    bool set_video_mode(int index) override;
    // Told after every change, for whoever keeps the screen's size (a
    // remote screen).
    using ModeFn = void (*)(void *context);
    void set_mode_listener(ModeFn fn, void *context)
    {
        mode_fn_ = fn;
        mode_context_ = context;
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
    size_t paint_bench(char *out, size_t capacity) override; // paint_bench.cpp
    void set_time(int hours, int minutes, int seconds) override;
    void get_time(int &hours, int &minutes, int &seconds) override;
    void set_animations(bool on) override;

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
    int pointer_x() const { return pointer_x_; }
    int pointer_y() const { return pointer_y_; }
    uint8_t modifiers() const { return mods_; }
    bool dragging() const { return capture_ == Capture::Drag || capture_ == Capture::Resize; }

    // Drag and drop of a file: called by a window while the left button is
    // held (from its mouse handler), or by a panel; an outline follows the
    // pointer until the button is let go over a window or the desktop panel
    // whose drag_over() takes it, or anywhere else (nothing happens).
    static constexpr int kDragPath = 160;
    // `desktop_only`: no window is offered it (a desktop icon that stands
    // for no file, on its way to another place on the desktop).
    void begin_drag(const char *path, bool desktop_only = false);
    // One more file carried by the drag just begun (several were picked):
    // the targets are asked about the first only, and when it is let go
    // each is dropped in turn, the first first. One marked `desktop_only`
    // is dropped only when the target is the desktop panel. False: there
    // was no memory for it, and it stays where it is.
    bool add_to_drag(const char *path, bool desktop_only = false);
    // While a target is being told of a drop: how many more files of the
    // same drag it will be told of after this one (it may wait for the
    // last before it repaints or complains).
    int drops_to_come() const { return drops_to_come_; }
    bool item_dragging() const { return capture_ == Capture::Item; }
    uint32_t last_update_us() const { return last_update_us_; }
    // Time to handle the last input event, painting included.
    uint32_t last_event_us() const { return last_event_us_; }

private:
    enum class Capture : uint8_t { None, Drag, Resize, TitleButton, Client, Panel, Menu, Swallow, Item };

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
    static bool heading_for(const Rect &r, int x0, int y0, int x1, int y1);
    void switch_to(int level, int item);
    void handle_key(uint16_t usage, bool pressed);
    Window *system_modal() const;

    int stack_index(const Window &w) const;
    Window *topmost_visible() const;
    void raise(Window &w);
    void on_press();
    void on_right_press();
    void on_wheel(int down, int right);
    void on_release();
    void on_move();
    void update_pointer_shape();
    bool double_click(int target);
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
    template <typename Fn> void paint_border(gfx::Canvas &c, const Rect &f, const gfx::RectList &parts, Fn paint);
    void paint_flat_title(gfx::Canvas &c, Window &w, const gfx::Rect &title, bool active);
    void paint_menu_bar(gfx::Canvas &c, Window &w);
    void paint_popup(int level, const gfx::RectList &clip);
    bool visible(const Window &w) const { return w.state_ != WindowState::Minimized; }

    ports::Display &display_;
    ports::Pointer &pointer_;
    ports::Clock &clock_;
    int width_, height_;
    int mode_wanted_ = -1; // set_video_mode() asked for it; -1: nothing asked
    ModeFn mode_fn_ = nullptr;
    void *mode_context_ = nullptr;
    void change_mode();

    bool room_for_one_more();
    Window *first_stack_[kFirstWindows] = {};
    Window *first_order_[kFirstWindows] = {};
    Window **stack_ = first_stack_; // stacking order, 0 at the bottom
    Window **order_ = first_order_; // order of opening, for the taskbar
    int room_ = kFirstWindows;      // how many the two lists hold
    ports::Memory *memory_ = nullptr;
    int count_ = 0;
    int next_id_ = 1;
    Window *active_ = nullptr;

    struct PanelSlot {
        Panel *panel = nullptr;
        proc::Pid owner = proc::kNone;
    };
    PanelSlot panels_[2];
    Panel *captured_panel_ = nullptr; // Capture::Panel
    proc::Pid captured_panel_owner_ = proc::kNone;
    void tell_panels(PanelNews what);

    Menu *start_menu_ = nullptr;
    void (*start_opening_)(void *context) = nullptr;
    void *start_opening_context_ = nullptr;
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
    bool captured_inside_ = false;
    int grab_dx_ = 0, grab_dy_ = 0;
    uint8_t resize_edges_ = 0;
    Rect resize_from_;
    Rect outline_;
    bool outline_shown_ = false;

    // Drag and drop: the file carried and the target under the pointer
    // (Capture::Item), a window or the desktop panel.
    void update_drop_target();
    void leave_desktop_target();
    char drag_path_[kDragPath] = "";
    bool drag_desktop_only_ = false;
    // The others, when several are dragged: for each a byte (1: for the
    // desktop only) and its path with its 0. The block is asked of the
    // Memory port when the second file joins a drag, grows by doubling, and
    // goes back when the drag ends: once per drag of several, not per event.
    void forget_drag_list();
    char *drag_more_ = nullptr;
    size_t drag_more_room_ = 0, drag_more_used_ = 0;
    int drag_more_count_ = 0;
    int drops_to_come_ = 0;
    Window *drop_window_ = nullptr;
    bool desktop_over_ = false;   // the desktop panel has been shown the drag
    bool desktop_target_ = false; // and it would take the file
    Backdrop backdrop_ = {{73, 109, 182}, {0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}};

    // Double clicks: the target of the last press and when it happened.
    int last_target_ = -1;
    uint32_t last_press_us_ = 0;
    int last_press_x_ = 0, last_press_y_ = 0;

    uint32_t last_update_us_ = 0;
    uint32_t last_event_us_ = 0;
    uint32_t busy_us_ = 0;
    sys::Frames frames_;
    AttentionFn attention_ = nullptr;
    void *attention_context_ = nullptr;
    LeaveFn leave_ = nullptr;
    void *leave_context_ = nullptr;
    // The logo key opens the Start menu when it comes up, if no other key
    // went down while it was held (logo+D is not the Start menu).
    bool logo_alone_ = false;
    bool repeating_ = false; // handle_key() is answering a held key
    ScreenFn screen_fn_ = nullptr;
    void *screen_context_ = nullptr;
    bool lent_coarse_ = false;       // and the screen shows the coarse picture for it
    Window *lent_ = nullptr;         // who has the whole screen on loan (lend_screen)
    int lent_x_ = 0, lent_y_ = 0;    // where a mouse that reports places was last
    // The desktop fading in (start()).
    enum class Arriving : uint8_t { No, Picture, Rest, Rising };
    Arriving arrive_stage_ = Arriving::No;
    int arrive_step_ = 0;
    int arrive_timer_ = 0;
    uint8_t *arrive_copy_ = nullptr; // the desktop's picture while it rises; the Memory port's
    static void arrive_tick(void *self);
    void arrive_step();
    void arrived();
    // Where the desktop, the windows and the menus are painted: the screen,
    // or the picture kept while the desktop rises.
    uint8_t *paint_pixels()
    {
        return arrive_stage_ == Arriving::Rest && arrive_copy_ != nullptr ? arrive_copy_ : display_.pixels();
    }
    void event_done(uint32_t start);

    // Processes.
    proc::Table *procs_ = nullptr;
    proc::Pid desktop_pid_ = 0;
    proc::Pid owner_of(PanelPlace place) const
    {
        const PanelSlot &slot = panels_[place == PanelPlace::Bar ? 1 : 0];
        return slot.owner != proc::kNone ? slot.owner : desktop_pid_;
    }
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
    static constexpr int kTrail = 4;
    struct TrailPoint {
        int16_t x, y;
    };
    TrailPoint trail_[kTrail] = {}; // the pointer's last positions, oldest first
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
