// End-to-end tests of the programs on a RAM disk: Editor, file dialogs and
// File manager, driven with keys and checked on the disk.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/fs/memory_volume.hpp"
#include "core/shell/shell.hpp"
#include "core/sound/beeps.hpp"
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

// The RAM disk with a spy in front: it notes the pointer's shape at each
// write, and can fail writes as a full or broken disk would.
class SpyVolume : public ports::Volume {
public:
    void set(ports::Volume &v) { v_ = &v; }
    int writes = 0, writes_under_hourglass = 0;
    int fail_after = -1; // writes that still succeed; -1: all of them

    ports::FsError info(ports::VolumeInfo &o) override { return v_->info(o); }
    ports::FsError entry(const char *f, int i, ports::DirEntry &o) override { return v_->entry(f, i, o); }
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
    // The desktop is up: the beeper's chime, once.
    CHECK(system_.beep_calls == 1 && system_.beeps.size() == static_cast<size_t>(sound::desktop_chime().count) &&
          system_.beeps[0].hz == sound::desktop_chime().steps[0].hz);
    CHECK(!exists("C:\\docs") && file("C:\\Documents\\only.txt") == "only" &&
          file("C:\\Documents\\notes (2).txt") == "mine" && file("C:\\Documents\\old\\in.txt") == "in" &&
          file("C:\\Documents\\notes.txt").size() > 100);
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    test_editor();
    test_file_manager();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
