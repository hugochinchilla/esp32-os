// Tests for drives, paths and the RAM disk.
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "core/fs/fs.hpp"
#include "core/fs/ini.hpp"
#include "core/fs/memory_volume.hpp"
#include "../folder_volume.hpp"
#include "../host_adapters.hpp"
#include <atomic>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

using ports::FsError;
using ports::OpenMode;

static void test_paths()
{
    char out[fs::kMaxPath];
    CHECK(fs::path::drive("c:\\docs") == 'C' && fs::path::drive("docs") == 0);
    CHECK(strcmp(fs::path::relative("C:\\docs\\a.txt"), "docs\\a.txt") == 0);
    CHECK(strcmp(fs::path::relative("C:\\"), "") == 0);
    CHECK(strcmp(fs::path::name("C:\\docs\\a.txt"), "a.txt") == 0);
    CHECK(strcmp(fs::path::name("C:\\docs\\"), "docs\\") == 0 || strcmp(fs::path::name("C:\\docs\\"), "docs") == 0);
    CHECK(strcmp(fs::path::name("C:\\"), "") == 0);
    fs::path::extension("C:\\docs\\Notes.TXT", out, sizeof(out));
    CHECK(strcmp(out, "txt") == 0);
    fs::path::extension("C:\\docs\\README", out, sizeof(out));
    CHECK(strcmp(out, "") == 0);
    fs::path::parent("C:\\docs\\a.txt", out, sizeof(out));
    CHECK(strcmp(out, "C:\\docs") == 0);
    fs::path::parent("C:\\docs", out, sizeof(out));
    CHECK(strcmp(out, "C:\\") == 0);
    fs::path::parent("C:\\", out, sizeof(out));
    CHECK(strcmp(out, "C:\\") == 0);
    CHECK(fs::path::join("C:\\", "docs", out, sizeof(out)) && strcmp(out, "C:\\docs") == 0);
    CHECK(fs::path::join("C:\\docs", "a.txt", out, sizeof(out)) && strcmp(out, "C:\\docs\\a.txt") == 0);
    CHECK(fs::path::valid_name("notes.txt") && fs::path::valid_name("año 2026.txt"));
    CHECK(!fs::path::valid_name("") && !fs::path::valid_name("a:b") && !fs::path::valid_name("a?") &&
          !fs::path::valid_name("..") && !fs::path::valid_name("trailing.") && !fs::path::valid_name("x\\y"));
}

static void test_memory_volume()
{
    std::vector<uint8_t> pool(64 * 1024);
    fs::MemoryVolume v(pool.data(), pool.size(), "RAM disk");
    fs::Storage s;
    s.mount('c', v);
    char letters[4];
    CHECK(s.drives(letters, 4) == 1 && letters[0] == 'C');

    ports::VolumeInfo info;
    CHECK(s.info('C', info) == FsError::Ok && strcmp(info.label, "RAM disk") == 0 && !info.keeps_files);
    const uint64_t empty_free = info.free_bytes;
    CHECK(empty_free > 40 * 1024 && info.total_bytes == empty_free);

    // Folders and files.
    CHECK(s.make_folder("C:\\docs") == FsError::Ok);
    CHECK(s.make_folder("C:\\DOCS") == FsError::Exists); // names ignore case
    CHECK(s.make_folder("C:\\nowhere\\x") == FsError::NotFound);
    CHECK(s.make_folder("C:\\bad:name") == FsError::BadName);
    const char text[] = "Hola, año\r\n";
    CHECK(s.write_file("C:\\docs\\a.txt", text, sizeof(text) - 1) == FsError::Ok);
    char buf[64];
    size_t n = 0;
    CHECK(s.read_file("c:\\Docs\\A.TXT", buf, sizeof(buf), n) == FsError::Ok);
    CHECK(n == sizeof(text) - 1 && memcmp(buf, text, n) == 0);
    ports::DirEntry e;
    CHECK(s.stat("C:\\docs\\a.txt", e) == FsError::Ok && !e.folder && e.size == n);
    CHECK(s.stat("C:\\docs", e) == FsError::Ok && e.folder);
    CHECK(s.entry("C:\\", 0, e) == FsError::Ok && strcmp(e.name, "docs") == 0);
    CHECK(s.entry("C:\\", 1, e) == FsError::NotFound);
    CHECK(s.entry("C:\\docs\\a.txt", 0, e) == FsError::NotFolder);
    CHECK(s.read_file("C:\\docs", buf, sizeof(buf), n) == FsError::IsFolder);

    // Rename, move, and the rules around them.
    CHECK(s.make_folder("C:\\old") == FsError::Ok);
    CHECK(s.rename("C:\\docs\\a.txt", "C:\\docs\\b.txt") == FsError::Ok);
    CHECK(s.stat("C:\\docs\\a.txt", e) == FsError::NotFound && s.stat("C:\\docs\\b.txt", e) == FsError::Ok);
    CHECK(s.rename("C:\\docs\\b.txt", "C:\\old\\b.txt") == FsError::Ok);
    CHECK(s.rename("C:\\old", "C:\\old\\inside") == FsError::BadName); // not into itself
    CHECK(s.write_file("C:\\docs\\c.txt", "x", 1) == FsError::Ok);
    CHECK(s.rename("C:\\docs\\c.txt", "C:\\old\\B.TXT") == FsError::Exists);
    CHECK(s.rename("C:\\docs\\c.txt", "C:\\docs\\C.txt") == FsError::Ok); // a change of case
    CHECK(s.remove("C:\\old") == FsError::NotEmpty);
    // replace(): the name changes hands in one step, over a file already
    // there. The entry keeps its place and its name as written.
    CHECK(s.write_file("C:\\docs\\new.txt", "new", 3) == FsError::Ok);
    CHECK(s.replace("C:\\docs\\new.txt", "C:\\old\\B.TXT") == FsError::Ok);
    CHECK(s.stat("C:\\docs\\new.txt", e) == FsError::NotFound);
    CHECK(s.read_file("C:\\old\\b.txt", buf, sizeof(buf), n) == FsError::Ok && n == 3 && memcmp(buf, "new", 3) == 0);
    CHECK(s.entry("C:\\old", 0, e) == FsError::Ok && strcmp(e.name, "b.txt") == 0);
    CHECK(s.entry("C:\\old", 1, e) == FsError::NotFound);
    CHECK(s.replace("C:\\docs\\C.txt", "C:\\docs\\fresh.txt") == FsError::Ok); // nothing there: a rename
    CHECK(s.rename("C:\\docs\\fresh.txt", "C:\\docs\\C.txt") == FsError::Ok);
    CHECK(s.replace("C:\\docs\\C.txt", "C:\\old") == FsError::IsFolder);
    CHECK(s.replace("C:\\old", "C:\\docs\\C.txt") == FsError::IsFolder);
    CHECK(s.replace("C:\\docs\\none.txt", "C:\\docs\\C.txt") == FsError::NotFound);
    // A safe save: the old file is whole until the last step, and no
    // temporary file is left, whether the save is kept or given up.
    fs::Save save;
    CHECK(s.begin_save("C:\\old\\b.txt", save) == FsError::Ok);
    CHECK(s.write(save.file, "half", 4) == FsError::Ok);
    CHECK(s.read_file("C:\\old\\b.txt", buf, sizeof(buf), n) == FsError::Ok && n == 3 && memcmp(buf, "new", 3) == 0);
    CHECK(s.stat("C:\\old\\.~b.txt.tmp", e) == FsError::Ok);
    CHECK(s.finish_save(save, false) == FsError::Ok); // given up
    CHECK(s.read_file("C:\\old\\b.txt", buf, sizeof(buf), n) == FsError::Ok && n == 3 && memcmp(buf, "new", 3) == 0);
    CHECK(s.entry("C:\\old", 1, e) == FsError::NotFound);
    CHECK(s.begin_save("C:\\old\\b.txt", save) == FsError::Ok);
    CHECK(s.write(save.file, "whole", 5) == FsError::Ok);
    CHECK(s.finish_save(save) == FsError::Ok);
    CHECK(s.read_file("C:\\old\\b.txt", buf, sizeof(buf), n) == FsError::Ok && n == 5 && memcmp(buf, "whole", 5) == 0);
    CHECK(s.entry("C:\\old", 1, e) == FsError::NotFound);
    CHECK(s.finish_save(save) == FsError::NotFound); // not begun
    CHECK(s.begin_save("C:\\old", save) == FsError::IsFolder);
    CHECK(s.begin_save("C:\\nowhere\\x.txt", save) == FsError::NotFound);
    CHECK(s.remove("C:\\old\\b.txt") == FsError::Ok && s.remove("C:\\old") == FsError::Ok);
    CHECK(s.remove("C:\\") == FsError::BadName);

    // A large file in pieces, read back with seeks; its blocks come back.
    std::vector<uint8_t> big(20000);
    std::mt19937 random(5);
    for (auto &b : big) {
        b = static_cast<uint8_t>(random());
    }
    fs::File f;
    CHECK(s.open("C:\\big.bin", OpenMode::Write, f) == FsError::Ok);
    for (size_t at = 0; at < big.size(); at += 777) {
        size_t piece = big.size() - at < 777 ? big.size() - at : 777;
        CHECK(s.write(f, big.data() + at, piece) == FsError::Ok);
    }
    // While it is open for writing, it cannot be removed or opened again to write.
    CHECK(s.remove("C:\\big.bin") == FsError::InUse);
    fs::File g;
    CHECK(s.open("C:\\big.bin", OpenMode::Update, g) == FsError::InUse);
    CHECK(s.close(f) == FsError::Ok);
    CHECK(s.open("C:\\big.bin", OpenMode::Read, f) == FsError::Ok);
    std::vector<uint8_t> back(big.size());
    for (uint32_t at : {15000u, 0u, 511u, 512u, 19999u}) {
        CHECK(s.seek(f, at) == FsError::Ok);
        size_t got = 0;
        CHECK(s.read(f, back.data(), 600, got) == FsError::Ok);
        const size_t want = big.size() - at < 600 ? big.size() - at : 600;
        CHECK(got == want && memcmp(back.data(), big.data() + at, got) == 0);
    }
    CHECK(s.seek(f, 30000) == FsError::Io);
    s.close(f);
    // Update writes in the middle without changing the rest.
    CHECK(s.open("C:\\big.bin", OpenMode::Update, f) == FsError::Ok);
    CHECK(s.seek(f, 1000) == FsError::Ok && s.write(f, "ZZZ", 3) == FsError::Ok);
    s.close(f);
    CHECK(s.read_file("C:\\big.bin", back.data(), back.size(), n) == FsError::Ok && n == big.size());
    CHECK(memcmp(back.data(), big.data(), 1000) == 0 && memcmp(back.data() + 1000, "ZZZ", 3) == 0 &&
          memcmp(back.data() + 1003, big.data() + 1003, big.size() - 1003) == 0);
    CHECK(s.remove("C:\\big.bin") == FsError::Ok);

    // Filling the disk gives NoSpace, and removing frees everything again.
    std::vector<uint8_t> chunk(4096, 0x41);
    CHECK(s.open("C:\\fill.bin", OpenMode::Write, f) == FsError::Ok);
    FsError last = FsError::Ok;
    for (int i = 0; i < 100 && last == FsError::Ok; i++) {
        last = s.write(f, chunk.data(), chunk.size());
    }
    CHECK(last == FsError::NoSpace);
    s.close(f);
    CHECK(s.remove("C:\\fill.bin") == FsError::Ok);
    s.remove("C:\\docs\\C.txt");
    s.remove("C:\\docs");
    CHECK(s.info('C', info) == FsError::Ok && info.free_bytes == empty_free);

    // Too many open files.
    fs::File files[fs::MemoryVolume::kMaxOpen + 1];
    s.write_file("C:\\r.txt", "r", 1);
    int opened = 0;
    for (auto &file : files) {
        opened += s.open("C:\\r.txt", OpenMode::Read, file) == FsError::Ok;
    }
    CHECK(opened == fs::MemoryVolume::kMaxOpen);
    for (auto &file : files) {
        s.close(file);
    }

    // No drive, no luck.
    CHECK(s.stat("Q:\\x", e) == FsError::NotFound);
}

// What every drive must do, whatever keeps its files: the RAM disk, and the
// POSIX folder drive (a PC folder here; the same code is the flash drive on
// the board).
static void check_drive(ports::Volume &v, const char *what)
{
    printf("drive behaviour: %s\n", what);
    fs::Storage s;
    s.mount('C', v);
    CHECK(s.make_folder("C:\\docs") == FsError::Ok);
    CHECK(s.make_folder("C:\\DOCS") == FsError::Exists); // names ignore case
    CHECK(s.make_folder("C:\\nowhere\\x") == FsError::NotFound);
    const char text[] = "Hola, a\xc3\xb1o\r\n";
    CHECK(s.write_file("C:\\docs\\a.txt", text, sizeof(text) - 1) == FsError::Ok);
    char buf[64];
    size_t n = 0;
    CHECK(s.read_file("c:\\Docs\\A.TXT", buf, sizeof(buf), n) == FsError::Ok);
    CHECK(n == sizeof(text) - 1 && memcmp(buf, text, n) == 0);
    ports::DirEntry e;
    CHECK(s.stat("C:\\docs\\a.txt", e) == FsError::Ok && !e.folder && e.size == n);
    CHECK(s.stat("C:\\docs", e) == FsError::Ok && e.folder);
    CHECK(s.entry("C:\\docs", 0, e) == FsError::Ok && strcmp(e.name, "a.txt") == 0);
    CHECK(s.entry("C:\\docs", 1, e) == FsError::NotFound);
    CHECK(s.entry("C:\\docs\\a.txt", 0, e) == FsError::NotFolder);
    CHECK(s.read_file("C:\\docs", buf, sizeof(buf), n) == FsError::IsFolder);
    CHECK(s.make_folder("C:\\old") == FsError::Ok);
    CHECK(s.rename("C:\\docs\\a.txt", "C:\\docs\\b.txt") == FsError::Ok);
    CHECK(s.stat("C:\\docs\\a.txt", e) == FsError::NotFound && s.stat("C:\\docs\\b.txt", e) == FsError::Ok);
    CHECK(s.rename("C:\\docs\\b.txt", "C:\\old\\b.txt") == FsError::Ok);
    CHECK(s.write_file("C:\\docs\\c.txt", "x", 1) == FsError::Ok);
    CHECK(s.rename("C:\\docs\\c.txt", "C:\\old\\B.TXT") == FsError::Exists);
    CHECK(s.rename("C:\\docs\\c.txt", "C:\\docs\\C.txt") == FsError::Ok); // a change of case
    CHECK(s.entry("C:\\docs", 0, e) == FsError::Ok && strcmp(e.name, "C.txt") == 0);
    CHECK(s.remove("C:\\old") == FsError::NotEmpty);
    CHECK(s.remove("C:\\old\\b.txt") == FsError::Ok && s.remove("C:\\old") == FsError::Ok);
    // A file in pieces, read back with seeks; a write in the middle.
    std::vector<uint8_t> big(20000);
    std::mt19937 random(5);
    for (auto &b : big) {
        b = static_cast<uint8_t>(random());
    }
    fs::File f;
    CHECK(s.open("C:\\big.bin", OpenMode::Write, f) == FsError::Ok);
    for (size_t at = 0; at < big.size(); at += 777) {
        CHECK(s.write(f, big.data() + at, big.size() - at < 777 ? big.size() - at : 777) == FsError::Ok);
    }
    CHECK(s.close(f) == FsError::Ok);
    CHECK(s.open("C:\\BIG.bin", OpenMode::Read, f) == FsError::Ok);
    std::vector<uint8_t> back(big.size());
    for (uint32_t at : {15000u, 0u, 511u, 512u, 19999u}) {
        CHECK(s.seek(f, at) == FsError::Ok);
        size_t got = 0;
        CHECK(s.read(f, back.data(), 600, got) == FsError::Ok);
        const size_t want = big.size() - at < 600 ? big.size() - at : 600;
        CHECK(got == want && memcmp(back.data(), big.data() + at, got) == 0);
    }
    CHECK(s.seek(f, 30000) == FsError::Io); // past the end
    s.close(f);
    CHECK(s.open("C:\\big.bin", OpenMode::Update, f) == FsError::Ok);
    CHECK(s.seek(f, 1000) == FsError::Ok && s.write(f, "ZZZ", 3) == FsError::Ok);
    s.close(f);
    CHECK(s.read_file("C:\\big.bin", back.data(), back.size(), n) == FsError::Ok && n == big.size());
    CHECK(memcmp(back.data() + 1000, "ZZZ", 3) == 0 && memcmp(back.data() + 1003, big.data() + 1003, 100) == 0);
    CHECK(s.remove("C:\\big.bin") == FsError::Ok);
    s.remove("C:\\docs\\C.txt");
    CHECK(s.remove("C:\\docs") == FsError::Ok);
    CHECK(s.entry("C:\\", 0, e) == FsError::NotFound); // empty again

    // A run of entries in one pass gives what asking one by one gives, in
    // the same order, whatever the size of the run; fewer than asked for
    // is the end; a folder that is not there has none.
    CHECK(s.make_folder("C:\\many") == FsError::Ok);
    for (int i = 0; i < 20; i++) {
        char name[32];
        snprintf(name, sizeof(name), "C:\\many\\f%02d.txt", i);
        CHECK(s.write_file(name, "x", 1) == FsError::Ok);
    }
    for (int run : {1, 7, 20, 32}) {
        ports::DirEntry got[32];
        int total = 0;
        for (;;) {
            int count = -1;
            CHECK(s.entries("C:\\many", total, got, run, count) == FsError::Ok);
            for (int i = 0; i < count; i++) {
                CHECK(s.entry("C:\\many", total + i, e) == FsError::Ok && strcmp(e.name, got[i].name) == 0 &&
                      e.size == got[i].size && e.folder == got[i].folder);
            }
            total += count;
            if (count < run) {
                break;
            }
        }
        CHECK(total == 20);
    }
    int none = -1;
    CHECK(s.entries("C:\\gone", 0, &e, 1, none) == FsError::Ok && none == 0);
    for (int i = 0; i < 20; i++) {
        char name[32];
        snprintf(name, sizeof(name), "C:\\many\\f%02d.txt", i);
        CHECK(s.remove(name) == FsError::Ok);
    }
    CHECK(s.remove("C:\\many") == FsError::Ok);
}

static void test_drives_behave_alike()
{
    std::vector<uint8_t> pool(64 * 1024);
    fs::MemoryVolume ram(pool.data(), pool.size(), "RAM disk");
    check_drive(ram, "RAM disk");
    char dir[] = "/tmp/chinchilla-drive-XXXXXX";
    if (mkdtemp(dir) == nullptr) {
        CHECK(false);
        return;
    }
    host::FolderVolume folder(dir);
    check_drive(folder, "POSIX folder");
    rmdir(dir);
}

// When a file was last written (ports::DirEntry::modified).
static void test_dates()
{
    // The RAM disk asks the machine for the time. Without a clock, or
    // while the clock does not know, there is no date.
    std::vector<uint8_t> pool(64 * 1024);
    fs::MemoryVolume ram(pool.data(), pool.size(), "RAM disk");
    fs::Storage s;
    s.mount('C', ram);
    fs::DirEntry e;
    CHECK(s.write_file("C:\\a.txt", "one", 3) == FsError::Ok);
    CHECK(s.stat("C:\\a.txt", e) == FsError::Ok && e.modified == 0);
    host::HostSystem machine;
    ram.set_clock(&machine);
    CHECK(s.write_file("C:\\a.txt", "two", 3) == FsError::Ok);
    CHECK(s.stat("C:\\a.txt", e) == FsError::Ok && e.modified == 0);

    // Made, written, saved over: the date of that moment.
    machine.utc = 1000;
    CHECK(s.make_folder("C:\\docs") == FsError::Ok && s.stat("C:\\docs", e) == FsError::Ok && e.modified == 1000);
    fs::File f;
    CHECK(s.open("C:\\b.txt", OpenMode::Write, f) == FsError::Ok);
    machine.utc = 2000;
    CHECK(s.write(f, "x", 1) == FsError::Ok);
    machine.utc = 3000;
    CHECK(s.close(f) == FsError::Ok); // the date is that of the close
    CHECK(s.stat("C:\\b.txt", e) == FsError::Ok && e.modified == 3000);
    CHECK(s.entry("C:\\", 2, e) == FsError::Ok && strcmp(e.name, "b.txt") == 0 && e.modified == 3000);
    machine.utc = 4000;
    CHECK(s.write_file("C:\\a.txt", "three", 5) == FsError::Ok); // the safe way: a temporary file takes its place
    CHECK(s.stat("C:\\a.txt", e) == FsError::Ok && e.modified == 4000);
    // Reading, or opening to change and changing nothing, leaves it; a new
    // name or folder keeps it; a change in the middle moves it.
    machine.utc = 5000;
    char back[8];
    size_t n = 0;
    CHECK(s.read_file("C:\\b.txt", back, sizeof back, n) == FsError::Ok);
    CHECK(s.open("C:\\b.txt", OpenMode::Update, f) == FsError::Ok && s.close(f) == FsError::Ok);
    CHECK(s.rename("C:\\b.txt", "C:\\docs\\c.txt") == FsError::Ok);
    CHECK(s.stat("C:\\docs\\c.txt", e) == FsError::Ok && e.modified == 3000);
    CHECK(s.open("C:\\docs\\c.txt", OpenMode::Update, f) == FsError::Ok);
    CHECK(s.write(f, "y", 1) == FsError::Ok && s.close(f) == FsError::Ok);
    CHECK(s.stat("C:\\docs\\c.txt", e) == FsError::Ok && e.modified == 5000);

    // A folder of the PC (the same code as the board's flash disk): the
    // file system's own date. One from before 2000 is "not known": the
    // board writes those while it has not been told the time.
    char dir[] = "/tmp/chinchilla-dates-XXXXXX";
    if (mkdtemp(dir) == nullptr) {
        CHECK(false);
        return;
    }
    host::FolderVolume folder(dir);
    fs::Storage d;
    d.mount('D', folder);
    const int64_t now = static_cast<int64_t>(time(nullptr));
    CHECK(d.write_file("D:\\a.txt", "one", 3) == FsError::Ok);
    CHECK(d.stat("D:\\a.txt", e) == FsError::Ok && e.modified >= now - 2 && e.modified <= now + 5);
    CHECK(d.entry("D:\\", 0, e) == FsError::Ok && e.modified >= now - 2 && e.modified <= now + 5);
    const std::string file = std::string(dir) + "/a.txt";
    const struct timespec old[2] = {{631152000, 0}, {631152000, 0}}; // 1990
    CHECK(utimensat(AT_FDCWD, file.c_str(), old, 0) == 0);
    CHECK(d.stat("D:\\a.txt", e) == FsError::Ok && e.modified == 0);
    CHECK(d.entry("D:\\", 0, e) == FsError::Ok && e.modified == 0);
    CHECK(d.remove("D:\\a.txt") == FsError::Ok);
    rmdir(dir);
}

// Program settings files: read, change one value, keep everything else.
static void test_ini()
{
    fs::Ini ini;
    ini.set_text("; viewer settings\r\n"
                 "top = 1\r\n"
                 "\r\n"
                 "[View]\r\n"
                 "  Dither =  floyd-steinberg  \r\n"
                 "# a comment\r\n"
                 "zoom = 200\r\n"
                 "garbage line without equals\r\n"
                 "[other]\r\n"
                 "dither = none\r\n"
                 "unknown = kept\r\n");
    char v[32];
    CHECK(ini.get("view", "dither", v, sizeof(v)) && strcmp(v, "floyd-steinberg") == 0); // case ignored
    CHECK(ini.get("other", "dither", v, sizeof(v)) && strcmp(v, "none") == 0);
    CHECK(ini.get("", "top", v, sizeof(v)) && strcmp(v, "1") == 0);
    CHECK(!ini.get("view", "missing", v, sizeof(v)));
    CHECK(!ini.get("nosuch", "dither", v, sizeof(v)));
    CHECK(ini.get_int("view", "zoom", 100) == 200);
    CHECK(ini.get_int("view", "dither", 7) == 7); // not a number: the default
    CHECK(ini.get_bool("view", "missing", true));

    // Changing a value touches only that value.
    CHECK(ini.set("view", "dither", "sierra-lite"));
    CHECK(strstr(ini.text(), "  Dither =  sierra-lite  \r\n# a comment\r\n") != nullptr);
    CHECK(strstr(ini.text(), "; viewer settings") != nullptr && strstr(ini.text(), "unknown = kept") != nullptr &&
          strstr(ini.text(), "garbage line without equals") != nullptr);
    // New keys go at the end of their section; new sections at the end.
    CHECK(ini.set_bool("view", "smooth", true));
    CHECK(strstr(ini.text(), "garbage line without equals\r\nsmooth = true\r\n[other]") != nullptr);
    CHECK(ini.set_int("new", "n", -3));
    CHECK(ini.get_int("new", "n", 0) == -3);
    CHECK(ini.set("", "top2", "x") && ini.get("", "top2", v, sizeof(v)) && strcmp(v, "x") == 0);
    CHECK(strstr(ini.text(), "top2 = x\r\n[View]") != nullptr);

    // Damaged text: no crash, defaults.
    const char junk[] = "[[[\n=\n==x\n[unterminated\nkey\n\xff\xfe=\x01\n";
    ini.set_text(junk);
    CHECK(!ini.get("view", "dither", v, sizeof(v)));
    CHECK(ini.set("view", "dither", "none") && ini.get("view", "dither", v, sizeof(v)) && strcmp(v, "none") == 0);
    // Too big: refused, text unchanged.
    std::string big(fs::Ini::kMaxText - 10, 'a');
    ini.set_text(big.c_str());
    CHECK(!ini.set("s", "k", "a value too long to fit in what is left"));
    CHECK(strlen(ini.text()) == big.size());

    // Saved through a drive: round trip, and a cut-short save is recovered.
    std::vector<uint8_t> pool(64 * 1024);
    fs::MemoryVolume ram(pool.data(), pool.size(), "RAM disk");
    fs::Storage s;
    s.mount('C', ram);
    s.make_folder("C:\\.config");
    fs::Ini a, b;
    a.set_text("; mine\r\n[view]\r\ndither = bayer4\r\n");
    CHECK(a.save(s, "C:\\.config\\viewer.ini") == FsError::Ok);
    CHECK(a.set("view", "dither", "atkinson") && a.save(s, "C:\\.config\\viewer.ini") == FsError::Ok);
    ports::DirEntry e;
    CHECK(s.stat("C:\\.config\\viewer.ini.tmp", e) == FsError::NotFound); // no leftovers
    CHECK(b.load(s, "C:\\.config\\viewer.ini") && strcmp(b.text(), a.text()) == 0);
    s.remove("C:\\.config\\viewer.ini"); // as if power went between remove and rename
    s.write_file("C:\\.config\\viewer.ini.tmp", a.text(), strlen(a.text()));
    CHECK(b.load(s, "C:\\.config\\viewer.ini") && b.get("view", "dither", v, sizeof(v)) && strcmp(v, "atkinson") == 0);
    CHECK(!b.load(s, "C:\\.config\\none.ini") && !b.get("view", "dither", v, sizeof(v)));
}

static void test_samples()
{
    std::vector<uint8_t> pool(256 * 1024);
    fs::MemoryVolume v(pool.data(), pool.size(), "RAM disk");
    fs::Storage s;
    s.mount('C', v);
    fs::add_samples(s, 'C');
    ports::DirEntry e;
    CHECK(s.stat("C:\\Documents\\notes.txt", e) == FsError::Ok && e.size > 100);
    CHECK(s.stat("C:\\readme.txt", e) == FsError::Ok);
    char buf[128];
    size_t n;
    CHECK(s.read_file("C:\\Documents\\español.txt", buf, sizeof(buf), n) == FsError::Ok);
    CHECK(n > 14 && static_cast<uint8_t>(buf[13]) == 0xF1); // Latin-1 ñ
}

// A save that runs out of disk leaves the old file as it was.
static void test_save_on_a_full_disk()
{
    std::vector<uint8_t> pool(64 * 1024);
    fs::MemoryVolume ram(pool.data(), pool.size(), "RAM disk");
    fs::Storage s;
    s.mount('C', ram);
    std::vector<uint8_t> old(25000, 'o'), bigger(40000, 'n'), back(60000);
    CHECK(s.write_file("C:\\doc.txt", old.data(), old.size()) == FsError::Ok);
    CHECK(s.write_file("C:\\doc.txt", bigger.data(), bigger.size()) == FsError::NoSpace);
    size_t n = 0;
    CHECK(s.read_file("C:\\doc.txt", back.data(), back.size(), n) == FsError::Ok && n == old.size() &&
          memcmp(back.data(), old.data(), n) == 0);
    ports::DirEntry e;
    CHECK(s.entry("C:\\", 1, e) == FsError::NotFound); // the temporary file is gone
    // The price: a save needs room for both copies until its last step.
    CHECK(s.remove("C:\\doc.txt") == FsError::Ok);
    CHECK(s.write_file("C:\\doc.txt", bigger.data(), bigger.size()) == FsError::Ok);

    char t[fs::kMaxPath];
    CHECK(fs::path::temporary("C:\\docs\\a.txt", t, sizeof(t)) && strcmp(t, "C:\\docs\\.~a.txt.tmp") == 0);
    CHECK(fs::path::temporary("C:\\a.txt", t, sizeof(t)) && strcmp(t, "C:\\.~a.txt.tmp") == 0);
    const std::string longest = "C:\\" + std::string(59, 'n') + ".txt"; // 63 characters
    CHECK(fs::path::temporary(longest.c_str(), t, sizeof(t)) && fs::path::valid_name(fs::path::name(t)));
    CHECK(!fs::path::temporary("C:\\", t, sizeof(t)));
}

// A drive that notices two tasks inside it at once. It passes every call on
// to the drive behind it (with none it only answers Ok), and stays inside for
// a moment so that a second task has time to walk in if nothing stops it.
class WatchedVolume : public ports::Volume {
public:
    explicit WatchedVolume(ports::Volume *behind) : behind_(behind) {}

    std::atomic<int> calls{0};
    std::atomic<int> met{0}; // times a task found another one already inside

    FsError info(ports::VolumeInfo &out) override
    {
        const Inside in(*this);
        return behind_ ? behind_->info(out) : FsError::Ok;
    }
    FsError entry(const char *folder, int index, ports::DirEntry &out) override
    {
        const Inside in(*this);
        return behind_ ? behind_->entry(folder, index, out) : FsError::Ok;
    }
    FsError stat(const char *path, ports::DirEntry &out) override
    {
        const Inside in(*this);
        return behind_ ? behind_->stat(path, out) : FsError::Ok;
    }
    FsError make_folder(const char *path) override
    {
        const Inside in(*this);
        return behind_ ? behind_->make_folder(path) : FsError::Ok;
    }
    FsError remove(const char *path) override
    {
        const Inside in(*this);
        return behind_ ? behind_->remove(path) : FsError::Ok;
    }
    FsError rename(const char *from, const char *to) override
    {
        const Inside in(*this);
        return behind_ ? behind_->rename(from, to) : FsError::Ok;
    }
    FsError replace(const char *from, const char *to) override
    {
        const Inside in(*this);
        return behind_ ? behind_->replace(from, to) : FsError::Ok;
    }
    FsError open(const char *path, OpenMode mode, int &file) override
    {
        const Inside in(*this);
        return behind_ ? behind_->open(path, mode, file) : FsError::Ok;
    }
    FsError read(int file, void *data, size_t size, size_t &done) override
    {
        const Inside in(*this);
        return behind_ ? behind_->read(file, data, size, done) : FsError::Ok;
    }
    FsError write(int file, const void *data, size_t size) override
    {
        const Inside in(*this);
        return behind_ ? behind_->write(file, data, size) : FsError::Ok;
    }
    FsError seek(int file, uint32_t offset) override
    {
        const Inside in(*this);
        return behind_ ? behind_->seek(file, offset) : FsError::Ok;
    }
    FsError close(int file) override
    {
        const Inside in(*this);
        return behind_ ? behind_->close(file) : FsError::Ok;
    }

private:
    struct Inside {
        explicit Inside(WatchedVolume &v) : v_(v)
        {
            v_.calls++;
            if (v_.inside_.fetch_add(1) != 0) {
                v_.met++;
            }
            std::this_thread::yield();
        }
        ~Inside() { v_.inside_--; }
        WatchedVolume &v_;
    };

    ports::Volume *behind_;
    std::atomic<int> inside_{0};
};

// Two tasks use the file system at once: each keeps its own folder of files
// and both write one shared file. With the lock no two calls ever meet inside
// the drive, every file reads back as it was written, and nothing is left
// open or lost at the end.
static void test_two_tasks()
{
    std::vector<uint8_t> pool(256 * 1024);
    fs::MemoryVolume ram(pool.data(), pool.size(), "RAM disk");
    WatchedVolume watched(&ram);
    host::ThreadLock lock;
    fs::Storage s;
    s.set_lock(&lock);
    s.mount('C', watched);
    ports::VolumeInfo before = {};
    CHECK(s.info('C', before) == FsError::Ok);

    constexpr int kRounds = 300;
    constexpr size_t kShared = 3000;
    std::atomic<int> wrong{0};
    auto task = [&](char who) {
        auto must = [&](bool ok) {
            if (!ok) {
                wrong++;
            }
        };
        const std::string folder = std::string("C:\\task-") + who;
        const std::string a = folder + "\\a.bin", b = folder + "\\b.bin";
        std::vector<uint8_t> data(5000), back(8000);
        const std::vector<uint8_t> mine(kShared, static_cast<uint8_t>(who));
        must(s.make_folder(folder.c_str()) == FsError::Ok);
        for (int round = 0; round < kRounds; round++) {
            // Its own file, whole: written, read back, renamed, removed.
            const size_t size = 1 + static_cast<size_t>(round * 37 + who) % data.size();
            for (size_t i = 0; i < size; i++) {
                data[i] = static_cast<uint8_t>(who + round + i);
            }
            size_t n = 0;
            must(s.write_file(a.c_str(), data.data(), size) == FsError::Ok);
            must(s.read_file(a.c_str(), back.data(), back.size(), n) == FsError::Ok && n == size &&
                 memcmp(back.data(), data.data(), n) == 0);
            // Piece by piece, the way the Editor saves and reads.
            fs::Save save;
            must(s.begin_save(b.c_str(), save) == FsError::Ok);
            for (size_t at = 0; at < size; at += 700) {
                must(s.write(save.file, data.data() + at, std::min<size_t>(700, size - at)) == FsError::Ok);
            }
            must(s.finish_save(save) == FsError::Ok);
            fs::File f;
            must(s.open(b.c_str(), OpenMode::Read, f) == FsError::Ok);
            size_t total = 0;
            for (;;) {
                must(s.read(f, back.data() + total, 512, n) == FsError::Ok);
                if (n == 0) {
                    break;
                }
                total += n;
            }
            must(s.close(f) == FsError::Ok);
            must(total == size && memcmp(back.data(), data.data(), size) == 0);
            ports::DirEntry e;
            must(s.stat(b.c_str(), e) == FsError::Ok && e.size == size);
            must(s.entry(folder.c_str(), 0, e) == FsError::Ok);
            must(s.remove(a.c_str()) == FsError::Ok);
            must(s.rename(b.c_str(), a.c_str()) == FsError::Ok);
            must(s.remove(a.c_str()) == FsError::Ok);
            // The file both write. A whole write is one step, so neither
            // finds the other's temporary file in use, and what is read is
            // all of one task's bytes, never a mix.
            must(s.write_file("C:\\shared.bin", mine.data(), mine.size()) == FsError::Ok);
            must(s.read_file("C:\\shared.bin", back.data(), back.size(), n) == FsError::Ok && n == kShared);
            bool whole = n == kShared && (back[0] == 'A' || back[0] == 'B');
            for (size_t i = 1; whole && i < n; i++) {
                whole = back[i] == back[0];
            }
            must(whole);
        }
        must(s.remove(folder.c_str()) == FsError::Ok);
    };
    std::thread first(task, 'A'), second(task, 'B');
    first.join();
    second.join();

    CHECK(wrong == 0);
    CHECK(watched.met == 0);
    CHECK(watched.calls > 2 * kRounds * 20);
    // Nothing left behind: the space is back and every file slot is free.
    CHECK(s.remove("C:\\shared.bin") == FsError::Ok);
    ports::VolumeInfo after = {};
    ports::DirEntry e;
    CHECK(s.info('C', after) == FsError::Ok && after.free_bytes == before.free_bytes);
    CHECK(s.entry("C:\\", 0, e) == FsError::NotFound);
    fs::File files[fs::MemoryVolume::kMaxOpen];
    CHECK(s.write_file("C:\\x", "x", 1) == FsError::Ok);
    for (fs::File &f : files) {
        CHECK(s.open("C:\\x", OpenMode::Read, f) == FsError::Ok);
    }
    for (fs::File &f : files) {
        CHECK(s.close(f) == FsError::Ok);
    }
}

// The same watch without a lock does see the two tasks meet, so the test
// above would notice a lock that was missing. (Nothing behind the drive:
// two tasks inside the real RAM disk at once would corrupt it.)
static void test_the_watch_sees_two_tasks()
{
    WatchedVolume watched(nullptr);
    fs::Storage s;
    s.mount('C', watched);
    std::atomic<bool> stop{false};
    auto task = [&] {
        ports::DirEntry e;
        while (!stop) {
            s.stat("C:\\a", e);
        }
    };
    std::thread first(task), second(task);
    for (int ms = 0; ms < 5000 && watched.met == 0; ms++) {
        usleep(1000);
    }
    stop = true;
    first.join();
    second.join();
    CHECK(watched.met > 0);
}

// Change notices: Storage says which folder's list of files changed, once
// the change is made, and says nothing for what only reads or fails.
static void test_change_notices()
{
    static uint8_t arena[64 * 1024];
    fs::MemoryVolume ram(arena, sizeof(arena), "RAM");
    fs::Storage storage;
    storage.mount('C', ram);
    static std::vector<std::string> told;
    told.clear();
    storage.set_changed([](const char *folder, void *) { told.push_back(folder); }, nullptr);
    const auto took = [&](std::initializer_list<const char *> want) {
        const std::vector<std::string> expect(want.begin(), want.end());
        const bool same = told == expect;
        if (!same) {
            for (const std::string &t : told) {
                printf("  told: %s\n", t.c_str());
            }
        }
        told.clear();
        return same;
    };

    CHECK(storage.make_folder("C:\\a") == FsError::Ok && took({"C:\\"}));
    CHECK(storage.make_folder("C:\\a") == FsError::Exists && took({})); // nothing changed
    CHECK(storage.make_folder("C:\\a\\b") == FsError::Ok && took({"C:\\a"}));
    CHECK(storage.write_file("C:\\a\\one.txt", "1", 1) == FsError::Ok && took({"C:\\a"})); // not its temporary file
    // Reading tells nothing.
    char buf[8];
    size_t n = 0;
    ports::DirEntry e;
    int count = 0;
    CHECK(storage.read_file("C:\\a\\one.txt", buf, sizeof(buf), n) == FsError::Ok);
    CHECK(storage.stat("C:\\a\\one.txt", e) == FsError::Ok && storage.entry("C:\\a", 0, e) == FsError::Ok);
    CHECK(storage.entries("C:\\a", 0, &e, 1, count) == FsError::Ok);
    fs::File f;
    CHECK(storage.open("C:\\a\\one.txt", OpenMode::Read, f) == FsError::Ok && storage.close(f) == FsError::Ok);
    CHECK(took({}));
    // A file written piece by piece: when its name appears, and when it is closed.
    CHECK(storage.open("C:\\a\\two.txt", OpenMode::Write, f) == FsError::Ok && took({"C:\\a"}));
    CHECK(storage.write(f, "22", 2) == FsError::Ok && took({}));
    CHECK(storage.close(f) == FsError::Ok && took({"C:\\a"}));
    CHECK(storage.open("C:\\a\\two.txt", OpenMode::Update, f) == FsError::Ok && took({}));
    CHECK(storage.close(f) == FsError::Ok && took({"C:\\a"}));
    // A safe save tells once, when the new file takes the name; never when given up.
    fs::Save save;
    CHECK(storage.begin_save("C:\\a\\b\\s.txt", save) == FsError::Ok && storage.write(save.file, "s", 1) == FsError::Ok);
    CHECK(took({}));
    CHECK(storage.finish_save(save) == FsError::Ok && took({"C:\\a\\b"}));
    CHECK(storage.begin_save("C:\\a\\b\\s.txt", save) == FsError::Ok && storage.finish_save(save, false) == FsError::Ok);
    CHECK(took({}));
    // A rename tells the folder it left and the one it went to; one folder once.
    CHECK(storage.rename("C:\\a\\one.txt", "C:\\a\\b\\one.txt") == FsError::Ok && took({"C:\\a", "C:\\a\\b"}));
    CHECK(storage.rename("C:\\a\\b\\one.txt", "C:\\a\\b\\uno.txt") == FsError::Ok && took({"C:\\a\\b"}));
    CHECK(storage.rename("C:\\a\\b\\uno.txt", "a\\uno.txt") == FsError::Ok && took({"C:\\a\\b", "C:\\a"})); // no drive: the same one
    CHECK(storage.rename("C:\\a\\nothing", "C:\\a\\b\\x") != FsError::Ok && took({}));
    CHECK(storage.replace("C:\\a\\uno.txt", "C:\\a\\b\\s.txt") == FsError::Ok && took({"C:\\a", "C:\\a\\b"}));
    CHECK(storage.remove("C:\\a\\b\\s.txt") == FsError::Ok && took({"C:\\a\\b"}));
    CHECK(storage.remove("C:\\a\\b\\s.txt") == FsError::NotFound && took({}));
    CHECK(storage.remove("C:\\a\\b") == FsError::Ok && took({"C:\\a"}));

    // The key a folder is matched by: the same however the path is written.
    CHECK(fs::path::key("C:\\Documents\\Desktop") == fs::path::key("c:/documents/DESKTOP\\"));
    CHECK(fs::path::key("C:\\") == fs::path::key("C:") && fs::path::key("C:\\a") != fs::path::key("C:\\b"));
    CHECK(fs::path::key("C:\\a") != fs::path::key("D:\\a") && fs::path::key("C:\\a") != fs::path::key("C:\\a\\a"));
}

int main()
{
    test_change_notices();
    test_two_tasks();
    test_the_watch_sees_two_tasks();
    test_save_on_a_full_disk();
    test_drives_behave_alike();
    test_dates();
    test_ini();
    test_paths();
    test_memory_volume();
    test_samples();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
