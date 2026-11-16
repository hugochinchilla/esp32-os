// The Task Manager (Ctrl+Alt+Del): the programs that are open, with End
// Task and Switch To, and what the processor and the memory are doing.
// Every figure is read from the machine: the desktop's share of the
// processor is the time the UI task spent working (Wm::busy_us), the video
// share is the time spent filling video lines (SystemStats::scanout_us), and
// memory comes from the System port.
#include <cstdio>
#include <cstring>

#include "core/gfx/art.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"
#include "meter.hpp"

namespace shell {

namespace {

constexpr gfx::Colour kNavy = gfx::rgb(0, 0, 2);

// The last minute of processor use, one column per second, newest right.
class History : public wm::Widget {
public:
    static constexpr int kSamples = 60, kColumn = 2;

    History(int x, int y, int h) : Widget({x, y, kSamples * kColumn + 4, h}) {}

    void push(int percent)
    {
        memmove(values_, values_ + 1, kSamples - 1);
        values_[kSamples - 1] = static_cast<uint8_t>(percent < 0 ? 0 : (percent > 100 ? 100 : percent));
        invalidate();
    }
    int latest() const { return values_[kSamples - 1]; }

    void paint(gfx::Canvas &c) override
    {
        c.sunken({0, 0, rect.w, rect.h});
        const int h = rect.h - 4;
        for (int i = 0; i < kSamples; i++) {
            int bar = (values_[i] * h + 50) / 100;
            bar = bar == 0 && values_[i] != 0 ? 1 : bar;
            c.fill({2 + i * kColumn, 2, kColumn, h - bar}, gfx::kWhite);
            c.fill({2 + i * kColumn, 2 + h - bar, kColumn, bar}, kNavy);
        }
    }

private:
    uint8_t values_[kSamples] = {};
};

class TaskManager : public wm::Window {
public:
    static constexpr int kClientW = 336, kClientH = 300;
    static constexpr uint32_t kRefreshUs = 1000000;

    TaskManager()
        : Window("Task Manager", {0, 0, kClientW + 2 * kBorder, kClientH + kClientTop + kBorder},
                 &gfx::settings_icon_16()),
          heading_({8, 6, 200, 14}, "&Programs running:"),
          list_({8, 22, 320, 94}, titles_, 0, nullptr, this),
          end_({8, 122, 75, 23}, "&End Task", &TaskManager::end_pressed, this),
          switch_({89, 122, 75, 23}, "&Switch To", &TaskManager::switch_pressed, this),
          shut_down_({kClientW - 8 - 96, 122, 96, 23}, "Shut &Down...", &TaskManager::shut_down_pressed, this),
          processor_({8, 152, 320, 74}, "Processor"),
          history_(18, 170, 46),
          desktop_({152, 170, 168, 14}, desktop_text_),
          video_({152, 186, 168, 14}, video_text_),
          uptime_({152, 202, 168, 14}, uptime_text_),
          memory_({8, 230, 320, 62}, "Memory"),
          internal_label_({18, 248, 54, 14}, "Internal"),
          internal_({76, 248, 100, 14}),
          internal_text_({184, 248, 138, 14}, internal_line_),
          external_label_({18, 268, 54, 14}, "External"),
          external_({76, 268, 100, 14}),
          external_text_({184, 268, 138, 14}, external_line_)
    {
        set_dialog(true); // fixed size, only the close button
        list_.set_icons(&TaskManager::icon_of);
        list_.set_on_activate(&TaskManager::activated);
        add(heading_);
        add(list_);
        add(end_);
        add(switch_);
        add(shut_down_);
        add(processor_);
        add(history_);
        add(desktop_);
        add(video_);
        add(uptime_);
        add(memory_);
        add(internal_label_);
        add(internal_);
        add(internal_text_);
        add(external_label_);
        add(external_);
        add(external_text_);
    }

    // Called when the window opens: fresh figures, then once a second.
    void opened()
    {
        have_baseline_ = false;
        refresh();
        if (timer_ == 0) {
            timer_ = shell::manager().start_timer(kRefreshUs, kRefreshUs, &TaskManager::tick, this);
        }
    }

    void closed() override
    {
        shell::manager().stop_timer(timer_);
        timer_ = 0;
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return Window::key(k);
    }

    // Ctrl+Alt+Del again while it is open: the list up to date and ready
    // for the arrow keys.
    void brought_forward()
    {
        refresh_programs();
        set_focus(&list_);
    }

    void refresh()
    {
        refresh_programs();
        refresh_figures();
    }

private:
    static void tick(void *context) { static_cast<TaskManager *>(context)->refresh(); }

    static const gfx::Bitmap *icon_of(int index, void *context)
    {
        return static_cast<TaskManager *>(context)->windows_[index]->icon();
    }

    wm::Window *chosen() const
    {
        const int at = list_.selected();
        return at >= 0 && at < count_ && windows_[at]->open() ? windows_[at] : nullptr;
    }

    static void activated(wm::ListBox &, void *context) { switch_pressed(static_cast<TaskManager *>(context)->switch_, context); }

    static void switch_pressed(wm::Button &, void *context)
    {
        if (wm::Window *w = static_cast<TaskManager *>(context)->chosen()) {
            shell::manager().activate(*w);
        }
    }

    // End Task closes the program at once, without its "save changes?"
    // question: it is the way out when a program will not close.
    static void end_pressed(wm::Button &, void *context)
    {
        auto *self = static_cast<TaskManager *>(context);
        wm::Window *w = self->chosen();
        if (w == nullptr) {
            return;
        }
        wm::Wm &m = shell::manager();
        // Its open dialogs (a message box, Save As) go first.
        for (int i = 0; i < wm::Wm::kMaxWindows; i++) {
            wm::Window *dialog = m.blocker(*w);
            if (dialog == nullptr || dialog == self) {
                break;
            }
            m.close(*dialog);
        }
        m.close(*w);
        self->refresh_programs();
        self->set_focus(&self->list_);
    }

    static void shut_down_pressed(wm::Button &, void *) { shut_down_dialog(); }

    // The list: every window with a taskbar button except this one. It is
    // only rebuilt when the windows changed, so the selection stays put.
    void refresh_programs()
    {
        wm::Wm &m = shell::manager();
        wm::Window *now[wm::Wm::kMaxWindows];
        int n = 0;
        uint32_t sum = 0; // of the titles' text, which can change in place
        for (int i = 0; i < m.taskbar_count(); i++) {
            wm::Window *w = m.taskbar_window(i);
            if (w == this) {
                continue;
            }
            now[n++] = w;
            for (const char *p = w->title(); *p != 0; p++) {
                sum = sum * 31 + static_cast<uint8_t>(*p);
            }
        }
        bool same = n == count_;
        for (int i = 0; i < n && same; i++) {
            same = now[i] == windows_[i] && now[i]->title() == titles_[i];
        }
        if (!same) {
            wm::Window *was = list_.selected() >= 0 && list_.selected() < count_ ? windows_[list_.selected()] : nullptr;
            int again = -1;
            for (int i = 0; i < n; i++) {
                windows_[i] = now[i];
                titles_[i] = now[i]->title();
                again = now[i] == was ? i : again;
            }
            count_ = n;
            list_.set_items(titles_, n);
            if (again >= 0) {
                list_.select(again);
            }
        } else if (sum != title_sum_) {
            list_.invalidate();
        }
        title_sum_ = sum;
    }

    void set_line(wm::Label &label, char *line, size_t capacity, const char *text)
    {
        if (strcmp(line, text) != 0) {
            snprintf(line, capacity, "%s", text);
            label.invalidate();
        }
    }

    void refresh_figures()
    {
        wm::Wm &m = shell::manager();
        ports::SystemStats stats = {};
        system_port().stats(stats);
        const uint64_t now = m.now_us();
        const uint32_t busy = m.busy_us();
        int desktop = 0, video = 0;
        if (have_baseline_ && now > last_now_) {
            const uint64_t span = now - last_now_;
            desktop = static_cast<int>(static_cast<uint64_t>(static_cast<uint32_t>(busy - last_busy_)) * 100 / span);
            video = static_cast<int>(
                static_cast<uint64_t>(static_cast<uint32_t>(stats.scanout_us - last_scanout_)) * 100 / span);
            desktop = desktop > 100 ? 100 : desktop;
            video = video > 100 ? 100 : video;
            history_.push(desktop + video);
        }
        have_baseline_ = true;
        last_now_ = now;
        last_busy_ = busy;
        last_scanout_ = stats.scanout_us;

        char text[40];
        snprintf(text, sizeof(text), "Desktop:   %3d %% of a core", desktop);
        set_line(desktop_, desktop_text_, sizeof(desktop_text_), text);
        snprintf(text, sizeof(text), "Video out: %3d %% of a core", video);
        set_line(video_, video_text_, sizeof(video_text_), text);
        const uint32_t up = static_cast<uint32_t>(now / 1000000);
        snprintf(text, sizeof(text), "Running for %u:%02u:%02u", static_cast<unsigned>(up / 3600),
                 static_cast<unsigned>(up / 60 % 60), static_cast<unsigned>(up % 60));
        set_line(uptime_, uptime_text_, sizeof(uptime_text_), text);

        // Memory: the totals are facts of the machine, read once.
        if (!have_totals_) {
            char unused[4];
            uint32_t value = 0;
            total_internal_ = system_port().fact(ports::Fact::InternalRam, value, unused, sizeof(unused)) ? value : 0;
            total_external_ = system_port().fact(ports::Fact::ExternalRam, value, unused, sizeof(unused)) ? value : 0;
            have_totals_ = true;
        }
        const uint32_t used_internal =
            total_internal_ > stats.free_internal_ram ? total_internal_ - stats.free_internal_ram : 0;
        const uint32_t used_external =
            total_external_ > stats.free_external_ram ? total_external_ - stats.free_external_ram : 0;
        internal_.set(used_internal, total_internal_);
        external_.set(used_external, total_external_);
        snprintf(text, sizeof(text), "%u of %u KB used", static_cast<unsigned>(used_internal / 1024),
                 static_cast<unsigned>(total_internal_ / 1024));
        set_line(internal_text_, internal_line_, sizeof(internal_line_), text);
        const unsigned used_tenths = static_cast<unsigned>((static_cast<uint64_t>(used_external) * 10 + (1 << 19)) >> 20);
        const unsigned total_tenths = static_cast<unsigned>((static_cast<uint64_t>(total_external_) * 10 + (1 << 19)) >> 20);
        snprintf(text, sizeof(text), "%u.%u of %u.%u MB used", used_tenths / 10, used_tenths % 10,
                 total_tenths / 10, total_tenths % 10);
        set_line(external_text_, external_line_, sizeof(external_line_), text);
    }

    wm::Window *windows_[wm::Wm::kMaxWindows] = {};
    const char *titles_[wm::Wm::kMaxWindows] = {};
    int count_ = 0;
    uint32_t title_sum_ = 0;
    int timer_ = 0;
    bool have_baseline_ = false, have_totals_ = false;
    uint64_t last_now_ = 0;
    uint32_t last_busy_ = 0, last_scanout_ = 0;
    uint32_t total_internal_ = 0, total_external_ = 0;
    char desktop_text_[40] = "", video_text_[40] = "", uptime_text_[40] = "";
    char internal_line_[40] = "", external_line_[40] = "";

    wm::Label heading_;
    wm::ListBox list_;
    wm::Button end_, switch_, shut_down_;
    wm::GroupFrame processor_;
    History history_;
    wm::Label desktop_, video_, uptime_;
    wm::GroupFrame memory_;
    wm::Label internal_label_;
    Meter internal_;
    wm::Label internal_text_;
    wm::Label external_label_;
    Meter external_;
    wm::Label external_text_;
};

TaskManager &task_manager()
{
    static TaskManager window;
    return window;
}

} // namespace

void open_task_manager()
{
    TaskManager &w = task_manager();
    const bool was_open = w.open();
    if (!was_open) {
        w.set_frame(centred(manager(), w.frame().w, w.frame().h));
    }
    show(w);
    if (!was_open && w.open()) {
        w.opened();
    }
    w.brought_forward();
}

} // namespace shell
