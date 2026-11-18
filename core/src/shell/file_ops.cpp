// Copy, Cut and Paste for files (backlog item 54), shared by the File
// manager and the desktop. Copy and Cut put full paths on the clipboard;
// Paste copies them into a folder, or moves them there if they were cut,
// on a job of its own: a copy can take seconds, and the desktop goes on.
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "core/os/os.hpp"
#include "core/os/ui.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

constexpr int kMaxDepth = 12; // folders inside folders, as remove_tree

// One Paste: a block of the asker's, made when it starts and given back
// when its job has finished. The job works only with what is in here.
struct Paste {
    int window; // shows the hourglass meanwhile; os::ui::kNoWindow: none
    bool cut;
    int count;
    ports::FsError error;       // the first thing that went wrong: it stops there
    const char *why;            // said instead of the error's own text, if set
    char failed[fs::kMaxPath];  // and on what
    char target[fs::kMaxPath];
    char from[fs::kMaxPath], to[fs::kMaxPath]; // grow and shrink as the copy walks a folder
    uint8_t piece[2048];
    char sources[1]; // `count` full paths, each with its closing zero
};

bool fail(Paste &p, ports::FsError e, const char *on, const char *why = nullptr)
{
    if (p.error == ports::FsError::Ok) {
        p.error = e;
        p.why = why;
        snprintf(p.failed, sizeof(p.failed), "%s", on);
    }
    return false;
}

// `inside` is `folder` or somewhere in it.
bool within(const char *folder, const char *inside)
{
    const size_t n = strlen(folder);
    return strncasecmp(inside, folder, n) == 0 && (inside[n] == 0 || inside[n] == '\\');
}

// Adds "\name" to a path in place; false if it does not fit.
bool grow(char *path, const char *name)
{
    const size_t length = strlen(path);
    const bool separator = length > 0 && path[length - 1] != '\\';
    return snprintf(path + length, fs::kMaxPath - length, "%s%s", separator ? "\\" : "", name) <
           static_cast<int>(fs::kMaxPath - length);
}

// p.from to p.to, the safe way: the copy takes its name when it is whole.
bool copy_file(const job::Job &job, Paste &p)
{
    os::file::Handle in = os::file::kNoFile, save = os::file::kNoFile;
    ports::FsError e = os::file::open(p.from, ports::OpenMode::Read, in, &job);
    if (e != ports::FsError::Ok) {
        return fail(p, e, p.from);
    }
    e = os::file::begin_save(p.to, save, &job);
    const char *on = p.to;
    while (e == ports::FsError::Ok) {
        if (job.stopping()) {
            e = ports::FsError::Io;
            break;
        }
        size_t got = 0;
        e = os::file::read(in, p.piece, sizeof(p.piece), got);
        if (e != ports::FsError::Ok) {
            on = p.from;
            break;
        }
        if (got == 0) {
            break;
        }
        e = os::file::write(save, p.piece, got);
    }
    os::file::close(in);
    if (save != os::file::kNoFile) {
        const ports::FsError closed = os::file::finish_save(save, e == ports::FsError::Ok);
        e = e != ports::FsError::Ok ? e : closed;
    }
    return e == ports::FsError::Ok || fail(p, e, on);
}

// The folder p.from and all in it to p.to, which is not there yet.
bool copy_tree(const job::Job &job, Paste &p, int depth)
{
    if (depth > kMaxDepth) {
        return fail(p, ports::FsError::NotEmpty, p.from, "its folders go too deep");
    }
    ports::FsError e = os::file::make_folder(p.to);
    if (e != ports::FsError::Ok) {
        return fail(p, e, p.to);
    }
    const size_t from_length = strlen(p.from), to_length = strlen(p.to);
    ports::DirEntry entry;
    bool ok = true;
    for (int i = 0; ok && os::file::entry(p.from, i, entry) == ports::FsError::Ok; i++) {
        if (!grow(p.from, entry.name) || !grow(p.to, entry.name)) {
            ok = fail(p, ports::FsError::BadName, entry.name, "its path is too long");
        } else {
            ok = entry.folder ? copy_tree(job, p, depth + 1) : copy_file(job, p);
        }
        p.from[from_length] = 0;
        p.to[to_length] = 0;
    }
    return ok;
}

// One of the clipboard's entries into the target folder.
bool paste_one(const job::Job &job, Paste &p, const char *source)
{
    ports::DirEntry e;
    const ports::FsError found = os::file::stat(source, e);
    if (found != ports::FsError::Ok) {
        return fail(p, found, source);
    }
    char parent[fs::kMaxPath], name[fs::kMaxPath];
    fs::path::parent(source, parent, sizeof(parent));
    const bool same_folder = strcasecmp(parent, p.target) == 0;
    if (p.cut && same_folder) {
        return true; // it is there already
    }
    if (e.folder && within(source, p.target)) {
        return fail(p, ports::FsError::BadName, source, "a folder cannot be put inside itself");
    }
    // A name taken there is numbered, "notes (2).txt": nothing is replaced.
    if (!free_name_in(p.target, fs::path::name(source), e.folder, name, sizeof(name)) ||
        !fs::path::join(p.target, name, p.to, sizeof(p.to))) {
        return fail(p, ports::FsError::BadName, source, "its path is too long");
    }
    snprintf(p.from, sizeof(p.from), "%s", source);
    if (p.cut && fs::path::drive(source) == fs::path::drive(p.target)) {
        const ports::FsError moved = os::file::rename(p.from, p.to);
        return moved == ports::FsError::Ok || fail(p, moved, source);
    }
    if (!(e.folder ? copy_tree(job, p, 0) : copy_file(job, p))) {
        return false;
    }
    if (p.cut) { // to another drive: copied whole, now the old one goes
        const ports::FsError gone = remove_tree(source);
        return gone == ports::FsError::Ok || fail(p, gone, source);
    }
    return true;
}

// The job: on its own task where there are tasks.
uint32_t paste_work(const job::Job &job, void *context)
{
    Paste &p = *static_cast<Paste *>(context);
    const char *source = p.sources;
    for (int i = 0; i < p.count && !job.stopping(); i++, source += strlen(source) + 1) {
        if (!paste_one(job, p, source)) {
            break;
        }
    }
    return 0;
}

// What is said when it went wrong, and the block given back.
void finish(Paste *p, bool stopped)
{
    static char text[fs::kMaxPath + 120];
    const bool failed = p->error != ports::FsError::Ok && !stopped;
    if (failed) {
        snprintf(text, sizeof(text), "Cannot %s '%s':\n%s.", p->cut ? "move" : "copy", fs::path::name(p->failed),
                 p->why != nullptr ? p->why : fs::error_text(p->error));
    }
    if (p->window != os::ui::kNoWindow) {
        os::ui::set_hourglass(p->window, false); // a window that has closed is simply not there
    }
    // What was cut has moved (or what is left of it is no longer what the
    // clipboard says): it is pasted once.
    if (p->cut && os::clipboard::files_cut()) {
        os::clipboard::clear();
    }
    os::release(p);
    files_changed();
    if (failed) {
        message_box("Paste", text);
    }
}

// Back where the asker's events run.
void pasted(void *context, uint32_t, bool stopped) { finish(static_cast<Paste *>(context), stopped); }

} // namespace

void copy_files(const Selection &s, bool cut)
{
    if (s.count < 1 || s.folder == nullptr || s.folder[0] == 0 || in_trash(s.folder)) {
        return;
    }
    if (!os::clipboard::set_files(s.folder, s.names, s.count, cut)) {
        message_box(cut ? "Cut" : "Copy", "These are too many to put on the clipboard at once.");
    }
}

bool can_paste(const char *folder)
{
    return folder != nullptr && folder[0] != 0 && !in_trash(folder) && os::clipboard::file_count() > 0;
}

void paste_files(const char *folder, int window)
{
    if (!can_paste(folder)) {
        return;
    }
    const int count = os::clipboard::file_count();
    char path[fs::kMaxPath];
    size_t bytes = 0;
    for (int i = 0; i < count; i++) {
        os::clipboard::file(i, path, sizeof(path));
        bytes += strlen(path) + 1;
    }
    Paste *p = static_cast<Paste *>(os::allocate(sizeof(Paste) + bytes));
    if (p == nullptr) {
        message_box("Paste", "There is not enough memory to paste.");
        return;
    }
    p->window = window;
    p->cut = os::clipboard::files_cut();
    p->count = count;
    p->error = ports::FsError::Ok;
    snprintf(p->target, sizeof(p->target), "%s", folder);
    char *at = p->sources;
    for (int i = 0; i < count; i++) {
        os::clipboard::file(i, path, sizeof(path));
        const size_t n = strlen(path) + 1;
        memcpy(at, path, n);
        at += n;
    }
    if (window != os::ui::kNoWindow) {
        os::ui::set_hourglass(window, true);
    }
    job::Spec spec;
    spec.name = "paste";
    spec.work = &paste_work;
    spec.done = &pasted;
    spec.context = p;
    spec.stack_bytes = 10240; // the file system's calls under a walk kMaxDepth folders deep
    // Where there are no tasks the work, and `pasted`, are over when this
    // returns: nothing of `p` is used after it.
    if (!os::start_job(spec)) {
        if (window != os::ui::kNoWindow) {
            os::ui::set_hourglass(window, false);
        }
        os::release(p);
        message_box("Paste", "Too much is going on to paste now. Try again in a moment.");
    }
}

} // namespace shell
