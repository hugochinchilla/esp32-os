// The shell's commands, for the shell's own files.
#pragma once

#include "core/sh/sh.hpp"

namespace sh {

using CommandFn = int (*)(Session &s, int argc, char **argv);

struct Command {
    const char *name;    // what is typed, lower case
    CommandFn run;
    const char *usage;   // nullptr: another name for the command before it
    const char *summary; // never nullptr ("" for another name)
};

extern const Command kCommands[];
extern const int kCommandCount;

// Letters after a '-' in the arguments before the first that is not an
// option ("-la", "-r"). They are taken out of argv; returns false, having
// said why, on a letter that is not in `allowed`.
bool options(Session &s, int &argc, char **argv, const char *allowed, char *found, int capacity);
bool has(const char *found, char letter);

// 1234567 as "1,234,567".
const char *grouped(uint64_t value, char *out, int capacity);

// files.cpp
int cmd_ls(Session &s, int argc, char **argv);
int cmd_dir(Session &s, int argc, char **argv);
int cmd_cd(Session &s, int argc, char **argv);
int cmd_pwd(Session &s, int argc, char **argv);
int cmd_cat(Session &s, int argc, char **argv);
int cmd_cp(Session &s, int argc, char **argv);
int cmd_mv(Session &s, int argc, char **argv);
int cmd_rm(Session &s, int argc, char **argv);
int cmd_mkdir(Session &s, int argc, char **argv);
int cmd_rmdir(Session &s, int argc, char **argv);
int cmd_touch(Session &s, int argc, char **argv);
int cmd_df(Session &s, int argc, char **argv);

// filters.cpp
int copy_input(Session &s); // cat without a file
int cmd_grep(Session &s, int argc, char **argv);
int cmd_wc(Session &s, int argc, char **argv);
int cmd_head(Session &s, int argc, char **argv);
int cmd_tail(Session &s, int argc, char **argv);
int cmd_more(Session &s, int argc, char **argv);

// system.cpp
int cmd_ps(Session &s, int argc, char **argv);
int cmd_kill(Session &s, int argc, char **argv);
int cmd_free(Session &s, int argc, char **argv);
int cmd_uptime(Session &s, int argc, char **argv);
int cmd_date(Session &s, int argc, char **argv);
int cmd_dmesg(Session &s, int argc, char **argv);
int cmd_ver(Session &s, int argc, char **argv);
int cmd_top(Session &s, int argc, char **argv);
int cmd_open(Session &s, int argc, char **argv);
int cmd_run(Session &s, int argc, char **argv);
int cmd_edit(Session &s, int argc, char **argv);

// ted.cpp
int cmd_ted(Session &s, int argc, char **argv);

} // namespace sh
