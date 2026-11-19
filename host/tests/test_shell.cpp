// End-to-end tests of the programs on a RAM disk: Editor, file dialogs and
// File manager, driven with keys and checked on the disk.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/fs/memory_volume.hpp"
#include "core/gfx/art.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "core/desk/desk.hpp"
#include "core/job/job.hpp"
#include "core/msg/queue.hpp"
#include "../host_adapters.hpp"
#include "../thread_queue.hpp"

#include "core/os/os.hpp"
#include "core/os/ui.hpp"
#include "core/wm/text_edit.hpp"
#include "../../core/src/shell/internal.hpp" // the Trash icon's action, the file commands

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

// The RAM disk with a spy in front: it notes the pointer's shape at each
// write, and can fail writes as a full or broken disk would.
class SpyVolume : public ports::Volume {
public:
    void set(ports::Volume &v) { v_ = &v; }
    int writes = 0, writes_under_hourglass = 0;
    int fail_after = -1; // writes that still succeed; -1: all of them

    ports::FsError info(ports::VolumeInfo &o) override { return v_->info(o); }
    int listings = 0; // times a folder was walked, to find one entry or a run of them
    ports::FsError entry(const char *f, int i, ports::DirEntry &o) override
    {
        listings++;
        return v_->entry(f, i, o);
    }
    ports::FsError entries(const char *f, int first, ports::DirEntry *o, int capacity, int &count) override
    {
        listings++;
        return v_->entries(f, first, o, capacity, count);
    }
    ports::FsError stat(const char *p, ports::DirEntry &o) override { return v_->stat(p, o); }
    ports::FsError make_folder(const char *p) override { return v_->make_folder(p); }
    ports::FsError remove(const char *p) override { return v_->remove(p); }
    ports::FsError rename(const char *a, const char *b) override { return v_->rename(a, b); }
    ports::FsError replace(const char *a, const char *b) override { return v_->replace(a, b); }
    ports::FsError open(const char *p, ports::OpenMode m, int &f) override { return v_->open(p, m, f); }
    ports::FsError read(int f, void *d, size_t s, size_t &n) override { return v_->read(f, d, s, n); }
    ports::FsError write(int f, const void *d, size_t s) override
    {
        writes++;
        const video::PointerState p = pointer.state();
        if (p.shape != nullptr && memcmp(p.shape, &video::busy_pointer(), sizeof(video::PointerShape)) == 0) {
            writes_under_hourglass++;
        }
        if (fail_after >= 0 && fail_after-- == 0) {
            fail_after = 0;
            return ports::FsError::Io;
        }
        return v_->write(f, d, s);
    }
    ports::FsError seek(int f, uint32_t o) override { return v_->seek(f, o); }
    ports::FsError close(int f) override { return v_->close(f); }

private:
    ports::Volume *v_ = nullptr;
};
SpyVolume spy;

bool arrow_shown()
{
    const video::PointerState p = pointer.state();
    return p.shape != nullptr && memcmp(p.shape, &video::arrow_pointer(), sizeof(video::PointerShape)) == 0;
}

void key(uint16_t usage, uint8_t mods = 0)
{
    const uint16_t mod_keys[3] = {wm::key::LeftShift, wm::key::LeftCtrl, wm::key::LeftAlt};
    for (int i = 0; i < 3; i++) {
        if (mods & (1 << i)) {
            desktop.key(mod_keys[i], true);
        }
    }
    desktop.key(usage, true);
    desktop.key(usage, false);
    for (int i = 0; i < 3; i++) {
        if (mods & (1 << i)) {
            desktop.key(mod_keys[i], false);
        }
    }
    desktop.tick(); // as the UI loop does after every event: change notices settle there
    clock_.now += 100000;
}

void letter(char c, uint8_t mods = 0) { key(static_cast<uint16_t>(wm::key::A + (c - 'a')), mods); }

std::string file(const char *path)
{
    char buf[4096];
    size_t n = 0;
    if (storage.read_file(path, buf, sizeof(buf), n) != ports::FsError::Ok) {
        return "<missing>";
    }
    return std::string(buf, n);
}

bool exists(const char *path)
{
    ports::DirEntry e;
    return storage.stat(path, e) == ports::FsError::Ok;
}

const char *title() { return desktop.active() ? desktop.active()->title() : ""; }

bool no_stale_pixels()
{
    std::string before(reinterpret_cast<char *>(display.pixels()), 640 * 480);
    desktop.repaint_all();
    return memcmp(before.data(), display.pixels(), before.size()) == 0;
}

void test_editor()
{
    // Open, type, save: the file keeps its CRLF and gets Latin-1 bytes.
    storage.write_file("C:\\Documents\\a.txt", "one\r\ntwo\r\n", 10);
    shell::open_in_editor("C:\\Documents\\a.txt");
    CHECK(strcmp(title(), "a.txt - Editor") == 0);
    key(wm::key::End, 2); // Ctrl+End
    desktop.text("tres ñ");
    CHECK(strcmp(title(), "a.txt* - Editor") == 0);
    spy.writes = spy.writes_under_hourglass = 0;
    letter('f', 4); // Alt+F
    letter('s');    // Save
    CHECK(file("C:\\Documents\\a.txt") == "one\r\ntwo\r\ntres \xf1");
    CHECK(strcmp(title(), "a.txt - Editor") == 0);
    // The hourglass was up for every write, and the arrow is back.
    CHECK(spy.writes > 0 && spy.writes_under_hourglass == spy.writes);
    CHECK(arrow_shown());
    CHECK(!exists("C:\\Documents\\.~a.txt.tmp"));

    // A save that fails half way: the file on the disk is the last good
    // save, nothing is left behind, and the Editor still holds the changes.
    desktop.text(("\r\n" + std::string(3000, 'q')).c_str());
    CHECK(strcmp(title(), "a.txt* - Editor") == 0); // the star returns after a save
    spy.fail_after = 1; // the first 1024 bytes go through, then the disk fails
    letter('f', 4);
    letter('s');
    spy.fail_after = -1;
    CHECK(file("C:\\Documents\\a.txt") == "one\r\ntwo\r\ntres \xf1");
    CHECK(!exists("C:\\Documents\\.~a.txt.tmp"));
    CHECK(arrow_shown());
    key(wm::key::Enter); // the error message
    CHECK(strcmp(title(), "a.txt* - Editor") == 0);
    for (int i = 0; i < 3001; i++) { // the line break is one step
        key(wm::key::Backspace);
    }
    letter('f', 4);
    letter('s');
    CHECK(file("C:\\Documents\\a.txt") == "one\r\ntwo\r\ntres \xf1");
    CHECK(strcmp(title(), "a.txt - Editor") == 0);

    // Save As a new name: the dialog, a typed name, Enter.
    letter('f', 4);
    letter('a');
    CHECK(strcmp(title(), "Save As") == 0);
    // A click on the Editor while the dialog is open types nothing.
    key(wm::key::A, 2); // Ctrl+A in the name field
    desktop.text("copia.txt");
    key(wm::key::Enter);
    CHECK(file("C:\\Documents\\copia.txt") == "one\r\ntwo\r\ntres \xf1");
    CHECK(strcmp(title(), "copia.txt - Editor") == 0);

    // Save As over an existing file asks first; No keeps it.
    desktop.text("X");
    letter('f', 4);
    letter('a');
    key(wm::key::A, 2);
    desktop.text("a.txt");
    key(wm::key::Enter);
    CHECK(strcmp(title(), "Save As") == 0); // the question, titled like the dialog
    letter('n', 4);                          // Alt+N: No
    CHECK(file("C:\\Documents\\a.txt") == "one\r\ntwo\r\ntres \xf1");
    key(wm::key::Escape); // leave the dialog
    // Yes replaces it.
    letter('f', 4);
    letter('a');
    key(wm::key::A, 2);
    desktop.text("a.txt");
    key(wm::key::Enter);
    letter('y', 4);
    CHECK(file("C:\\Documents\\a.txt") == "one\r\ntwo\r\ntres \xf1X");

    // Closing with changes asks; No closes without saving.
    desktop.text("!");
    key(wm::key::F4, 4);
    CHECK(strcmp(title(), "Editor") == 0);
    letter('n', 4);
    CHECK(file("C:\\Documents\\a.txt") == "one\r\ntwo\r\ntres \xf1X");
    CHECK(strstr(title(), "Editor") == nullptr);

    // Open through the dialog by typing a full path.
    shell::open_in_editor(nullptr);
    letter('f', 4);
    letter('o');
    CHECK(strcmp(title(), "Open") == 0);
    desktop.text("C:\\Documents\\copia.txt");
    key(wm::key::Enter);
    CHECK(strcmp(title(), "copia.txt - Editor") == 0);
    CHECK(no_stale_pixels());
    key(wm::key::F4, 4);
}

void test_file_manager()
{
    shell::open_folder("C:\\Documents");
    CHECK(strcmp(title(), "Documents") == 0);

    // New Folder: the name dialog, then it exists.
    letter('f', 4);
    key(wm::key::Down);
    key(wm::key::Enter); // New Folder... (the menu opens on Open, greyed: nothing selected)
    CHECK(strcmp(title(), "New Folder") == 0);
    desktop.text("cosas");
    key(wm::key::Enter);
    CHECK(exists("C:\\Documents\\cosas"));
    storage.write_file("C:\\Documents\\cosas\\inside.txt", "x", 1);

    // Rename it with F2 (the new folder is selected).
    key(0x3B);
    CHECK(strcmp(title(), "Rename") == 0);
    desktop.text("trastos");
    key(wm::key::Enter);
    CHECK(!exists("C:\\Documents\\cosas") && exists("C:\\Documents\\trastos\\inside.txt"));

    // Delete it, contents and all, after saying Yes.
    key(wm::key::Delete);
    CHECK(strcmp(title(), "Confirm Folder Delete") == 0);
    letter('y', 4);
    CHECK(!exists("C:\\Documents\\trastos"));

    // A file put on the disk from outside shows after Refresh (F5).
    storage.write_file("C:\\Documents\\from-pc.txt", "hola", 4);
    key(0x3E);
    char json[2048];
    desktop.describe_windows(json, sizeof(json));
    CHECK(strstr(json, "\"title\":\"Documents\"") != nullptr);
    // Enter on a text file opens it in the Editor (from-pc.txt sorts after
    // a.txt, copia.txt, español.txt; End selects the last).
    key(wm::key::End);
    key(wm::key::Enter);
    CHECK(strcmp(title(), "todo.txt - Editor") == 0);
    CHECK(no_stale_pixels());
}

// The Trash is the File manager on C:\\.trash: the same window, which gains
// Restore and Empty Trash there and loses them on the way out.
void test_trash_view()
{
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    const auto icon = [] { return desktop.active() != nullptr ? desktop.active()->icon() : nullptr; };
    // "trastos" went to the Trash in test_file_manager.
    CHECK(exists("C:\\.trash\\trastos\\inside.txt"));
    shell::open_trash();
    CHECK(strcmp(title(), "Trash") == 0 && icon() == &gfx::bin_icon_16());
    // Asked for again, or as a folder: the window there is, not another.
    shell::open_trash();
    shell::open_folder("C:\\.trash");
    CHECK(desktop.window_count() == 1);

    // A folder window beside it is a folder window: its own icon, and the
    // Trash is still the Trash when it is gone.
    shell::open_folder("C:\\");
    CHECK(desktop.window_count() == 2 && icon() == &gfx::folder_icon_16());
    key(wm::key::F4, 4);
    CHECK(strcmp(title(), "Trash") == 0 && icon() == &gfx::bin_icon_16() && desktop.window_count() == 1);
    // The Trash has no Back and no Up (the owner): their keys do nothing.
    key(wm::key::Backspace);
    key(wm::key::Left, 4);
    CHECK(strcmp(title(), "Trash") == 0);

    // A folder in the Trash opens like any other; nothing is renamed or
    // made in there (F2, File > New Folder greyed).
    key(wm::key::Home);
    key(wm::key::Enter);
    CHECK(strcmp(title(), "trastos") == 0);
    key(wm::key::Home);
    key(0x3B); // F2
    CHECK(strcmp(title(), "trastos") == 0);
    letter('f', 4);
    key(wm::key::Down);
    key(wm::key::Enter); // New Folder..., greyed
    key(wm::key::Escape);
    CHECK(strcmp(title(), "trastos") == 0);
    key(wm::key::Backspace); // Up, there again inside a folder of the Trash
    CHECK(strcmp(title(), "Trash") == 0 && icon() == &gfx::bin_icon_16());

    // File > Restore puts the folder back where it was.
    key(wm::key::Home);
    letter('f', 4);
    letter('r');
    CHECK(!exists("C:\\.trash\\trastos") && exists("C:\\Documents\\trastos\\inside.txt"));
    CHECK(strcmp(title(), "Trash") == 0);

    // File > Empty Trash asks, then removes it all; the window stays, empty.
    storage.write_file("C:\\.trash\\gone.txt", "x", 1);
    key(0x3E); // F5
    letter('f', 4);
    letter('e');
    CHECK(strcmp(title(), "Empty Trash") == 0);
    // Emptied, it sounds of paper crumpled (backlog item 70); asked and
    // answered No, nothing is crumpled.
    static const int16_t samples[sound::kIds] = {};
    static sound::Clip clips[sound::kIds];
    for (int i = 0; i < sound::kIds; i++) {
        clips[i] = {&samples[i], 1};
    }
    host::RecordingSound heard;
    sound::set_clips(clips);
    os::install_sound(&heard);
    key(wm::key::Escape); // No
    CHECK(exists("C:\\.trash\\gone.txt") && strcmp(title(), "Trash") == 0 && heard.played.empty());
    letter('f', 4);
    letter('e');
    CHECK(heard.played.size() == 1 && heard.played[0] == &samples[static_cast<int>(sound::Id::Alert)]);
    key(wm::key::Enter); // Yes
    CHECK(!exists("C:\\.trash\\gone.txt") && strcmp(title(), "Trash") == 0);
    CHECK(heard.played.size() == 2 && heard.played[1] == &samples[static_cast<int>(sound::Id::Crumple)]);
    os::install_sound(nullptr);
    sound::set_clips(nullptr);
    CHECK(no_stale_pixels());
    key(wm::key::F4, 4);
}

// Backlog item 54: Copy, Cut, Paste and Delete for files. First the
// commands themselves on lists of several (what item 55 will hand them),
// then the File manager's keys, its Edit menu and its right-click menus.
void mouse(int x, int y, uint8_t buttons)
{
    clock_.now += 300000;
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
    desktop.tick(); // as the UI loop does after every event: change notices settle there
}

// A right click on a point of the icon view of the window in use.
void right_click_view(int x, int y)
{
    const gfx::Rect c = desktop.active()->client();
    mouse(c.x + x, c.y + 64 + y, 0);
    mouse(c.x + x, c.y + 64 + y, 2);
    mouse(c.x + x, c.y + 64 + y, 0);
}

std::string menu_labels()
{
    std::string all;
    const wm::Menu *menu = desktop.menu_open() ? desktop.popup_menu(0) : nullptr;
    for (int i = 0; menu != nullptr && i < menu->count; i++) {
        const wm::MenuItem &it = menu->items[i];
        all += it.label == nullptr ? "-" : it.label;
        all += (it.flags & wm::kMenuDisabled) ? "(off) " : " ";
    }
    return all;
}

void test_copy_cut_paste()
{
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    const uint32_t blocks = os::blocks_out();
    // The commands called here directly are asked for as the taskbar, as a
    // right click on the desktop would: a job needs a process to belong to.
    const proc::Pid taskbar = os::find("taskbar");
    CHECK(taskbar != proc::kNone);
    os::As *as = new os::As(taskbar);
    storage.make_folder("C:\\cp");
    storage.make_folder("C:\\cp\\to");
    storage.make_folder("C:\\cp\\box");
    storage.make_folder("C:\\cp\\box\\deep");
    storage.write_file("C:\\cp\\box\\in.txt", "in", 2);
    storage.write_file("C:\\cp\\box\\deep\\d.txt", "deep", 4);
    const std::string big(3000, 'b'); // more than one piece of the copy
    storage.write_file("C:\\cp\\one.txt", big.data(), big.size());

    // Copy two, a file and a folder: Paste copies them, the folder whole.
    const char *two[] = {"one.txt", "box"};
    CHECK(!shell::can_paste("C:\\cp\\to"));
    shell::copy_files({"C:\\cp", two, 2}, false);
    CHECK(os::clipboard::file_count() == 2 && !os::clipboard::files_cut());
    CHECK(shell::can_paste("C:\\cp\\to") && !shell::can_paste("") && !shell::can_paste("C:\\.trash"));
    CHECK(os::ui::clipboard_size() == 0); // files are not text: the Editor's Paste has nothing
    shell::paste_files("C:\\cp\\to", os::kNoWindow);
    CHECK(file("C:\\cp\\to\\one.txt") == big && file("C:\\cp\\to\\box\\in.txt") == "in" &&
          file("C:\\cp\\to\\box\\deep\\d.txt") == "deep");
    CHECK(file("C:\\cp\\one.txt") == big && exists("C:\\cp\\box\\deep\\d.txt"));
    CHECK(!exists("C:\\cp\\to\\.~one.txt.tmp"));
    // What was copied can be pasted again: the names taken are numbered.
    shell::paste_files("C:\\cp\\to", os::kNoWindow);
    CHECK(file("C:\\cp\\to\\one (2).txt") == big && file("C:\\cp\\to\\box (2)\\deep\\d.txt") == "deep");
    // Pasted where it came from: a numbered copy beside it.
    shell::paste_files("C:\\cp", os::kNoWindow);
    CHECK(file("C:\\cp\\one (2).txt") == big && exists("C:\\cp\\box (2)\\in.txt") && exists("C:\\cp\\one.txt"));

    // Cut: Paste moves them, once; the clipboard is empty afterwards.
    shell::copy_files({"C:\\cp", two, 2}, true);
    CHECK(os::clipboard::files_cut());
    shell::paste_files("C:\\cp", os::kNoWindow); // where they are already: nothing moves
    CHECK(exists("C:\\cp\\one.txt") && !exists("C:\\cp\\one (3).txt") && !shell::can_paste("C:\\cp"));
    shell::copy_files({"C:\\cp", two, 2}, true);
    shell::paste_files("C:\\cp\\to", os::kNoWindow);
    CHECK(!exists("C:\\cp\\one.txt") && !exists("C:\\cp\\box"));
    CHECK(file("C:\\cp\\to\\one (3).txt") == big && file("C:\\cp\\to\\box (3)\\deep\\d.txt") == "deep");
    CHECK(os::clipboard::file_count() == 0 && !shell::can_paste("C:\\cp\\to"));

    // A folder is not put inside itself, copied or cut: it says so.
    const char *box[] = {"box"};
    for (int cut = 0; cut < 2; cut++) {
        shell::copy_files({"C:\\cp\\to", box, 1}, cut == 1);
        shell::paste_files("C:\\cp\\to\\box\\deep", os::kNoWindow);
        CHECK(strcmp(title(), "Paste") == 0);
        key(wm::key::Enter);
        CHECK(!exists("C:\\cp\\to\\box\\deep\\box") && exists("C:\\cp\\to\\box\\in.txt"));
    }
    // What was cut and has gone since: it says so, and nothing else happens.
    const char *one[] = {"one.txt"};
    shell::copy_files({"C:\\cp\\to", one, 1}, false);
    storage.remove("C:\\cp\\to\\one.txt");
    shell::paste_files("C:\\cp", os::kNoWindow);
    CHECK(strcmp(title(), "Paste") == 0);
    key(wm::key::Enter);
    // A disk that fails half way: no part of a file is left, and it says so.
    const char *numbered[] = {"one (2).txt"};
    shell::copy_files({"C:\\cp\\to", numbered, 1}, false);
    spy.fail_after = 1;
    shell::paste_files("C:\\cp", os::kNoWindow);
    spy.fail_after = -1;
    CHECK(strcmp(title(), "Paste") == 0);
    key(wm::key::Enter);
    CHECK(file("C:\\cp\\one (2).txt") == big && !exists("C:\\cp\\one (3).txt") && !exists("C:\\cp\\.~one (3).txt.tmp"));
    // Text put on the clipboard takes the files away.
    wm::clipboard::set(reinterpret_cast<const uint8_t *>("abc"), 3);
    CHECK(!shell::can_paste("C:\\cp") && os::clipboard::file_count() == 0 && os::ui::clipboard_size() == 3);

    // Delete for several asks once, saying how many, and all go to the
    // Trash, each remembered with where it came from.
    const char *three[] = {"one (2).txt", "box", "box (2)"};
    shell::delete_asking({"C:\\cp\\to", three, 3});
    CHECK(strcmp(title(), "Confirm Delete") == 0);
    letter('n', 4);
    CHECK(exists("C:\\cp\\to\\box") && !exists("C:\\.trash\\box"));
    shell::delete_asking({"C:\\cp\\to", three, 3});
    letter('y', 4);
    CHECK(!exists("C:\\cp\\to\\one (2).txt") && !exists("C:\\cp\\to\\box") && !exists("C:\\cp\\to\\box (2)"));
    CHECK(exists("C:\\.trash\\one (2).txt") && exists("C:\\.trash\\box\\deep\\d.txt") && exists("C:\\.trash\\box (2)"));
    char from[fs::kMaxPath];
    CHECK(shell::trash_origin("box (2)", from, sizeof(from)) && strcmp(from, "C:\\cp\\to\\box (2)") == 0);
    // In the Trash the same question is for good.
    const char *gone[] = {"one (2).txt", "box"};
    shell::delete_asking({"C:\\.trash", gone, 2});
    CHECK(strcmp(title(), "Confirm Delete") == 0);
    letter('y', 4);
    CHECK(!exists("C:\\.trash\\one (2).txt") && !exists("C:\\.trash\\box") && exists("C:\\.trash\\box (2)"));
    // A question dropped for another one costs nothing.
    shell::delete_asking({"C:\\cp\\to", three + 2, 0});
    shell::delete_asking("C:\\cp\\to\\box (3)");
    shell::delete_asking("C:\\cp\\to\\one (3).txt");
    CHECK(strcmp(title(), "Confirm File Delete") == 0);
    letter('y', 4);
    CHECK(exists("C:\\cp\\to\\box (3)") && !exists("C:\\cp\\to\\one (3).txt"));
    CHECK(os::blocks_out() == blocks); // every Paste and every question gave its block back
    delete as;

    // The File manager. C:\cp shows: box (2), to, one (2).txt, uno.txt.
    storage.write_file("C:\\cp\\uno.txt", "uno", 3);
    shell::open_folder("C:\\cp");
    key(wm::key::End);
    letter('c', 2); // Ctrl+C
    CHECK(os::clipboard::file_count() == 1);
    letter('v', 2); // Ctrl+V: a copy beside it
    CHECK(file("C:\\cp\\uno (2).txt") == "uno");
    key(wm::key::End); // uno.txt, after its copy
    letter('x', 2);    // Ctrl+X
    CHECK(os::clipboard::files_cut());
    key(wm::key::Home);
    key(wm::key::Right); // "to"
    key(wm::key::Enter);
    CHECK(strcmp(title(), "to") == 0);
    // Edit > Paste.
    letter('e', 4);
    CHECK(menu_labels() == "Cu&t(off) &Copy(off) &Paste ");
    letter('p');
    CHECK(file("C:\\cp\\to\\uno.txt") == "uno" && !exists("C:\\cp\\uno.txt"));
    letter('e', 4);
    CHECK(menu_labels() == "Cu&t(off) &Copy(off) &Paste(off) "); // nothing picked, nothing to paste
    key(wm::key::Escape);

    // A right click on a file picks it and offers Open, Cut, Copy, Delete.
    // Here: box (3), uno.txt.
    right_click_view(12 + 76 + 30, 30);
    CHECK(menu_labels() == "&Open - Cu&t &Copy - &Delete ");
    letter('c');
    char path[fs::kMaxPath] = "";
    CHECK(os::clipboard::file_count() == 1 && os::clipboard::file(0, path, sizeof(path)) &&
          strcmp(path, "C:\\cp\\to\\uno.txt") == 0);
    // On a folder there is Paste too, into that folder.
    right_click_view(12 + 30, 30); // box (3)
    CHECK(menu_labels() == "&Open - Cu&t &Copy &Paste - &Delete ");
    letter('p');
    CHECK(file("C:\\cp\\to\\box (3)\\uno.txt") == "uno");
    // On the blank part: Paste into the folder shown, and nothing picked.
    key(wm::key::Backspace); // up to C:\cp: box (2), to, one (2).txt, uno (2).txt
    right_click_view(200, 150);
    CHECK(menu_labels() == "&Paste - New &Folder... Re&fresh ");
    letter('p');
    CHECK(file("C:\\cp\\uno.txt") == "uno");
    // Cut from the menu, pasted into a folder: moved, and Paste is over.
    right_click_view(12 + 2 * 76 + 30, 30); // one (2).txt
    letter('t');
    right_click_view(12 + 30, 30); // box (2)
    letter('p');
    CHECK(file("C:\\cp\\box (2)\\one (2).txt") == big && !exists("C:\\cp\\one (2).txt"));
    right_click_view(200, 150);
    CHECK(menu_labels() == "&Paste(off) - New &Folder... Re&fresh ");
    key(wm::key::Escape);
    // Delete from the menu asks as the key does. Now: box (2), to,
    // uno (2).txt, uno.txt.
    right_click_view(12 + 3 * 76 + 30, 30);
    letter('d');
    CHECK(strcmp(title(), "Confirm File Delete") == 0);
    letter('y', 4);
    CHECK(!exists("C:\\cp\\uno.txt") && exists("C:\\.trash\\uno.txt"));

    // In the Trash nothing is cut, copied or pasted; Restore is in the menu.
    shell::open_trash();
    letter('e', 4);
    CHECK(menu_labels() == "Cu&t(off) &Copy(off) &Paste(off) ");
    key(wm::key::Escape);
    right_click_view(12 + 30, 30);
    CHECK(menu_labels() == "&Open &Restore - &Delete ");
    key(wm::key::Escape);
    right_click_view(300, 150);
    CHECK(menu_labels() == "&Empty Trash - Re&fresh ");
    key(wm::key::Escape);
    // And in My Board, whose entries are drives.
    shell::open_folder(nullptr);
    right_click_view(12 + 30, 30);
    CHECK(menu_labels() == "&Open ");
    key(wm::key::Escape);
    right_click_view(300, 150);
    CHECK(menu_labels() == "Re&fresh ");
    key(wm::key::Escape);
    CHECK(no_stale_pixels());
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    clock_.now += 10000;
    desktop.tick(); // closed windows are destroyed at the next safe point
    CHECK(os::blocks_out() == blocks);
}

// The desktop's icons follow C:\\Documents\\Desktop by change notices
// (fs::Storage::set_changed), not by reading the folder every 2 s: an idle
// desktop reads nothing, a file put there any way shows at the next round of
// the loop, and a change somewhere else costs the desktop nothing.
void idle(int seconds)
{
    for (int i = 0; i < seconds * 10; i++) {
        clock_.now += 100000;
        desktop.tick();
    }
}

void test_change_notices()
{
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    desktop.tick();
    desk::Icons &icons = *shell::desktop_icons();
    const int before = icons.count();

    // Twenty seconds of nothing: not one walk of a folder.
    int walks = spy.listings;
    idle(20);
    CHECK(spy.listings == walks);

    // A file written straight to the drive (as tools/dbg.py put does):
    // its icon is there after one round, with one walk of the desktop's
    // folder and one look into the Trash.
    storage.make_folder("C:\\Documents\\Desktop");
    desktop.tick();
    walks = spy.listings;
    storage.write_file("C:\\Documents\\Desktop\\nota.txt", "n", 1);
    CHECK(icons.count() == before); // not in the middle of the write
    desktop.tick();
    CHECK(icons.count() == before + 1 && spy.listings == walks + 2);

    // Changes elsewhere are not the desktop's business.
    walks = spy.listings;
    storage.write_file("C:\\Documents\\other.txt", "o", 1);
    storage.make_folder("C:\\elsewhere");
    idle(5);
    CHECK(icons.count() == before + 1 && spy.listings == walks);

    // Several changes in one round are one reading.
    walks = spy.listings;
    storage.write_file("C:\\Documents\\Desktop\\a.txt", "a", 1);
    storage.write_file("C:\\Documents\\Desktop\\b.txt", "b", 1);
    storage.remove("C:\\Documents\\Desktop\\nota.txt");
    desktop.tick();
    CHECK(icons.count() == before + 2 && spy.listings == walks + 2);

    // A File manager window on a folder follows it too, and one on another
    // folder is left alone.
    shell::open_folder("C:\\elsewhere");
    desktop.tick();
    walks = spy.listings;
    storage.write_file("C:\\Documents\\other2.txt", "o", 1);
    desktop.tick();
    CHECK(spy.listings == walks);
    storage.write_file("C:\\elsewhere\\new.txt", "n", 1);
    desktop.tick();
    CHECK(spy.listings > walks);
    // The folder it shows is removed (its parent is what changed): it sees that.
    storage.remove("C:\\elsewhere\\new.txt");
    storage.remove("C:\\elsewhere");
    desktop.tick();
    CHECK(strcmp(title(), "My Board") == 0);
    desktop.close(*desktop.active());
    desktop.tick();

    // With a queue and tasks, as on the board: the notice is a message, the
    // folder is read by a job on another task, and the icons are set when
    // the job's result comes back.
    static host::ThreadQueue queue;
    static host::ThreadTasks tasks;
    msg::Inbox inbox(queue);
    msg::set_queue(&queue);
    job::jobs().set_tasks(&tasks);
    const int started = tasks.started;
    tasks.hold();
    storage.write_file("C:\\Documents\\Desktop\\c.txt", "c", 1);
    CHECK(tasks.started == started); // nothing until the UI task takes the message
    inbox.deliver();
    desktop.tick();
    CHECK(tasks.started == started + 1 && icons.count() == before + 2); // being read; the desktop goes on
    // A change while the job reads: it reads once more when it is back.
    storage.write_file("C:\\Documents\\Desktop\\d.txt", "d", 1);
    clock_.now += 300000; // past the pause between two settlings
    desktop.tick();
    inbox.deliver();
    CHECK(tasks.started == started + 1);
    tasks.finish();
    inbox.deliver(); // the first job is back: starts the second
    tasks.finish();
    inbox.deliver();
    CHECK(tasks.started == started + 2 && icons.count() == before + 4);
    // No task to be had: tried again by the 2 s tick, which otherwise reads nothing.
    tasks.refuse = true;
    storage.remove("C:\\Documents\\Desktop\\d.txt");
    clock_.now += 300000;
    desktop.tick();
    inbox.deliver();
    CHECK(icons.count() == before + 4);
    tasks.refuse = false;
    for (int i = 0; i < 30 && icons.count() != before + 3; i++) {
        clock_.now += 100000;
        desktop.tick();
        tasks.finish();
        inbox.deliver();
    }
    CHECK(icons.count() == before + 3);
    walks = spy.listings;
    for (int i = 0; i < 200; i++) {
        clock_.now += 100000;
        desktop.tick();
        inbox.deliver();
    }
    CHECK(spy.listings == walks && tasks.started == started + 3);
    job::jobs().set_tasks(nullptr);
    msg::set_queue(nullptr);
    for (const char *name : {"a.txt", "b.txt", "c.txt"}) {
        char path[fs::kMaxPath];
        fs::path::join("C:\\Documents\\Desktop", name, path, sizeof(path));
        storage.remove(path);
    }
    desktop.tick();
    CHECK(icons.count() == before);
}

} // namespace

int main()
{
    static fs::MemoryVolume ram_disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    spy.set(ram_disk);
    storage.mount('C', spy);
    fs::add_samples(storage, 'C');
    // A drive from before C:\Documents: its C:\docs moves in at start-up,
    // a clashing name numbered, nothing overwritten, C:\docs gone.
    storage.make_folder("C:\\docs");
    storage.make_folder("C:\\docs\\old");
    storage.write_file("C:\\docs\\old\\in.txt", "in", 2);
    storage.write_file("C:\\docs\\notes.txt", "mine", 4);
    storage.write_file("C:\\docs\\only.txt", "only", 4);
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    CHECK(!exists("C:\\docs") && file("C:\\Documents\\only.txt") == "only" &&
          file("C:\\Documents\\notes (2).txt") == "mine" && file("C:\\Documents\\old\\in.txt") == "in" &&
          file("C:\\Documents\\notes.txt").size() > 100);
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    test_editor();
    test_file_manager();
    test_trash_view();
    test_copy_cut_paste();
    test_change_notices();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
