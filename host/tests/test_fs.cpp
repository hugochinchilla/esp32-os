// Tests for drives, paths and the RAM disk.
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "core/fs/fs.hpp"
#include "core/fs/memory_volume.hpp"

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

static void test_samples()
{
    std::vector<uint8_t> pool(256 * 1024);
    fs::MemoryVolume v(pool.data(), pool.size(), "RAM disk");
    fs::Storage s;
    s.mount('C', v);
    fs::add_samples(s, 'C');
    ports::DirEntry e;
    CHECK(s.stat("C:\\docs\\notes.txt", e) == FsError::Ok && e.size > 100);
    CHECK(s.stat("C:\\readme.txt", e) == FsError::Ok);
    char buf[128];
    size_t n;
    CHECK(s.read_file("C:\\docs\\español.txt", buf, sizeof(buf), n) == FsError::Ok);
    CHECK(n > 14 && static_cast<uint8_t>(buf[13]) == 0xF1); // Latin-1 ñ
}

int main()
{
    test_paths();
    test_memory_volume();
    test_samples();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
