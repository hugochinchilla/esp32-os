// The Task Manager (Ctrl+Alt+Del): the processes that are running, each
// with its number (PID), with End Task and Switch To, and what the processor
// and the memory are doing. End Task sends the TERM signal, which is the
// program's own close button, and offers KILL if the program is still there
// a few seconds later. It is greyed out on a protected process. The list is the process table (core/proc): the
// programs with windows by default, everything with "Show all processes".
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
    static constexpr int kClientW = 336, kClientH = 332;
    static constexpr int kMaxRows = proc::Table::kMaxProcesses, kRowText = 48;
    static constexpr uint32_t kRefreshUs = 1000000;
    // How long a program gets to close after End Task before force is offered.
    static constexpr uint32_t kGraceUs = 5000000;

    TaskManager()
        : Window("Task Manager", {0, 0, kClientW + 2 * kBorder, kClientH + kClientTop + kBorder},
                 &gfx::tasks_icon_16()),
          heading_({8, 6, 200, 14}, "&Processes running:"),
          columns_({30, 22, 290, 14}, "  PID  Name              Window"),
          list_({8, 36, 320, 94}, titles_, 0, &TaskManager::row_picked, this),
          end_({8, 136, 75, 23}, "&End Task", &TaskManager::end_pressed, this),
          switch_({89, 136, 75, 23}, "&Switch To", &TaskManager::switch_pressed, this),
          shut_down_({kClientW - 8 - 96, 136, 96, 23}, "Shut &Down...", &TaskManager::shut_down_pressed, this),
          all_({8, 164, 200, 18}, "Show &all processes", false, &TaskManager::all_changed, this),
          processor_({8, 184, 320, 74}, "Processor"),
          history_(18, 202, 46),
          desktop_({152, 202, 168, 14}, desktop_text_),
          video_({152, 218, 168, 14}, video_text_),
          uptime_({152, 234, 168, 14}, uptime_text_),
          memory_({8, 262, 320, 62}, "Memory"),
          internal_label_({18, 280, 54, 14}, "Internal"),
          internal_({76, 280, 100, 14}),
          internal_text_({184, 280, 138, 14}, internal_line_),
          external_label_({18, 300, 54, 14}, "External"),
          external_({76, 300, 100, 14}),
          external_text_({184, 300, 138, 14}, external_line_)
    {
        set_dialog(true); // fixed size, only the close button
        list_.set_icons(&TaskManager::icon_of);
        list_.set_on_activate(&TaskManager::activated);
        add(columns_);
        add(heading_); // just before the list: Alt+P goes to the widget after the label
        add(list_);
        add(end_);
        add(switch_);
        add(shut_down_);
        add(all_);
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
        ending_ = proc::kNone;
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
        check_ending();
    }

    void refresh()
    {
        refresh_programs();
        refresh_figures();
        check_ending();
    }

private:
    static void tick(void *context) { static_cast<TaskManager *>(context)->refresh(); }

    static const gfx::Bitmap *icon_of(int index, void *context)
    {
        auto *self = static_cast<TaskManager *>(context);
        const wm::Window *w = shell::manager().main_window_of(self->pids_[index]);
        return w != nullptr ? w->icon() : &gfx::service_icon_16();
    }

    // The PID of the row picked, if that process is still there.
    proc::Pid chosen() const
    {
        const int at = list_.selected();
        return at >= 0 && at < count_ && proc::table().alive(pids_[at]) ? pids_[at] : proc::kNone;
    }

    static void activated(wm::ListBox &, void *context) { switch_pressed(static_cast<TaskManager *>(context)->switch_, context); }

    static void all_changed(wm::CheckBox &, void *context) { static_cast<TaskManager *>(context)->refresh_programs(); }

    static void switch_pressed(wm::Button &, void *context)
    {
        wm::Wm &m = shell::manager();
        if (wm::Window *w = m.main_window_of(static_cast<TaskManager *>(context)->chosen())) {
            // Its dialog, if one holds it, comes forward with it.
            wm::Window *dialog = m.blocker(*w);
            m.activate(dialog != nullptr ? *dialog : *w);
        }
    }

    static void row_picked(wm::ListBox &, void *context) { static_cast<TaskManager *>(context)->update_buttons(); }

    // End Task is greyed out on a protected process: nobody may signal it.
    void update_buttons()
    {
        const proc::Process *p = proc::table().find(chosen());
        const bool allowed = p == nullptr || !p->is(proc::kProtected);
        if (!allowed && focus() == &end_) {
            set_focus(&list_);
        }
        end_.set_enabled(allowed);
    }

    // End Task asks first: it sends TERM, which is the program's own close
    // button, so a program with unsaved work puts its question up. The
    // signal takes effect when this click has been dealt with. If the
    // program is still there kGraceUs later, check_ending() offers force.
    // End Task again on the same program offers it at once.
    static void end_pressed(wm::Button &, void *context)
    {
        auto *self = static_cast<TaskManager *>(context);
        const proc::Pid pid = self->chosen();
        if (pid == proc::kNone) {
            return;
        }
        self->set_focus(&self->list_); // the arrow keys go on to the next row
        if (pid == self->ending_) {
            self->offer_force();
            return;
        }
        static char text[120];
        const char *name = proc::table().find(pid)->name;
        switch (proc::table().signal(pid, proc::Signal::Term)) {
        case proc::Sent::Ok:
            self->ending_ = pid;
            self->ending_since_ = shell::manager().now_us();
            self->look_again_soon();
            break;
        case proc::Sent::NotPermitted:
            snprintf(text, sizeof(text), "%s (PID %d) is protected.\nChinchilla needs it to keep running.", name, pid);
            message_box("Task Manager", text);
            break;
        case proc::Sent::NotSupported:
            snprintf(text, sizeof(text), "%s (PID %d) is a service without windows.\nServices cannot be ended yet.",
                     name, pid);
            message_box("Task Manager", text);
            break;
        case proc::Sent::NoSuchProcess:
            break;
        }
    }

    // The program End Task asked to close: gone by now, or still there after
    // its few seconds? The offer waits until this window is the one in
    // front. A program that answered with a question of its own has that in
    // front, and there is only one message box: ours would take its place
    // while the user is still reading it.
    void check_ending()
    {
        if (ending_ == proc::kNone) {
            return;
        }
        if (!proc::table().alive(ending_)) {
            ending_ = proc::kNone;
            return;
        }
        wm::Wm &m = shell::manager();
        if (m.now_us() - ending_since_ >= kGraceUs && m.active() == this) {
            offer_force();
        }
    }

    void offer_force()
    {
        const proc::Process *p = proc::table().find(ending_);
        forcing_ = ending_;
        ending_ = proc::kNone;
        if (p == nullptr) {
            return;
        }
        static const char *const buttons[] = {"&End Now", "Cancel"};
        char text[200];
        snprintf(text, sizeof(text),
                 "%s (PID %d) has not closed.\nIt may be waiting for an answer from you.\n\n"
                 "End it now? Changes that were not saved\nwill be lost.",
                 p->name, p->pid);
        ask("End Task", text, buttons, 2, &TaskManager::force_answer, this);
    }

    // End Now sends KILL: the program is not asked again, and the window
    // manager takes its windows and timers away.
    static void force_answer(int choice, void *context)
    {
        auto *self = static_cast<TaskManager *>(context);
        if (choice == 0) {
            proc::table().signal(self->forcing_, proc::Signal::Kill);
            self->look_again_soon();
        }
        self->forcing_ = proc::kNone;
    }

    // A signal takes effect after the click that sent it: the list is read
    // again at the next tick, not a second later.
    void look_again_soon() { shell::manager().start_timer(1, 0, &TaskManager::look_again, this); }
    static void look_again(void *context)
    {
        auto *self = static_cast<TaskManager *>(context);
        if (self->open()) {
            self->refresh_programs();
        }
    }

    static void shut_down_pressed(wm::Button &, void *) { shut_down_dialog(); }

    // The list: one row per process. By default the programs with windows,
    // except this one; with "Show all processes" every row of the table,
    // system processes and this one too. It is only rebuilt when the set of
    // processes changed, so the selection stays put.
    void refresh_programs()
    {
        wm::Wm &m = shell::manager();
        const proc::Table &table = proc::table();
        const bool all = all_.checked();
        proc::Pid now[kMaxRows];
        char text[kMaxRows][kRowText];
        int n = 0;
        for (int i = 0; i < table.count() && n < kMaxRows; i++) {
            const proc::Process &p = table.at(i);
            const wm::Window *w = m.main_window_of(p.pid);
            if (!all && (p.is(proc::kSystem) || w == nullptr || p.pid == owner())) {
                continue;
            }
            const char *what = w != nullptr ? w->title() : (p.is(proc::kSystem) ? "(system)" : "");
            now[n] = p.pid;
            snprintf(text[n], kRowText, "%5d  %-16.16s  %s", p.pid, p.name, what);
            n++;
        }
        bool same = n == count_;
        bool changed = false;
        for (int i = 0; i < n; i++) {
            same = same && now[i] == pids_[i];
            changed = changed || strcmp(text[i], rows_[i]) != 0;
        }
        const proc::Pid was = list_.selected() >= 0 && list_.selected() < count_ ? pids_[list_.selected()] : proc::kNone;
        for (int i = 0; i < n; i++) {
            pids_[i] = now[i];
            memcpy(rows_[i], text[i], kRowText);
            titles_[i] = rows_[i];
        }
        count_ = n;
        if (!same) {
            list_.set_items(titles_, n);
            for (int i = 0; i < n; i++) {
                if (now[i] == was) {
                    list_.select(i);
                }
            }
        } else if (changed) {
            list_.invalidate();
        }
        update_buttons();
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

    proc::Pid pids_[kMaxRows] = {};
    char rows_[kMaxRows][kRowText] = {};
    const char *titles_[kMaxRows] = {};
    int count_ = 0;
    int timer_ = 0;
    proc::Pid ending_ = proc::kNone;  // sent TERM, and watched until it goes
    uint64_t ending_since_ = 0;
    proc::Pid forcing_ = proc::kNone; // the one the "End Now?" question is about
    bool have_baseline_ = false, have_totals_ = false;
    uint64_t last_now_ = 0;
    uint32_t last_busy_ = 0, last_scanout_ = 0;
    uint32_t total_internal_ = 0, total_external_ = 0;
    char desktop_text_[40] = "", video_text_[40] = "", uptime_text_[40] = "";
    char internal_line_[40] = "", external_line_[40] = "";

    wm::Label heading_, columns_;
    wm::ListBox list_;
    wm::Button end_, switch_, shut_down_;
    wm::CheckBox all_;
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

} // namespace

void open_task_manager()
{
    const AsProgram as("Task Manager");
    TaskManager *window = one<TaskManager>("Task Manager");
    if (window == nullptr) {
        return;
    }
    TaskManager &w = *window;
    const bool was_open = w.open();
    if (!was_open) {
        w.set_frame(centred(manager(), w.frame().w, w.frame().h));
    }
    show(w);
    if (!was_open && w.open()) {
        w.opened();
    }
    w.brought_forward();
    discard(window); // if the window manager had no room for it
}

} // namespace shell
