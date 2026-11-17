// The OS calls (core/os/os.hpp): whose a block of memory is, the files, a
// process and a scope, and that the programs moved to them give back what
// they asked for.
#include <cstdio>
#include <cstring>

#include "core/fs/memory_volume.hpp"
#include "core/app/app.hpp"
#include "core/os/os.hpp"
#include "core/proc/table.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
host::HostMemory memory;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);

void wait(uint32_t us)
{
    clock_.now += us;
    desktop.tick();
}

void test_memory()
{
    proc::Table &t = proc::table();
    const uint32_t blocks = os::blocks_out(), bytes = os::bytes_out();
    const proc::Pid a = os::spawn("a");
    CHECK(a != proc::kNone && t.find(a)->parent == os::self());
    void *mine = os::allocate(1000);
    void *its = nullptr;
    {
        const os::As as(a);
        CHECK(os::self() == a);
        its = os::allocate(300);
        // Zeroed, whatever was there before.
        bool zero = its != nullptr;
        for (int i = 0; zero && i < 300; i++) {
            zero = static_cast<uint8_t *>(its)[i] == 0;
        }
        CHECK(zero);
    }
    CHECK(os::self() != a);
    CHECK(mine != nullptr && its != nullptr);
    CHECK(t.find(a)->memory == 300);
    CHECK(os::blocks_out() == blocks + 2 && os::bytes_out() == bytes + 1300);
    // Given back by someone else: still taken off the owner's count.
    os::release(its);
    CHECK(t.find(a)->memory == 0);
    // The owner gone before its block: nothing breaks.
    void *late = nullptr;
    {
        const os::As as(a);
        late = os::memory().allocate(64);
    }
    t.exit(a);
    os::memory().release(late);
    os::release(mine);
    os::release(nullptr);
    CHECK(os::blocks_out() == blocks && os::bytes_out() == bytes);
    CHECK(os::find("a") == proc::kNone && os::find("desktop") != proc::kNone);
}

void test_files()
{
    using os::file::Error;
    CHECK(os::file::has_drive('C') && !os::file::has_drive('Q'));
    char letters[4];
    CHECK(os::file::drives(letters, 4) == 1 && letters[0] == 'C');
    fs::VolumeInfo info;
    CHECK(os::file::drive_info('C', info) == Error::Ok);

    CHECK(os::file::make_folder("C:\\os") == Error::Ok);
    CHECK(os::file::write_file("C:\\os\\a.txt", "hello", 5) == Error::Ok);
    fs::DirEntry e;
    CHECK(os::file::stat("C:\\os\\a.txt", e) == Error::Ok && e.size == 5 && !e.folder);
    CHECK(os::file::entry("C:\\os", 0, e) == Error::Ok && strcmp(e.name, "a.txt") == 0);
    CHECK(os::file::entry("C:\\os", 1, e) != Error::Ok);

    // An open file is a handle, and the system knows whose.
    using os::file::Handle;
    Handle f = os::file::kNoFile;
    char text[16] = {};
    size_t got = 0;
    CHECK(os::file::open_files() == 0);
    CHECK(os::file::open("C:\\os\\a.txt", ports::OpenMode::Read, f) == Error::Ok && f != os::file::kNoFile);
    CHECK(os::file::open_files(os::self()) == 1 && os::file::open_files() == 1);
    CHECK(os::file::read(f, text, sizeof(text), got) == Error::Ok && got == 5 && memcmp(text, "hello", 5) == 0);
    CHECK(os::file::close(f) == Error::Ok && os::file::open_files() == 0);
    // A handle that was closed is refused, also once its slot is in use
    // again; so are numbers that were never handles.
    CHECK(os::file::read(f, text, sizeof(text), got) == Error::Io && got == 0);
    CHECK(os::file::close(f) == Error::Io);
    Handle again = os::file::kNoFile;
    CHECK(os::file::open("C:\\os\\a.txt", ports::OpenMode::Read, again) == Error::Ok && again != f);
    CHECK(os::file::read(f, text, sizeof(text), got) == Error::Io);
    CHECK(os::file::close(again) == Error::Ok);
    CHECK(os::file::close(0) == Error::Io && os::file::close(-5) == Error::Io && os::file::close(77) == Error::Io);
    // A file that is not there takes no slot.
    CHECK(os::file::open("C:\\os\\none.txt", ports::OpenMode::Read, f) == Error::NotFound && f == os::file::kNoFile);
    CHECK(os::file::open_files() == 0);

    // A save given up leaves the old file; one kept replaces it.
    Handle save = os::file::kNoFile;
    CHECK(os::file::begin_save("C:\\os\\a.txt", save) == Error::Ok);
    CHECK(os::file::write(save, "bye", 3) == Error::Ok);
    CHECK(os::file::finish_save(save, false) == Error::Ok && os::file::open_files() == 0);
    CHECK(os::file::read_file("C:\\os\\a.txt", text, sizeof(text), got) == Error::Ok && got == 5);
    CHECK(os::file::begin_save("C:\\os\\a.txt", save) == Error::Ok);
    CHECK(os::file::write(save, "bye", 3) == Error::Ok);
    CHECK(os::file::finish_save(save) == Error::Ok);
    CHECK(os::file::read_file("C:\\os\\a.txt", text, sizeof(text), got) == Error::Ok && got == 3);
    CHECK(os::file::finish_save(save) == Error::Io); // it was closed by the first

    // One process may hold kMaxOpenEach, the machine kMaxOpen.
    {
        proc::Table &t = proc::table();
        const proc::Pid a = os::spawn("holder"), b = os::spawn("other"), c = os::spawn("third");
        const proc::Pid d = os::spawn("fourth");
        Handle held[os::file::kMaxOpen + 1] = {};
        int n = 0;
        for (const proc::Pid pid : {a, b, d}) {
            const os::As as(pid);
            for (int i = 0; i < os::file::kMaxOpenEach; i++) {
                CHECK(os::file::open("C:\\os\\a.txt", ports::OpenMode::Read, held[n++]) == Error::Ok);
            }
            Handle more;
            CHECK(os::file::open("C:\\os\\a.txt", ports::OpenMode::Read, more) == Error::TooManyOpen);
        }
        CHECK(os::file::open_files(a) == os::file::kMaxOpenEach && os::file::open_files() == os::file::kMaxOpen);
        {
            const os::As as(c);
            Handle more;
            CHECK(os::file::open("C:\\os\\a.txt", ports::OpenMode::Read, more) == Error::TooManyOpen);
        }
        // A process that ends: what it left open is closed for it, and its
        // handles are dead. The other's are as they were.
        CHECK(t.exit(a));
        CHECK(os::file::open_files(a) == 0 && os::file::open_files() == 2 * os::file::kMaxOpenEach);
        CHECK(os::file::read(held[0], text, sizeof(text), got) == Error::Io);
        CHECK(os::file::read(held[os::file::kMaxOpenEach], text, sizeof(text), got) == Error::Ok && got == 3);
        {
            // One that ends in the middle of a save: the new text is thrown
            // away, the old file stays, and no temporary file is left.
            const os::As as(c);
            CHECK(os::file::begin_save("C:\\os\\a.txt", save) == Error::Ok);
            CHECK(os::file::write(save, "half", 4) == Error::Ok);
        }
        CHECK(t.exit(c) && t.exit(b) && t.exit(d) && os::file::open_files() == 0);
        CHECK(os::file::read_file("C:\\os\\a.txt", text, sizeof(text), got) == Error::Ok && got == 3);
        fs::DirEntry only;
        CHECK(os::file::entry("C:\\os", 0, only) == Error::Ok && strcmp(only.name, "a.txt") == 0);
        CHECK(os::file::entry("C:\\os", 1, only) != Error::Ok);
    }

    CHECK(os::file::rename("C:\\os\\a.txt", "C:\\os\\b.txt") == Error::Ok);
    CHECK(os::file::remove("C:\\os") == Error::NotEmpty);
    CHECK(os::file::remove("C:\\os\\b.txt") == Error::Ok);
    CHECK(os::file::remove("C:\\os") == Error::Ok);

    // Settings: none, then what was saved.
    fs::Ini ini;
    CHECK(!os::load_settings("ostest", ini));
    ini.set("a", "b", "c");
    CHECK(os::save_settings("ostest", ini));
    fs::Ini back;
    char value[8] = {};
    CHECK(os::load_settings("ostest", back) && back.get("a", "b", value, sizeof(value)) && strcmp(value, "c") == 0);
    CHECK(os::file::stat("C:\\.config\\ostest.ini", e) == Error::Ok);
}

void test_windows_and_time()
{
    const gfx::Rect area = os::work_area();
    CHECK(area.w == 640 && area.h == 480 - wm::Wm::kTaskbarHeight);
    const gfx::Rect r = os::centred(200, 100);
    CHECK(r.x == 220 && r.w == 200 && r.h == 100 && r.y == (area.h - 100) / 2);
    CHECK(!os::menu_open());

    // A timer belongs to whoever asked, and goes with it.
    static int fired = 0;
    const proc::Pid p = os::spawn("ticker");
    int id = 0;
    {
        const os::As as(p);
        id = os::start_timer(1000, 1000, [](void *) { fired++; }, nullptr);
    }
    CHECK(id != 0);
    const uint64_t before = os::now_us();
    wait(1500);
    CHECK(fired == 1 && os::now_us() == before + 1500);
    os::stop_timer(id);
    wait(5000);
    CHECK(fired == 1);
    proc::table().exit(p);
}

// What the Task Manager asks: the list, a window's process, a signal.
void test_process_list()
{
    const int before = os::process_count();
    const proc::Pid p = os::spawn("listed");
    CHECK(os::process_count() == before + 1 && os::process_at(before).pid == p);
    CHECK(os::alive(p) && os::process(p) != nullptr && strcmp(os::process(p)->name, "listed") == 0);
    CHECK(os::main_window(p) == os::kNoWindow);
    os::refresh_figures();
    // The desktop is protected; one that is not there is not there; a
    // window program without windows is ended at the next safe point.
    CHECK(os::signal(os::find("desktop"), proc::Signal::Kill) == proc::Sent::NotPermitted);
    CHECK(os::signal(30000, proc::Signal::Term) == proc::Sent::NoSuchProcess);
    CHECK(os::signal(p, proc::Signal::Kill) == proc::Sent::Ok);
    wait(10000);
    wait(10000);
    CHECK(!os::alive(p) && os::process(p) == nullptr && os::process_count() == before);

    // A program with a window: the window is its main one, it is in front,
    // nothing blocks it, and Term closes it.
    shell::open_culebra();
    wait(10000);
    wm::Window *w = desktop.active();
    CHECK(w != nullptr);
    if (w != nullptr) {
        os::WindowId seen = os::kNoWindow;
        const proc::Pid owner = w->owner();
        CHECK(os::in_front(*w) && os::blocker(*w) == nullptr);
        // Seen from another program it is a number: good while it is open.
        const os::WindowId id = os::main_window(owner);
        CHECK(id != os::kNoWindow && id == w->id() && strcmp(os::window_title(id), w->title()) == 0);
        CHECK(os::window_icon(id) == w->icon() && os::bring_forward(id) && os::in_front(*w));
        CHECK(os::window_title(id + 1000)[0] == 0 && !os::bring_forward(id + 1000) && !os::bring_forward(os::kNoWindow));
        seen = id;
        CHECK(os::signal(owner, proc::Signal::Term) == proc::Sent::Ok);
        wait(10000);
        wait(10000);
        CHECK(!os::alive(owner));
        // Closed: the number is dead, and stays dead.
        CHECK(os::window_title(seen)[0] == 0 && os::window_icon(seen) == nullptr && !os::bring_forward(seen));
    }

    ports::SystemStats stats = {};
    os::stats(stats);
    uint32_t value = 0;
    char text[64] = "";
    os::fact(ports::Fact::Processor, value, text, sizeof(text));

    // The Picture viewer holds its file, as a handle, only while it decodes:
    // one while the picture is being read, none when it is on the screen,
    // and none when the viewer is ended half way.
    {
        uint8_t bmp[54 + 16] = {'B', 'M', 70, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24};
        bmp[34] = 16;
        CHECK(os::file::write_file("C:\\dot.bmp", bmp, sizeof(bmp)) == os::file::Error::Ok);
        CHECK(os::file::open_files() == 0);
        shell::open_picture("C:\\dot.bmp");
        const proc::Pid viewer = os::find("Picture viewer");
        CHECK(viewer != proc::kNone && os::file::open_files(viewer) == 1);
        shell::finish_picture();
        CHECK(os::file::open_files() == 0);
        shell::open_picture("C:\\dot.bmp"); // the same window starts again
        CHECK(os::file::open_files(viewer) == 1);
        CHECK(os::signal(viewer, proc::Signal::Kill) == proc::Sent::Ok);
        wait(10000);
        CHECK(!os::alive(viewer) && os::file::open_files() == 0);
        CHECK(os::file::remove("C:\\dot.bmp") == os::file::Error::Ok);
    }
}

// Every program opened and closed: the memory out comes back to where it
// was, so nothing a program asked the OS for outlives it.
void test_programs_give_back()
{
    const uint32_t blocks = os::blocks_out(), bytes = os::bytes_out();
    int opened = 0;
    for (int i = 0; i < shell::app_count(); i++) {
        const shell::App &app = shell::app(i);
        wm::Window *before = desktop.active();
        shell::launch(app);
        wait(10000);
        wm::Window *w = desktop.active();
        if (w == nullptr || w == before) {
            continue;
        }
        opened++;
        CHECK(os::blocks_out() > blocks);
        desktop.close(*w);
        // A program that asks before closing has a box open now: the last
        // button is "do not ask again", whatever it says.
        for (int tries = 0; tries < 4 && desktop.active() != before && desktop.active() != nullptr; tries++) {
            wait(10000);
            if (desktop.active() != nullptr && desktop.active() != before) {
                desktop.close(*desktop.active());
            }
        }
        wait(10000);
        if (os::blocks_out() != blocks) {
            printf("%s left %u blocks, %u bytes\n", app.name, static_cast<unsigned>(os::blocks_out() - blocks),
                   static_cast<unsigned>(os::bytes_out() - bytes));
        }
        CHECK(os::blocks_out() == blocks && os::bytes_out() == bytes);
    }
    CHECK(opened >= 5);
}

// What the settings tools and Shut Down ask: the background, the
// screensaver, the network, the way down.
void test_desktop_calls()
{
    wm::Wm::Backdrop b = desktop.backdrop();
    b.colour = {1, 2, 3};
    b.pattern[0] = 0x55;
    os::set_backdrop(b);
    CHECK(desktop.backdrop().colour.g == 2 && desktop.backdrop().pattern[0] == 0x55);

    // No screensaver set: a preview shows one, and a key ends it.
    os::set_screen_saver(-1, 60);
    os::preview_screen_saver(99); // there is no such animation
    CHECK(!desktop.screen_saver_active());
    os::preview_screen_saver(0);
    CHECK(desktop.screen_saver_active());
    for (int i = 0; i < 10; i++) {
        wait(40000);
    }
    desktop.key(wm::key::Escape, true);
    desktop.key(wm::key::Escape, false);
    wait(10000);
    CHECK(!desktop.screen_saver_active());
    CHECK(os::last_saver_run().frames > 0 && os::last_saver_run().shown_us > 0);
    // One set: it starts by itself after the wait, and not when taken away.
    os::set_screen_saver(1, 2);
    wait(2100000);
    CHECK(desktop.screen_saver_active());
    desktop.key(wm::key::Escape, true);
    desktop.key(wm::key::Escape, false);
    os::set_screen_saver(1, 0);
    wait(3000000);
    CHECK(!desktop.screen_saver_active());

    // The Display and Screen Saver tools keep nothing while they are closed.
    const uint32_t blocks = os::blocks_out();
    shell::display_dialog();
    shell::screen_saver_dialog();
    wait(10000);
    CHECK(os::blocks_out() == blocks + 13 + 11); // each its window and its controls: twelve and ten
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
        wait(10000);
    }
    CHECK(os::blocks_out() == blocks);
    // Closing Display put back what the settings file says, not what was
    // set behind its back.
    CHECK(desktop.backdrop().pattern[0] != 0x55);

    CHECK(os::network() == nullptr); // this machine has no radio

    // Shut Down with nothing open: the screen is taken (what stops it is in
    // test_system). Last: there is no way back from it.
    CHECK(os::shut_down() && desktop.taken_over());
}

} // namespace

// Windows and controls by number (core/os/ui.hpp), through the thin
// classes a program is written with (core/app/app.hpp).
int sample_made = 0, sample_gone = 0;
const char *const kRows[] = {"one", "two", "three"};

class Sample : public app::Window {
public:
    Sample()
        : tick_(check_box({10, 10, 100, 16}, "Tick")), a_(radio({10, 30, 60, 16}, "A", 0, 1)),
          b_(radio({80, 30, 60, 16}, "B", 0, 2)), field_(text_field({10, 50, 120, 20}, "start")),
          bar_(scroll_bar({10, 76, 120, 16}, false)), rows_(list({10, 96, 120, 50}, kRows, 3)),
          frame_(group({140, 10, 80, 60}, "Group")), area_(text_area({140, 76, 80, 40}, "some\ntext")),
          ok_(button({140, 124, 75, 23}, "OK"))
    {
        set_default(ok_);
        sample_made++;
    }
    ~Sample() override { sample_gone++; }
    using app::Window::radio_value;
    using app::Window::set_focus;
    using app::Window::set_radio_value;

    void command(app::Control which) override { commands += which == ok_; }
    void changed(app::Control which) override
    {
        ticks += which == tick_;
        moves += which == bar_;
        picks += which == rows_;
    }
    bool chosen(app::Control which) override
    {
        chosen_rows += which == rows_;
        return true;
    }
    // Draws by number: a square, a corner of it through a part, and as
    // many parts as the system gives.
    void paint(app::Canvas &c) override
    {
        paints++;
        c.fill({2, 152, 6, 6}, 5);
        c.sub({2, 152, 6, 6}).fill({0, 0, 2, 2}, 6);
        parts = 1;
        while (os::ui::draw::part(drawing_of(c), {0, 0, 4, 4}) != os::ui::kNoDrawing) {
            parts++;
        }
        kept = drawing_of(c);
    }
    static os::ui::Drawing drawing_of(const app::Canvas &c) { return c.number(); }

    app::CheckBox tick_;
    app::RadioButton a_, b_;
    app::TextField field_;
    app::ScrollBar bar_;
    app::ListBox rows_;
    app::GroupFrame frame_;
    app::TextArea area_;
    app::Button ok_;
    int commands = 0, ticks = 0, moves = 0, picks = 0, chosen_rows = 0, paints = 0, parts = 0;
    os::ui::Drawing kept = os::ui::kNoDrawing;
};

void press(uint8_t usage)
{
    desktop.key(usage, true);
    desktop.key(usage, false);
}

void test_ui()
{
    const uint32_t blocks = os::blocks_out();
    const proc::Pid pid = os::spawn("Sample");
    app::Number number = app::kNone;
    {
        const os::As as(pid);
        app::Look look;
        look.title = "Sample";
        look.width = 240;
        look.height = 190;
        look.flags = app::kPicture;
        number = app::open<Sample>(look);
    }
    CHECK(number != app::kNone && os::ui::alive(number) && sample_made == 1 && sample_gone == 0);
    // The window and its nine controls are the system's, in memory counted
    // as the program's; the program's object came with the window.
    CHECK(os::blocks_out() == blocks + 10 && os::process(pid)->memory > sizeof(Sample));
    CHECK(desktop.active() != nullptr && strcmp(desktop.active()->title(), "Sample") == 0);
    CHECK(desktop.active()->owner() == pid && os::main_window(pid) == desktop.active()->id());
    auto &s = *static_cast<Sample *>(os::ui::state(number));

    // The program painted by number, through a part too; the parts run out,
    // and a number kept past its event draws nothing.
    wait(10000);
    const gfx::Rect client = desktop.active()->client();
    const auto pixel = [&](int x, int y) { return display.pixels()[(client.y + y) * 640 + client.x + x]; };
    CHECK(s.paints > 0 && s.parts == os::ui::draw::kMaxParts);
    CHECK(pixel(2, 152) == 6 && pixel(3, 153) == 6 && pixel(4, 154) == 5 && pixel(7, 157) == 5 && pixel(8, 158) != 5);
    os::ui::draw::fill(s.kept, {2, 152, 6, 6}, 9);
    CHECK(pixel(4, 154) == 5 && os::ui::draw::part(s.kept, {0, 0, 4, 4}) == os::ui::kNoDrawing);
    CHECK(os::ui::draw::text(s.kept, 10, 0, "ab", 0, false) == 10 + os::ui::draw::text_width("ab", false));

    // What the controls hold, read and set by number.
    char text[32];
    s.field_.get(text, sizeof(text));
    CHECK(strcmp(text, "start") == 0);
    s.field_.set("other");
    s.field_.get(text, sizeof(text));
    CHECK(strcmp(text, "other") == 0);
    CHECK(!s.tick_.checked());
    s.set_radio_value(0, 2);
    CHECK(s.radio_value(0) == 2);
    s.bar_.set_range(50, 10);
    s.bar_.set_value(20);
    CHECK(s.bar_.value() == 20 && s.moves == 0);
    CHECK(s.rows_.selected() == -1);

    // What happens reaches the program as events, by the control's number.
    s.set_focus(s.tick_);
    press(wm::key::Space);
    CHECK(s.tick_.checked() && s.ticks == 1);
    s.set_focus(s.rows_);
    press(wm::key::Down);
    press(wm::key::Down);
    CHECK(s.rows_.selected() == 1 && s.picks == 2);
    press(wm::key::Enter);
    CHECK(s.chosen_rows == 1 && s.commands == 0);
    s.set_focus(s.field_);
    press(wm::key::Enter); // the default button
    CHECK(s.commands == 1);
    s.ok_.press();
    CHECK(s.commands == 2);

    // A number of another kind, or of no control, does nothing.
    os::ui::set_checked(number, s.ok_.number(), true);
    os::ui::select(number, 99, 1);
    CHECK(os::ui::value(number, s.rows_.number()) == 0 && !os::ui::checked(number, 0));

    // Closed: the program's object is destroyed, everything goes back, the
    // process ends and the number is dead.
    s.close();
    // Dead to the program at once, before the system has taken it apart.
    CHECK(sample_gone == 1 && !os::ui::alive(number) && !os::ui::show(number));
    wait(10000);
    wait(10000);
    CHECK(sample_gone == 1 && !os::ui::alive(number) && os::ui::state(number) == nullptr);
    CHECK(os::blocks_out() == blocks && !os::alive(pid));
    os::ui::close(number);
    CHECK(!os::ui::show(number) && os::ui::label(number, {0, 0, 1, 1}, "x") == os::ui::kNoControl);

    // A process ended with its window open: the same.
    const proc::Pid second = os::spawn("Sample");
    {
        const os::As as(second);
        app::Look look;
        look.title = "Sample";
        number = app::open<Sample>(look);
    }
    CHECK(sample_made == 2 && os::signal(second, proc::Signal::Kill) == proc::Sent::Ok);
    wait(10000);
    wait(10000);
    CHECK(sample_gone == 2 && !os::ui::alive(number) && os::blocks_out() == blocks && !os::alive(second));
}

int main()
{
    static fs::MemoryVolume disk(memory.allocate(1 << 18), 1 << 18, "RAM disk");
    storage.mount('C', disk);
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_animations(false);
    const proc::Scope as(&proc::table(), proc::table().find_by_name("desktop"));
    wait(10000);

    test_memory();
    test_files();
    test_windows_and_time();
    test_process_list();
    test_programs_give_back();
    test_ui(); // before the last one, which ends with the screen taken by Shut Down
    test_desktop_calls();

    if (failures == 0) {
        printf("os: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
