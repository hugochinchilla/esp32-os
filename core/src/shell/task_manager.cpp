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

#include "core/app/app.hpp"
#include "core/gfx/art.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

constexpr gfx::Colour kNavy = gfx::rgb(0, 0, 2);

class TaskManager : public app::Window {
public:
    static constexpr int kClientW = 404, kClientH = 332;
    static constexpr int kMaxRows = proc::Table::kMaxProcesses, kRowText = 64;
    static constexpr uint32_t kRefreshUs = 1000000;
    // How long a program gets to close after End Task before force is offered.
    static constexpr uint32_t kGraceUs = 5000000;
    // The last minute of processor use, one column per second, newest right.
    static constexpr int kSamples = 60, kColumn = 2;
    static constexpr gfx::Rect kHistory = {18, 202, kSamples * kColumn + 4, 46};

    TaskManager()
        : columns_(label({30, 22, kClientW - 46, 14}, "  PID  Name            Time    Mem  Window")),
          heading_(label({8, 6, 200, 14}, "&Processes running:")), // just before the list: Alt+P goes to the control after the label
          list_(list({8, 36, kClientW - 16, 94}, titles_, 0)),
          end_(button({8, 136, 75, 23}, "&End Task")),
          switch_(button({89, 136, 75, 23}, "&Switch To")),
          shut_down_(button({kClientW - 8 - 96, 136, 96, 23}, "Shut &Down...")),
          all_(check_box({8, 164, 200, 18}, "Show &all processes")),
          processor_(group({8, 184, kClientW - 16, 74}, "Processor")),
          history_(picture(kHistory)),
          desktop_(plain({152, 202, 168, 14})),
          video_(plain({152, 218, 168, 14})),
          uptime_(plain({152, 234, 168, 14})),
          memory_(group({8, 262, kClientW - 16, 62}, "Memory")),
          internal_label_(label({18, 280, 54, 14}, "Internal")),
          internal_(meter({76, 280, 100, 14})),
          internal_text_(plain({184, 280, 138, 14})),
          external_label_(label({18, 300, 54, 14}, "External")),
          external_(meter({76, 300, 100, 14})),
          external_text_(plain({184, 300, 138, 14}))
    {
        list_.set_items(titles_, 0, icons_);
        // Fresh figures, then once a second.
        refresh();
        timer_ = os::start_timer(kRefreshUs, kRefreshUs, &TaskManager::tick, this);
    }

    void closed() override
    {
        os::stop_timer(timer_);
        timer_ = 0;
        ending_ = proc::kNone;
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return false;
    }

    void command(app::Control which) override
    {
        if (which == end_) {
            end_pressed();
        } else if (which == switch_) {
            switch_to();
        } else if (which == shut_down_) {
            shut_down_dialog();
        }
    }
    void changed(app::Control which) override
    {
        if (which == all_) {
            refresh_programs();
        } else if (which == list_) {
            update_buttons();
        }
    }
    bool chosen(app::Control) override
    {
        switch_to();
        return true;
    }

    void paint_in(app::Control, app::Canvas &c) override
    {
        c.sunken({0, 0, kHistory.w, kHistory.h});
        const int h = kHistory.h - 4;
        for (int i = 0; i < kSamples; i++) {
            int bar = (values_[i] * h + 50) / 100;
            bar = bar == 0 && values_[i] != 0 ? 1 : bar;
            c.fill({2 + i * kColumn, 2, kColumn, h - bar}, gfx::kWhite);
            c.fill({2 + i * kColumn, 2 + h - bar, kColumn, bar}, kNavy);
        }
    }

    // Ctrl+Alt+Del again while it is open: the list up to date and ready
    // for the arrow keys.
    void brought_forward()
    {
        refresh_programs();
        set_focus(list_);
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

    void push(int percent)
    {
        memmove(values_, values_ + 1, kSamples - 1);
        values_[kSamples - 1] = static_cast<uint8_t>(percent < 0 ? 0 : (percent > 100 ? 100 : percent));
        history_.redraw();
    }

    // The PID of the row picked, if that process is still there.
    proc::Pid chosen() const
    {
        const int at = list_.selected();
        return at >= 0 && at < count_ && os::alive(pids_[at]) ? pids_[at] : proc::kNone;
    }

    // Its dialog, if one holds it, comes forward with it.
    void switch_to() { os::bring_forward(os::main_window(chosen())); }

    // End Task is greyed out on a protected process: nobody may signal it.
    void update_buttons()
    {
        const proc::Process *p = os::process(chosen());
        const bool allowed = p == nullptr || !p->is(proc::kProtected);
        if (!allowed && focus() == end_) {
            set_focus(list_);
        }
        end_.set_enabled(allowed);
    }

    // End Task asks first: it sends TERM, which is the program's own close
    // button, so a program with unsaved work puts its question up. The
    // signal takes effect when this click has been dealt with. If the
    // program is still there kGraceUs later, check_ending() offers force.
    // End Task again on the same program offers it at once.
    void end_pressed()
    {
        TaskManager *self = this;
        const proc::Pid pid = chosen();
        if (pid == proc::kNone) {
            return;
        }
        set_focus(list_); // the arrow keys go on to the next row
        if (pid == ending_) {
            offer_force();
            return;
        }
        static char text[120];
        const char *name = os::process(pid)->name;
        switch (os::signal(pid, proc::Signal::Term)) {
        case proc::Sent::Ok:
            self->ending_ = pid;
            self->ending_since_ = os::now_us();
            self->look_again_soon();
            break;
        case proc::Sent::NotPermitted:
            snprintf(text, sizeof(text), "%s (PID %d) is protected.\nChinchilla needs it to keep running.", name, pid);
            message_box("Task Manager", text);
            break;
        case proc::Sent::NotSupported:
            snprintf(text, sizeof(text), "%s (PID %d) is a service that cannot be ended\nwhile Chinchilla is running.",
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
        if (!os::alive(ending_)) {
            ending_ = proc::kNone;
            return;
        }
        if (os::now_us() - ending_since_ >= kGraceUs && in_front()) {
            offer_force();
        }
    }

    void offer_force()
    {
        const proc::Process *p = os::process(ending_);
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
            os::signal(self->forcing_, proc::Signal::Kill);
            self->look_again_soon();
        }
        self->forcing_ = proc::kNone;
    }

    // A signal takes effect after the click that sent it: the list is read
    // again at the next tick, not a second later.
    void look_again_soon() { os::start_timer(1, 0, &TaskManager::look_again, this); }
    static void look_again(void *context)
    {
        auto *self = static_cast<TaskManager *>(context);
        if (self->on_screen()) {
            self->refresh_programs();
        }
    }

    // The list: one row per process. By default the programs with windows,
    // except this one; with "Show all processes" every row of the table,
    // system processes and this one too. It is only rebuilt when the set of
    // processes changed, so the selection stays put.
    void refresh_programs()
    {
        os::refresh_figures(); // the time of jobs still running
        const bool all = all_.checked();
        proc::Pid now[kMaxRows];
        char text[kMaxRows][kRowText];
        int n = 0;
        for (int i = 0; i < os::process_count() && n < kMaxRows; i++) {
            const proc::Process &p = os::process_at(i);
            const os::WindowId w = os::main_window(p.pid);
            if (!all && (p.is(proc::kSystem) || w == os::kNoWindow || p.pid == os::self())) {
                continue;
            }
            const char *what = w != os::kNoWindow ? os::window_title(w) : (p.is(proc::kSystem) ? "(system)" : "");
            now[n] = p.pid;
            char time[16], held[16];
            snprintf(text[n], kRowText, "%5d  %-12.12s %7s %6s  %s", p.pid, p.name,
                     proc::time_text(p.cpu_us, time, sizeof(time)), proc::memory_text(p.memory, held, sizeof(held)),
                     what);
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
            const os::WindowId w = os::main_window(now[i]);
            icons_[i] = w != os::kNoWindow ? os::window_icon(w) : &gfx::service_icon_16();
        }
        count_ = n;
        if (!same) {
            list_.set_items(titles_, n, icons_);
            for (int i = 0; i < n; i++) {
                if (now[i] == was) {
                    list_.select(i);
                }
            }
        } else if (changed) {
            list_.redraw();
        }
        update_buttons();
    }

    void refresh_figures()
    {
        ports::SystemStats stats = {};
        os::stats(stats);
        const uint64_t now = os::now_us();
        const uint32_t busy = os::busy_us();
        int desktop = 0, video = 0;
        if (have_baseline_ && now > last_now_) {
            const uint64_t span = now - last_now_;
            desktop = static_cast<int>(static_cast<uint64_t>(static_cast<uint32_t>(busy - last_busy_)) * 100 / span);
            video = static_cast<int>(
                static_cast<uint64_t>(static_cast<uint32_t>(stats.scanout_us - last_scanout_)) * 100 / span);
            desktop = desktop > 100 ? 100 : desktop;
            video = video > 100 ? 100 : video;
            push(desktop + video);
        }
        have_baseline_ = true;
        last_now_ = now;
        last_busy_ = busy;
        last_scanout_ = stats.scanout_us;

        char text[40];
        snprintf(text, sizeof(text), "Desktop:   %3d %% of a core", desktop);
        desktop_.set(text);
        snprintf(text, sizeof(text), "Video out: %3d %% of a core", video);
        video_.set(text);
        const uint32_t up = static_cast<uint32_t>(now / 1000000);
        snprintf(text, sizeof(text), "Running for %u:%02u:%02u", static_cast<unsigned>(up / 3600),
                 static_cast<unsigned>(up / 60 % 60), static_cast<unsigned>(up % 60));
        uptime_.set(text);

        // Memory: the totals are facts of the machine, read once.
        if (!have_totals_) {
            char unused[4];
            uint32_t value = 0;
            total_internal_ = os::fact(ports::Fact::InternalRam, value, unused, sizeof(unused)) ? value : 0;
            total_external_ = os::fact(ports::Fact::ExternalRam, value, unused, sizeof(unused)) ? value : 0;
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
        internal_text_.set(text);
        const unsigned used_tenths = static_cast<unsigned>((static_cast<uint64_t>(used_external) * 10 + (1 << 19)) >> 20);
        const unsigned total_tenths = static_cast<unsigned>((static_cast<uint64_t>(total_external_) * 10 + (1 << 19)) >> 20);
        snprintf(text, sizeof(text), "%u.%u of %u.%u MB used", used_tenths / 10, used_tenths % 10,
                 total_tenths / 10, total_tenths % 10);
        external_text_.set(text);
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
    uint8_t values_[kSamples] = {};
    const gfx::Bitmap *icons_[kMaxRows] = {};

    app::Label columns_, heading_;
    app::ListBox list_;
    app::Button end_, switch_, shut_down_;
    app::CheckBox all_;
    app::GroupFrame processor_;
    app::Picture history_;
    app::Plain desktop_, video_, uptime_;
    app::GroupFrame memory_;
    app::Label internal_label_;
    app::Meter internal_;
    app::Plain internal_text_;
    app::Label external_label_;
    app::Meter external_;
    app::Plain external_text_;
};

app::Number g_window = app::kNone; // the one there may be: a number, dead once it has closed

} // namespace

void open_task_manager()
{
    const AsProgram as("Task Manager");
    if (!app::bring_forward(g_window)) {
        app::Look look;
        look.title = "Task Manager";
        look.width = TaskManager::kClientW;
        look.height = TaskManager::kClientH;
        look.icon = &gfx::tasks_icon_16();
        look.flags = app::kDialog | app::kClientSize; // fixed size, only the close button
        g_window = app::open<TaskManager>(look);
        if (g_window == app::kNone) {
            no_memory_for("Task Manager");
            return;
        }
    }
    // Ctrl+Alt+Del again while it is open does this too.
    if (TaskManager *w = app::program<TaskManager>(g_window)) {
        w->brought_forward();
    }
}

} // namespace shell
