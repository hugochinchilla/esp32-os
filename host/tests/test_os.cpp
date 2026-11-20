// The OS calls (core/os/os.hpp): whose a block of memory is, the files, a
// process and a scope, and that the programs moved to them give back what
// they asked for.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <cstring>

#include "core/fs/memory_volume.hpp"
#include "core/app/app.hpp"
#include "core/os/os.hpp"
#include "core/proc/table.hpp"
#include "core/prog/prog.hpp"
#include "core/shell/shell.hpp"
#include "../../programs/test_hooks.hpp"
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
    shell::start_program("Culebra");
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
    for (int i = 0; i < shell::program_count(); i++) {
        const shell::Program &app = shell::program(i); // the system's own and the files alike
        wm::Window *before = desktop.active();
        shell::start_program(app.name);
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
    CHECK(os::blocks_out() == blocks + 28 + 11); // each its window and its controls: twenty-seven and ten
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
        wait(10000);
    }
    CHECK(os::blocks_out() == blocks);
    // Closing Display put back what the settings file says, not what was
    // set behind its back.
    CHECK(desktop.backdrop().pattern[0] != 0x55);

    CHECK(os::network() == nullptr); // this machine has no radio

    // Shut Down with nothing open: the screen is taken at the desktop's
    // next tick (what holds it up is in test_system). Last: there is no
    // way back from it.
    CHECK(os::shut_down() && !desktop.taken_over());
    clock_.now += 1000;
    desktop.tick();
    CHECK(desktop.taken_over());
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

    // The hourglass on demand (item 49): asked for by number, shown over
    // the client area only, and the program still gets its keys.
    const auto shape_is = [](const video::PointerShape &shape) {
        const video::PointerShape *now = pointer.state().shape;
        return now != nullptr && memcmp(now, &shape, sizeof(video::PointerShape)) == 0;
    };
    desktop.mouse({true, static_cast<int16_t>(client.x + 200), static_cast<int16_t>(client.y + 100), 0, 0});
    CHECK(!s.hourglass() && shape_is(video::arrow_pointer()));
    s.set_hourglass(true);
    CHECK(s.hourglass() && os::ui::hourglass(number) && !s.locked() && shape_is(video::busy_pointer()));
    desktop.mouse({true, static_cast<int16_t>(client.x + 200), static_cast<int16_t>(client.y - 10), 0, 0});
    CHECK(shape_is(video::arrow_pointer())); // the title bar
    desktop.mouse({true, static_cast<int16_t>(client.x + 200), static_cast<int16_t>(client.y + 100), 0, 0});
    CHECK(shape_is(video::busy_pointer()));
    s.set_focus(s.tick_);
    press(wm::key::Space);
    CHECK(!s.tick_.checked() && s.ticks == 2);
    s.set_hourglass(false);
    CHECK(!os::ui::hourglass(number) && shape_is(video::arrow_pointer()));
    // A number of no window does nothing and answers no.
    os::ui::set_hourglass(app::kNone, true);
    CHECK(!os::ui::hourglass(app::kNone));
    // Left on when the window closes: it goes with the window.
    s.set_hourglass(true);
    CHECK(shape_is(video::busy_pointer()));

    // Closed: the program's object is destroyed, everything goes back, the
    // process ends and the number is dead.
    s.close();
    CHECK(shape_is(video::arrow_pointer()) && !os::ui::hourglass(number));
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

// A program on a task of its own that stops answering (backlog item 26):
// the desktop goes on without it, its picture is grey, and it can be ended.
host::ThreadEvent sleepy_gate;
std::atomic<int> sleepy_hangs{0}, sleepy_woke{0}, sleepy_paints{0};
std::atomic<bool> sleepy_ended{false};
app::Number sleepy_number = app::kNone;
constexpr gfx::Colour kSleepyInk = gfx::rgb(7, 0, 0);

class Sleepy : public app::Window {
public:
    Sleepy() : picture_(picture({10, 10, 60, 40})) {}
    bool key(const wm::KeyMsg &k) override
    {
        if (!k.pressed || k.usage != wm::key::A + 7) { // H: hang
            return false;
        }
        sleepy_hangs++;
        sleepy_gate.wait(0xFFFFFFFF);
        if (sleepy_ended) {
            return true; // ended while it hung: nothing of it is left to touch
        }
        sleepy_woke++;
        picture_.redraw(); // an OS call, from a program that is late
        return true;
    }
    void paint_in(app::Control, app::Canvas &c) override
    {
        sleepy_paints++;
        c.fill({0, 0, 60, 40}, kSleepyInk);
    }

private:
    app::Picture picture_;
};

template <class F> bool soon(F &&f)
{
    for (int i = 0; i < 400 && !f(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        wait(10000);
    }
    return f();
}

// A program that is late before it has opened its first window (a large
// folder being listed, on the board) is still starting: it is not taken for
// one whose last window has closed and ended in the middle.
host::ThreadEvent slow_start_gate;
app::Number slow_start_number = app::kNone;

// The whole screen lent to one window (backlog item 81): a game that fills
// it. Keys reach it as they are, the mouse as movement, nothing is painted
// around it, and the screen comes back by the program's wish, by
// Ctrl+Alt+Del or when the window closes.
class Game : public app::Window {
public:
    bool key(const wm::KeyMsg &k) override
    {
        keys++;
        last = k;
        return true;
    }
    void mouse_moved(int dx, int dy, int buttons) override
    {
        moved_x += dx;
        moved_y += dy;
        held = buttons;
    }
    int keys = 0, moved_x = 0, moved_y = 0, held = 0;
    wm::KeyMsg last = {};
};

void test_lent_screen()
{
    const uint32_t blocks = os::blocks_out();
    const proc::Pid pid = os::spawn("Game");
    app::Number number = app::kNone;
    {
        const os::As as(pid);
        app::Look look;
        look.title = "Game";
        number = app::open<Game>(look);
    }
    wait(10000);
    auto &g = *static_cast<Game *>(os::ui::state(number));
    const auto corner = [] { return display.pixels()[3 * 640 + 3]; };
    const uint8_t desk = corner();
    CHECK(!g.has_screen() && desktop.screen_lent_to() == nullptr);

    os::ui::Screen screen;
    CHECK(g.take_screen(screen) && g.has_screen());
    CHECK(screen.pixels == display.pixels() && screen.width == 640 && screen.height == 480);
    CHECK(desktop.screen_lent_to() == desktop.active() && !pointer.state().visible);
    // The picture is the program's: what it writes stays, and nothing of
    // the desktop is painted over it, whoever asks.
    memset(screen.pixels, 0, 640 * 480);
    g.screen_changed({0, 0, 640, 480});
    desktop.invalidate({0, 0, 640, 480});
    wait(1100000); // the taskbar's clock would paint
    CHECK(corner() == 0 && display.pixels()[470 * 640 + 600] == 0);
    // Every key, down and up, the modifiers too, and none repeats.
    desktop.key(wm::key::LeftCtrl, true);
    CHECK(g.keys == 1 && g.last.usage == wm::key::LeftCtrl && g.last.pressed);
    desktop.key(wm::key::LeftCtrl, false);
    CHECK(g.keys == 2 && !g.last.pressed);
    desktop.key(wm::key::LeftAlt, true);
    desktop.key(wm::key::Tab, true); // no window switching
    desktop.key(wm::key::Tab, false);
    desktop.key(wm::key::LeftAlt, false);
    CHECK(g.keys == 6 && strcmp(desktop.active()->title(), "Game") == 0);
    desktop.key(wm::key::Up, true);
    wait(2000000);
    CHECK(g.keys == 7);
    desktop.key(wm::key::Up, false);
    // The mouse as movement, with its buttons; the pointer stays away.
    ports::MouseEvent move = {};
    move.x = 7;
    move.y = -3;
    move.buttons = 1;
    desktop.mouse(move);
    desktop.mouse(move);
    CHECK(g.moved_x == 14 && g.moved_y == -6 && g.held == 1 && !pointer.state().visible);
    move = {};
    desktop.mouse(move);
    // The screensaver does not start over a game nobody touches.
    os::set_screen_saver(0, 60);
    wait(61000000);
    CHECK(!desktop.screen_saver_active() && g.has_screen());
    os::set_screen_saver(-1, 0);

    // Given back: the desktop is painted again and the pointer is there.
    g.give_screen_back();
    wait(10000);
    CHECK(!g.has_screen() && desktop.screen_lent_to() == nullptr && pointer.state().visible && corner() == desk);
    desktop.key(wm::key::A, true);
    desktop.key(wm::key::A, false);

    // Ctrl+Alt+Del takes it back whatever the program does, and then does
    // what it always does.
    CHECK(g.take_screen(screen));
    memset(screen.pixels, 0, 640 * 480);
    const int keys = g.keys;
    desktop.key(wm::key::LeftCtrl, true);
    desktop.key(wm::key::LeftAlt, true);
    desktop.key(wm::key::Delete, true);
    desktop.key(wm::key::Delete, false);
    desktop.key(wm::key::LeftAlt, false);
    desktop.key(wm::key::LeftCtrl, false);
    wait(10000);
    CHECK(g.keys == keys + 2 && !g.has_screen() && pointer.state().visible && corner() == desk);
    while (desktop.active() != nullptr && strcmp(desktop.active()->title(), "Game") != 0) {
        desktop.close(*desktop.active()); // the Task Manager, where there is one
        wait(10000);
    }

    // The logo key leaves for the desktop: the screen comes back and the
    // window is put away on the taskbar, with no Start menu and no key for
    // the program. Its taskbar button brings the window back.
    {
        wm::Window *game = desktop.active();
        CHECK(game != nullptr && g.take_screen(screen));
        memset(screen.pixels, 0, 640 * 480);
        const int before = g.keys;
        desktop.key(wm::key::LeftGui, true);
        desktop.key(wm::key::LeftGui, false);
        wait(10000);
        CHECK(g.keys == before && !g.has_screen() && pointer.state().visible && corner() == desk);
        CHECK(!desktop.menu_open() && game->state() == wm::WindowState::Minimized && desktop.active() != game);
        desktop.restore(*game);
        wait(10000);
        CHECK(desktop.active() == game);
    }

    // A window that closes with the screen gives it back.
    CHECK(desktop.active() != nullptr && g.take_screen(screen) && g.has_screen());
    memset(screen.pixels, 0, 640 * 480);
    g.close();
    wait(10000);
    wait(10000);
    CHECK(desktop.screen_lent_to() == nullptr && pointer.state().visible && corner() == desk);
    CHECK(!os::ui::alive(number) && !os::alive(pid) && os::blocks_out() == blocks);
    // A window that is gone is lent nothing.
    CHECK(!os::ui::take_screen(number, screen) && screen.pixels == nullptr);
}

// A program may ask for the coarse picture with the screen: it gets it
// where the screen has one, and the screen as it is where it has not. The
// desktop keeps its size all the while and is painted again afterwards.
void test_lent_coarse_screen()
{
    const proc::Pid pid = os::spawn("Game");
    app::Number number = app::kNone;
    {
        const os::As as(pid);
        app::Look look;
        look.title = "Game";
        number = app::open<Game>(look);
    }
    wait(10000);
    auto &g = *static_cast<Game *>(os::ui::state(number));
    const uint8_t desk = display.pixels()[3 * 640 + 3];
    static int changes;
    changes = 0;
    desktop.set_mode_listener([](void *) { changes++; }, nullptr);

    os::ui::Screen screen;
    CHECK(g.take_screen(screen, 320) && screen.width == 640 && screen.height == 480); // this screen has none
    CHECK(!display.coarse() && changes == 0);
    g.give_screen_back();
    wait(10000);

    display.offer_coarse();
    CHECK(g.take_screen(screen, 320) && screen.pixels == display.pixels());
    CHECK(screen.width == 320 && screen.height == 240 && display.coarse() && changes == 1);
    CHECK(display.width() == 320 && desktop.screen_width() == 640 && desktop.screen_height() == 480);
    CHECK(screen.pixels[0] == 0 && screen.pixels[239 * 320 + 319] == 0); // it starts black
    memset(screen.pixels, 0xFF, 320 * 240);
    desktop.invalidate({0, 0, 640, 480});
    wait(20000);
    CHECK(screen.pixels[100 * 320 + 100] == 0xFF); // nothing of the desktop is painted over it
    // Given back: the screen is the desktop's again, at its own size.
    g.give_screen_back();
    wait(20000);
    CHECK(!g.has_screen() && !display.coarse() && display.width() == 640 && changes == 2);
    CHECK(display.pixels()[3 * 640 + 3] == desk);
    // Not asked for, not given.
    CHECK(g.take_screen(screen) && screen.width == 640 && !display.coarse());
    g.close();
    wait(20000);
    desktop.set_mode_listener(nullptr, nullptr);
}

void test_late_before_its_first_window()
{
    static host::ThreadTasks tasks;
    static host::ThreadEvent events[2];
    static prog::Kit kit = {&events[0], &events[1]};
    prog::Runner &runner = prog::runner();
    runner.set_tasks(&tasks, &kit, 1);
    runner.set_limit(50);
    const uint32_t blocks = os::blocks_out();

    const proc::Pid pid = os::spawn("Slow start");
    CHECK(runner.start(pid));
    const bool in_time = proc::table().call(
        pid,
        [](void *) {
            slow_start_gate.wait(0xFFFFFFFF); // busy with something of its own, in no OS call
            app::Look look;
            look.title = "Slow start";
            look.width = 200;
            look.height = 120;
            slow_start_number = app::open<Sample>(look);
        },
        nullptr);
    CHECK(!in_time && !os::responding(pid));
    for (int i = 0; i < 5; i++) {
        wait(10000); // safe points come and go
    }
    CHECK(os::alive(pid) && runner.has_task(pid) && tasks.ended == 0);
    slow_start_gate.set();
    CHECK(soon([&] { return os::responding(pid) && slow_start_number != app::kNone; }));
    CHECK(desktop.active() != nullptr && desktop.active()->owner() == pid && os::alive(pid));
    desktop.close(*desktop.active());
    wait(10000);
    wait(10000);
    CHECK(!os::alive(pid) && os::blocks_out() == blocks);
    CHECK(soon([&] { return runner.tasks() == 0; }));
    runner.set_limit(prog::kForever);
    runner.set_tasks(nullptr, nullptr, 0);
}

void test_not_responding()
{
    static host::ThreadTasks tasks;
    static host::ThreadEvent events[2];
    static prog::Kit kit = {&events[0], &events[1]};
    prog::Runner &runner = prog::runner();
    runner.set_tasks(&tasks, &kit, 1);
    runner.set_limit(50);
    const uint32_t blocks = os::blocks_out();

    const proc::Pid pid = os::spawn("Sleepy");
    CHECK(runner.start(pid));
    proc::table().call(
        pid,
        [](void *) {
            app::Look look;
            look.title = "Sleepy";
            look.width = 200;
            look.height = 120;
            sleepy_number = app::open<Sleepy>(look);
        },
        nullptr);
    wait(10000);
    CHECK(sleepy_number != app::kNone && desktop.active() != nullptr && desktop.active()->owner() == pid);
    const gfx::Rect client = desktop.active()->client();
    const auto ink = [&] { return display.pixels()[(client.y + 30) * 640 + client.x + 40]; };
    CHECK(ink() == kSleepyInk && sleepy_paints > 0 && os::responding(pid));

    // It hangs on a key. The key comes back unanswered after the limit and
    // the program is marked; its picture goes grey; the desktop goes on.
    press(wm::key::A + 7);
    CHECK(sleepy_hangs == 1 && !os::responding(pid));
    wait(10000);
    CHECK(ink() == gfx::kGrey);
    // Nothing more reaches it, and nothing waits for it.
    const int paints = sleepy_paints;
    press(wm::key::A + 7);
    press(wm::key::Enter);
    desktop.repaint_all();
    wait(10000);
    CHECK(sleepy_hangs == 1 && sleepy_paints == paints && ink() == gfx::kGrey);
    CHECK(proc::table().current() == proc::table().find_by_name("desktop"));

    // It wakes up: its OS call is let in when the desktop has nothing else
    // under way, it is taken back, and it paints again.
    sleepy_gate.set();
    CHECK(soon([&] { return os::responding(pid); }));
    wait(10000);
    CHECK(sleepy_woke == 1 && ink() == kSleepyInk && sleepy_paints > paints);

    // It hangs again and its window is closed: the program is ended without
    // its say, and what it held comes back.
    press(wm::key::A + 7);
    CHECK(sleepy_hangs == 2 && !os::responding(pid));
    sleepy_ended = true;
    desktop.close(*desktop.active());
    wait(10000);
    CHECK(!os::alive(pid) && !os::ui::alive(sleepy_number));
    CHECK(os::blocks_out() == blocks);
    // (A thread of the PC cannot be taken away: it is let finish.)
    sleepy_gate.set();
    CHECK(soon([&] { return runner.tasks() == 0; }));
    runner.set_limit(prog::kForever);
    runner.set_tasks(nullptr, nullptr, 0);
}

// The programs that have tasks of their own (backlog item 26), with real
// threads: one program opening another, a question answered, files changing
// under a File manager window. Whose task each piece of code runs on is
// what is checked.
void test_programs_on_tasks()
{
    static host::ThreadTasks tasks;
    static host::ThreadEvent events[2 * prog::Runner::kMaxTasks];
    static prog::Kit kits[prog::Runner::kMaxTasks];
    for (int i = 0; i < prog::Runner::kMaxTasks; i++) {
        kits[i] = {&events[2 * i], &events[2 * i + 1]};
    }
    prog::Runner &runner = prog::runner();
    runner.set_tasks(&tasks, kits, prog::Runner::kMaxTasks);
    proc::Table &t = proc::table();
    const auto title = [] { return desktop.active() != nullptr ? desktop.active()->title() : ""; };

    storage.make_folder("C:\\Work");
    CHECK(storage.write_file("C:\\Work\\a.txt", "hello", 5) == ports::FsError::Ok);

    // A folder of many files opens like any other.
    storage.make_folder("C:\\Many");
    for (int i = 0; i < 81; i++) {
        char name[40];
        snprintf(name, sizeof(name), "C:\\Many\\file%02d.txt", i);
        CHECK(storage.write_file(name, "x", 1) == ports::FsError::Ok);
    }
    // Even when listing it takes longer than the system waits: the File
    // manager is late for a while, and the window comes when it is done.
    runner.set_limit(1);
    shell::open_folder("C:\\Many");
    runner.set_limit(prog::kForever);
    // Late, with no window yet: it is not taken for a program whose last
    // window has closed (on the board that ended it, task and all, in the
    // middle of the listing).
    const proc::Pid listing = t.find_by_name("File manager");
    wait(10000);
    CHECK(listing != proc::kNone && t.alive(listing) && runner.has_task(listing));
    CHECK(soon([&] { return desktop.active() != nullptr && strcmp(title(), "Many") == 0; }));
    CHECK(desktop.active() != nullptr && desktop.active()->owner() == listing);
    CHECK(os::responding(t.find_by_name("File manager")));
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
        wait(10000);
    }
    CHECK(soon([&] { return runner.tasks() == 0; }));

    // From the desktop: the File manager gets a process and a task, and its
    // window is made on that task.
    shell::open_folder("C:\\Work");
    const proc::Pid files = t.find_by_name("File manager");
    CHECK(files != proc::kNone && runner.has_task(files) && runner.tasks() == 1);
    CHECK(desktop.active() != nullptr && desktop.active()->owner() == files);
    CHECK(t.current() == t.find_by_name("desktop"));

    // The File manager opens a file: the Editor is another process with
    // another task, started from the File manager's.
    press(wm::key::Down);
    press(wm::key::Enter);
    wait(10000);
    const proc::Pid editor = t.find_by_name("Editor");
    CHECK(editor != proc::kNone && runner.has_task(editor) && runner.tasks() == 2);
    CHECK(strcmp(title(), "a.txt - Editor") == 0 && desktop.active()->owner() == editor);
    CHECK(t.find(editor) != nullptr && t.find(editor)->parent == files);

    // Delete in the File manager asks; the answer goes back to the File
    // manager, which moves the file and has every folder window read again.
    desktop.activate(*desktop.main_window_of(files));
    press(wm::key::Delete);
    wait(10000);
    CHECK(strcmp(title(), "Confirm File Delete") == 0);
    press(wm::key::Enter); // Yes
    wait(10000);
    ports::DirEntry e;
    CHECK(storage.stat("C:\\Work\\a.txt", e) != ports::FsError::Ok);
    CHECK(os::responding(files) && os::responding(editor));

    // The viewer and the Task Manager, each on its own.
    shell::open_picture(nullptr);
    shell::open_task_manager();
    wait(1100000); // the Task Manager's list fills on its timer
    CHECK(runner.tasks() == 4 && strcmp(title(), "Task Manager") == 0);
    // One program's menu opens another: the gallery's Help, About.
    shell::start_program("Widget gallery");
    const proc::Pid gallery = t.find_by_name("Widget gallery");
    CHECK(runner.has_task(gallery) && runner.tasks() == 5);
    desktop.key(wm::key::LeftAlt, true); // Alt+H: Help, and its one entry
    press(wm::key::A + 7);
    desktop.key(wm::key::LeftAlt, false);
    press(wm::key::Enter);
    wait(10000);
    const proc::Pid about = t.find_by_name("About Chinchilla");
    CHECK(strcmp(title(), "About Chinchilla") == 0 && runner.has_task(about) && runner.tasks() == 6);
    CHECK(about != proc::kNone && t.find(about)->parent == gallery);
    CHECK(runner.has_task(t.find_by_name("Picture viewer")) && runner.has_task(t.find_by_name("Task Manager")));

    // All closed: the processes end and every task is given back.
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
        wait(10000);
    }
    CHECK(soon([&] { return runner.tasks() == 0; }));
    CHECK(t.find_by_name("File manager") == proc::kNone && t.find_by_name("Editor") == proc::kNone);
    CHECK(tasks.started == tasks.ended);
    runner.set_tasks(nullptr, nullptr, 0);
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
    test_ui();
    test_lent_screen();
    test_lent_coarse_screen();
    test_programs_on_tasks();
    test_late_before_its_first_window();
    test_not_responding(); // before the last one, which ends with the screen taken by Shut Down
    test_desktop_calls();

    if (failures == 0) {
        printf("os: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
