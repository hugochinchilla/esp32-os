// The Services settings: which network services start with the machine.
// Opened from Start > Settings (docs/journal/2026-11-18-services.md).
//
// It changes two lines of init's start-up file, C:\.config\startup.txt
// (core/init/init.hpp): telnetd and vncd, `once` for a ticked box and `off`
// for an empty one. Every other line of the file, and its comments, stay as
// they are. init reads the file when the machine starts, so a change
// applies from the next start; `service NAME stop` and `service NAME start`
// in a terminal are for now.
//
// A program of its own: nothing is kept while it is closed, the file has
// it all.
#include <cstdio>
#include <cstring>

#include "core/app/app.hpp"
#include "core/gfx/art.hpp"
#include "core/init/init.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

constexpr const char *kTitle = "Services";
constexpr int kServices = 2;
constexpr const char *kNames[kServices] = {"telnetd", "vncd"};
// Room for the file as init reads it, and for two new lines.
constexpr size_t kRoom = init::Init::kMaxFile + 64;

// The start-up file's text, in memory asked for while it is needed. Without
// a file it is the usual text, which is what init went by.
struct File {
    File() : text(static_cast<char *>(os::allocate(kRoom))) {}
    ~File() { os::release(text); }
    File(const File &) = delete;
    File &operator=(const File &) = delete;

    // False: no memory, or a file that cannot be read or is too long.
    bool load()
    {
        if (text == nullptr) {
            return false;
        }
        const os::file::Error read = os::file::read_file(init::kPath, text, kRoom, size);
        if (read == os::file::Error::NotFound) {
            const char *usual = init::Init::default_text();
            size = strlen(usual);
            memcpy(text, usual, size);
            return true;
        }
        return read == os::file::Error::Ok && size <= init::Init::kMaxFile;
    }

    // Started with the machine: its line says anything but off. No line:
    // not started.
    bool starts(const char *name) const
    {
        char action[16];
        return init::line_action(text, size, name, action, sizeof(action)) && strcmp(action, "off") != 0 &&
               strcmp(action, "OFF") != 0 && strcmp(action, "Off") != 0;
    }

    bool set(const char *name, bool on)
    {
        const size_t now = init::set_line_action(text, size, init::Init::kMaxFile, name, on ? "once" : "off");
        if (now == 0) {
            return false;
        }
        size = now;
        return true;
    }

    bool save() const
    {
        os::file::make_folder("C:\\.config"); // already there is fine
        return os::file::write_file(init::kPath, text, size) == os::file::Error::Ok;
    }

    char *text;
    size_t size = 0;
};

class ServicesDialog : public app::Window {
public:
    ServicesDialog()
        : group_(group({8, 6, 344, 96}, "Started with the machine")),
          telnet_(check_box({18, 26, 326, 16}, "&Telnet: the command shell (port 23)")),
          vnc_(check_box({18, 48, 326, 16}, "&Remote screen: VNC (port 5900) and web (port 80)")),
          warning_(label({18, 74, 326, 14}, "No passwords: for a network you trust.")),
          note_(label({10, 110, 340, 14}, note_text_)),
          ok_(button({194, 134, 75, 23}, "OK")),
          cancel_(button({275, 134, 75, 23}, "Cancel"))
    {
        set_default(ok_);
        File file;
        readable_ = file.load();
        if (readable_) {
            was_[0] = file.starts(kNames[0]);
            was_[1] = file.starts(kNames[1]);
        } else {
            snprintf(note_text_, sizeof(note_text_), "The start-up file cannot be read.");
            ok_.set_enabled(false);
        }
        telnet_.set_checked(was_[0]);
        vnc_.set_checked(was_[1]);
        set_focus(telnet_);
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt) && k.usage == wm::key::A + ('t' - 'a')) {
            telnet_.set_checked(!telnet_.checked());
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt) && k.usage == wm::key::A + ('r' - 'a')) {
            vnc_.set_checked(!vnc_.checked());
            return true;
        }
        return false;
    }

    void command(app::Control which) override
    {
        if (which == cancel_) {
            close();
        } else if (which == ok_) {
            if (save()) {
                close();
            } else {
                message_box(kTitle, "The start-up file could not be changed.");
            }
        }
    }

private:
    // Only a box that was changed changes its line: one the file has as
    // respawn, or in some other hand's words, stays as it is.
    bool save()
    {
        const bool now[kServices] = {telnet_.checked(), vnc_.checked()};
        if (now[0] == was_[0] && now[1] == was_[1]) {
            return true;
        }
        // The file as it is now, not as it was when the window opened.
        File file;
        if (!file.load()) {
            return false;
        }
        for (int i = 0; i < kServices; i++) {
            if (now[i] != was_[i] && !file.set(kNames[i], now[i])) {
                return false;
            }
        }
        return file.save();
    }

    bool readable_ = false;
    char note_text_[56] = "Changes apply the next time the machine starts.";
    bool was_[kServices] = {false, false}; // what the file said when the window opened
    app::GroupFrame group_;
    app::CheckBox telnet_, vnc_;
    app::Label warning_, note_;
    app::Button ok_, cancel_;
};

app::Number g_dialog = app::kNone; // the one there may be: a number, dead once it has closed

} // namespace

void services_dialog()
{
    const AsProgram as(kTitle);
    if (app::bring_forward(g_dialog)) {
        return;
    }
    app::Look look;
    look.title = kTitle;
    look.width = 366;
    look.height = 196;
    look.icon = &gfx::services_icon_16();
    look.flags = app::kDialog;
    g_dialog = app::open<ServicesDialog>(look);
    if (g_dialog == app::kNone) {
        no_memory_for(kTitle);
    }
}

} // namespace shell
