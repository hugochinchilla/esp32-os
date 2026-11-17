// Chinchilla's command shell in a terminal of the PC: the same sh::Session
// and tty::Tty as on the board, with this terminal as the device.
//
//     chinchilla-console [FOLDER]
//
// FOLDER becomes C: (what is done there is done to the real files). With no
// folder, C: is a RAM disk with the sample files. Keys go to the TTY one at
// a time, so the line editing, the earlier lines and Ctrl+C are Chinchilla's
// own, not the PC's. Text piped in is typed for you:
//
//     printf 'dir\nexit\n' | chinchilla-console
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "core/fs/fs.hpp"
#include "core/fs/memory_volume.hpp"
#include "core/proc/table.hpp"
#include "core/sh/sh.hpp"
#include "core/tty/tty.hpp"
#include "../folder_volume.hpp"
#include "../host_adapters.hpp"

namespace {

// The terminal of the PC as the TTY's device: what the TTY shows goes to
// the standard output as it is (both speak UTF-8).
class StdOut : public ports::ByteSink {
public:
    void write(const uint8_t *data, size_t size) override
    {
        size_t done = 0;
        while (done < size) {
            const ssize_t n = ::write(STDOUT_FILENO, data + done, size - done);
            if (n <= 0) {
                break;
            }
            done += static_cast<size_t>(n);
        }
    }
};

termios g_saved;
bool g_raw = false;

void restore_terminal()
{
    if (g_raw) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
    }
}

} // namespace

int main(int argc, char **argv)
{
    static fs::Storage storage;
    static host::ThreadLock storage_lock;
    storage.set_lock(&storage_lock);
    static std::vector<uint8_t> pool(2u << 20);
    static fs::MemoryVolume ram(pool.data(), pool.size(), "RAM disk");
    if (argc > 1) {
        static host::FolderVolume folder(argv[1], "PC folder");
        storage.mount('C', folder);
    } else {
        storage.mount('C', ram);
        fs::add_samples(storage, 'C');
    }

    // A process table with what would be there, so ps has something to say.
    static proc::Table procs;
    procs.spawn("desktop", proc::kNone, proc::kSystem | proc::kProtected);
    procs.spawn_service("console", proc::kProtected);
    procs.spawn("editor", proc::kRoot, proc::kWindowed);

    static host::HostSystem system;
    system.real_clock = true; // files written from here on get the PC's date
    ram.set_clock(&system);
    static StdOut out;
    static host::ThreadLock tty_lock;
    static host::ThreadEvent typed;
    static tty::Tty terminal(out, &tty_lock, &typed);

    // Keys one at a time and unechoed, Ctrl+C as a key: the TTY does the rest.
    if (isatty(STDIN_FILENO) != 0 && tcgetattr(STDIN_FILENO, &g_saved) == 0) {
        termios raw = g_saved;
        cfmakeraw(&raw);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        g_raw = true;
        atexit(&restore_terminal);
    }

    sh::Env env;
    env.storage = &storage;
    env.procs = &procs; // nothing else touches it here, so the session calls straight in
    env.system = &system;
    static host::HostMemory memory; // ted's text
    env.memory = &memory;
    static sh::Session session(terminal, env);
    std::atomic<bool> over{false};
    std::thread task([&] {
        session.run();
        over = true;
    });

    bool input_open = true;
    while (!over) {
        pollfd waiting = {STDIN_FILENO, POLLIN, 0};
        if (!input_open || poll(&waiting, 1, 50) <= 0) {
            if (!input_open) {
                // Nothing more will come. Ctrl+D again for as long as
                // something still reads: a command that took the first one
                // as the end of its own input (cat | wc), then the session.
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (terminal.readable() == false) {
                    terminal.input("\x04");
                }
            }
            continue;
        }
        uint8_t bytes[256];
        const ssize_t n = read(STDIN_FILENO, bytes, sizeof bytes);
        if (n <= 0) {
            // The end of what was piped in: as Ctrl+D, after its last line.
            terminal.input("\x04");
            input_open = false;
            continue;
        }
        terminal.input(bytes, static_cast<size_t>(n));
    }
    task.join();
    restore_terminal();
    g_raw = false;
    return 0;
}
