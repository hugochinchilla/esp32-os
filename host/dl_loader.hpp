// The Loader port on the PC, for the tests: the program is a shared library
// of the PC's own kind and the PC's dynamic loader joins it to the test
// program, whose OS calls it finds by name as the board's loader does.
#pragma once

#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>

#include "core/ports.hpp"

namespace host {

class DlLoader : public ports::Loader {
public:
    void *load(const uint8_t *image, size_t size, const char *&why) override
    {
        // The loader of the PC wants a file: one that is only in memory.
        const int fd = memfd_create("program", 0);
        if (fd < 0 || write(fd, image, size) != static_cast<ssize_t>(size)) {
            why = "there is not enough memory";
            if (fd >= 0) {
                close(fd);
            }
            return nullptr;
        }
        char path[64];
        snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
        void *module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        close(fd);
        if (module == nullptr) {
            snprintf(last_error, sizeof(last_error), "%s", dlerror());
            why = "it is not a program for this machine";
            return nullptr;
        }
        if (dlsym(module, "app_main") == nullptr) {
            dlclose(module);
            why = "it has no way in";
            return nullptr;
        }
        loaded++;
        bytes_ = size;
        return module;
    }
    int enter(void *module) override
    {
        auto *way_in = reinterpret_cast<int (*)(int, char **)>(dlsym(module, "app_main"));
        return way_in(0, nullptr);
    }
    // The PC's loader does not say: the size of the file stands in.
    size_t bytes(void *) override { return bytes_; }
    void unload(void *module) override
    {
        dlclose(module);
        loaded--;
    }

    size_t bytes_ = 0;
    int loaded = 0; // programs in memory now
    char last_error[256] = "";
};

} // namespace host
