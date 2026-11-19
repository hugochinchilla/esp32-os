// Programs linked into the system's own binary: core/app/linked.hpp.
#include "core/app/linked.hpp"

#include <cstdio>
#include <cstring>

namespace app {

namespace {

constexpr int kMaxLinked = 32;
// Filled before main() by the programs' own static objects, so it must not
// need a constructor itself.
Linked g_linked[kMaxLinked];
int g_count = 0;

// What follows the header in a linked program's file.
constexpr char kMark[] = "linked:";
constexpr size_t kMarkSize = sizeof(kMark) - 1;

class LinkedLoader final : public ports::Loader {
public:
    void *load(const uint8_t *image, size_t size, const char *&why) override
    {
        if (size > kMarkSize && memcmp(image, kMark, kMarkSize) == 0) {
            const char *name = reinterpret_cast<const char *>(image) + kMarkSize;
            const size_t length = size - kMarkSize;
            for (int i = 0; i < g_count; i++) {
                if (strlen(g_linked[i].file) == length && memcmp(g_linked[i].file, name, length) == 0) {
                    return &g_linked[i];
                }
            }
        }
        why = "it is not a program for this machine";
        return nullptr;
    }
    int enter(void *module, int argc, char **argv) override
    {
        return static_cast<Linked *>(module)->main(argc, argv);
    }
    size_t bytes(void *) override { return 0; } // its code is the system's own
    void unload(void *) override {}
};

LinkedLoader g_loader;

} // namespace

void add_linked(const Linked &program)
{
    if (g_count < kMaxLinked) {
        g_linked[g_count++] = program;
    }
}

int linked_count() { return g_count; }

const Linked &linked(int index) { return g_linked[index]; }

ports::Loader &linked_loader() { return g_loader; }

bool install_linked(fs::Storage &storage, const char *folder)
{
    if (g_count == 0) {
        return true;
    }
    storage.make_folder(folder); // there already: fine
    bool all = true;
    // One file at a time, in memory of the system's own: this runs once, at
    // start-up, before any program.
    static uint8_t image[sizeof(FileInfo) + kMarkSize + 64];
    static uint8_t there[sizeof(image)];
    for (int i = 0; i < g_count; i++) {
        const Linked &p = g_linked[i];
        const size_t name = strlen(p.file);
        if (name > 64) {
            all = false;
            continue;
        }
        memcpy(image, p.info, sizeof(FileInfo));
        memcpy(image + sizeof(FileInfo), kMark, kMarkSize);
        memcpy(image + sizeof(FileInfo) + kMarkSize, p.file, name);
        const size_t size = sizeof(FileInfo) + kMarkSize + name;
        char path[fs::kMaxPath];
        snprintf(path, sizeof(path), "%s\\%s.app", folder, p.file);
        size_t got = 0;
        if (storage.read_file(path, there, sizeof(there), got) == ports::FsError::Ok && got == size &&
            memcmp(there, image, size) == 0) {
            continue;
        }
        all = storage.write_file(path, image, size) == ports::FsError::Ok && all;
    }
    return all;
}

} // namespace app
