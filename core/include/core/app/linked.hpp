// Programs linked into the system's own binary (the web page, the PC's
// tests): the Loader port for a machine that cannot load code from a file.
//
// The programs of programs/ are the same source everywhere. On the board
// and in the Linux program each is compiled by itself into a `.app` file
// that the machine's loader joins to the system. Where there is no such
// loader, the build compiles them into the system with APP_LINKED set to
// the file's name: APP_MAIN and APP_INFO (core/app/app.hpp) then put the
// program on the list kept here. C:\Programs gets a file for each all the
// same: its header, as on the board, and after it the name this loader
// finds the code by. So the desktop, the Start menu and the File manager
// see the same files on every machine, and only this driver differs.
#pragma once

#include "core/app/file.hpp"
#include "core/fs/fs.hpp"
#include "core/ports.hpp"

namespace app {

struct Linked {
    const char *file;       // "chess": C:\Programs\chess.app
    const FileInfo *info;   // what the file begins with
    int (*main)(int, char **);
};
// What APP_MAIN does in a linked program, before main().
void add_linked(const Linked &program);
int linked_count();
const Linked &linked(int index);

// The Loader that finds a file's code on the list.
ports::Loader &linked_loader();
// Writes a file for each program on the list to `folder`, where it is
// missing or not this build's. False if one could not be written.
bool install_linked(fs::Storage &storage, const char *folder);

} // namespace app
