// Hello: the first program that is not part of the firmware (backlog item
// 27). It is compiled by itself into a file, hello.app, that the system
// loads from a drive and runs. It uses the same thin classes as the
// built-in programs (core/app/app.hpp) and reaches the system only through
// the OS calls, which the loader joins to it by name when it is loaded.
#include <cstdio>

#include "core/app/app.hpp"

namespace {

// Its picture: a window with a waving hand of four fingers.
constexpr const char *kSmall[16] = {
    "................", ".KKKKKKKKKKKKKK.", ".KBBBBBBBBBBWWK.", ".KKKKKKKKKKKKKK.",
    ".KWWWWWWWWWWWWK.", ".KWWWWYWYWWWWWK.", ".KWWWYWYWYWWWWK.", ".KWWWYWYWYWYWWK.",
    ".KWWWYYYYYWYWWK.", ".KWYWYYYYYYYWWK.", ".KWYYYYYYYYYWWK.", ".KWWYYYYYYYWWWK.",
    ".KWWWYYYYYWWWWK.", ".KWWWWWWWWWWWWK.", ".KKKKKKKKKKKKKK.", "................",
};
constexpr app::Ink kInks[] = {
    {'K', gfx::kBlack},
    {'W', gfx::kWhite},
    {'B', gfx::rgb(0, 0, 2)},
    {'Y', gfx::rgb(7, 5, 0)},
};

class HelloWindow : public app::Window {
public:
    HelloWindow()
        : line_(label({16, 16, 220, 14}, "Hello from a file on the disk.")),
          count_(label({16, 36, 220, 14}, "Pressed 0 times")),
          again_(button({16, 62, 90, 23}, "&Press me")),
          close_(button({130, 62, 75, 23}, "Close"))
    {
        set_default(again_);
    }

    void command(app::Control which) override
    {
        if (which == again_) {
            presses_++;
            snprintf(text_, sizeof(text_), "Pressed %d time%s", presses_, presses_ == 1 ? "" : "s");
            count_.set_text(text_);
        } else if (which == close_) {
            close();
        }
    }

private:
    int presses_ = 0;
    char text_[32] = "";
    app::Label line_, count_;
    app::Button again_, close_;
};

} // namespace

// Where the system comes in: once, as the program's own process. The
// window it opens keeps the program loaded; when the window has closed the
// process ends and the system takes the program out of memory.
APP_INFO(app::many(app::file_info("Hello", 0, kSmall, kInks)));

APP_MAIN(int, char **)
{
    app::Look look;
    look.title = "Hello";
    look.width = 250;
    look.height = 130;
    look.flags = app::kDialog;
    return app::open<HelloWindow>(look) != app::kNone ? 0 : 1;
}
