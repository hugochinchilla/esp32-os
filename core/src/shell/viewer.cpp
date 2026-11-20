// The Picture viewer: BMP, PNG, JPEG and GIF, decoded in slices from a timer so
// the desktop stays responsive and the picture appears as it is decoded. An
// animated GIF plays: the file stays open and the same timer decodes each
// picture when its time comes.
#include <cstdio>
#include <cstring>

#include "core/app/app.hpp"
#include "core/gfx/art.hpp"
#include "core/image/convert.hpp"
#include "core/image/image.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

constexpr int kMaxShown = 1600 * 1200;      // actual size up to this many pixels
constexpr uint32_t kSliceUs = 8000;          // decoding time per timer tick
constexpr uint32_t kTickUs = 15000;
// The most working memory taken to play an animation: a screen of 800x600.
const size_t kMaxAnimationPool = image::animation_pool(800, 600);
constexpr uint64_t kReportUs = 5000000; // into an animation, one line in the system's log
constexpr gfx::Colour kBackground = gfx::kDarkGrey;
constexpr uint8_t kBackgroundRgb[3] = {73, 73, 73}; // shows through transparent PNGs and GIFs

const char *const *dither_names()
{
    static const char *names[image::kDitherCount];
    for (int i = 0; i < image::kDitherCount; i++) {
        names[i] = image::dither_info(static_cast<image::Dither>(i)).name;
    }
    return names;
}

// View > Options: the dithering, from a list with a line about each.
class OptionsDialog : public app::Window {
public:
    using Done = void (*)(image::Dither d);

    OptionsDialog(image::Dither current, Done done)
        : done_(done),
          group_(group({8, 6, 358, 180}, "Dithering")),
          list_(list({18, 24, 338, 116}, dither_names(), image::kDitherCount)),
          about_(label({18, 150, 338, 14}, "")),
          ok_(button({210, 196, 75, 23}, "OK")),
          cancel_(button({291, 196, 75, 23}, "Cancel"))
    {
        set_default(ok_);
        list_.select(static_cast<int>(current));
        describe();
        set_focus(list_);
    }

    void command(app::Control which) override
    {
        const int i = list_.selected();
        const Done done = done_;
        close();
        if (which == ok_ && i >= 0 && done != nullptr) {
            done(static_cast<image::Dither>(i));
        }
    }
    void changed(app::Control) override { describe(); }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return false;
    }

private:
    void describe()
    {
        if (list_.selected() >= 0) {
            about_.set_text(image::dither_info(static_cast<image::Dither>(list_.selected())).description);
        }
    }

    Done done_;
    app::GroupFrame group_;
    app::ListBox list_;
    app::Label about_;
    app::Button ok_, cancel_;
};

const image::Dither kDithers[image::kDitherCount] = {
    image::Dither::None,           image::Dither::Bayer2,          image::Dither::Bayer4,
    image::Dither::Bayer8,         image::Dither::BlueNoise,       image::Dither::FloydSteinberg,
    image::Dither::FloydSerpentine, image::Dither::Atkinson,       image::Dither::JarvisJudiceNinke,
    image::Dither::Stucki,         image::Dither::SierraLite,
};

void set_dither(image::Dither d);

// Timings of the last picture, kept after its window has gone (see
// picture_timings()).
uint32_t g_timings[kPictureTimings] = {};

class ViewerWindow : public app::Window {
public:
    static constexpr int kStatusH = 20;
    static constexpr int kBar = os::ui::kScrollBarThickness;

    // The picture area: the converted picture, centred, with scroll bars
    // when it is larger than the area (actual size); under it, a line of
    // status.
    ViewerWindow()
        : view_(picture({0, 0, 10, 10}, app::kOnce)), // the picture, painted once already
          vbar_(scroll_bar({0, 0, 0, 0}, true)),
          hbar_(scroll_bar({0, 0, 0, 0}, false)),
          status_(picture({0, 0, 10, kStatusH}, app::kOnce))
    {
        file_items_[0] = {"&Open...", &ViewerWindow::menu_open, this, nullptr, nullptr, 0};
        file_items_[1] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[2] = {"E&xit", &ViewerWindow::menu_exit, this, nullptr, nullptr, 0};
        view_items_[0] = {"&Fit to window", &ViewerWindow::menu_fit, this, nullptr, nullptr, 0};
        view_items_[1] = {"&Actual size", &ViewerWindow::menu_actual, this, nullptr, nullptr, 0};
        view_items_[2] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        view_items_[3] = {"&Dithering", nullptr, nullptr, &dither_menu_, nullptr, 0};
        view_items_[4] = {"&Options...", &ViewerWindow::menu_options, this, nullptr, nullptr, 0};
        for (int i = 0; i < image::kDitherCount; i++) {
            dither_items_[i] = {image::dither_info(kDithers[i]).menu, &ViewerWindow::menu_dither,
                                const_cast<image::Dither *>(&kDithers[i]), nullptr, nullptr, 0};
        }
        set_menu_bar(&bar_);
        const gfx::Rect c = client_size();
        resized(c.w, c.h);
    }

    void open_path(const char *path)
    {
        if (!memory_ready()) {
            close(); // no memory for the picture: no window either
            return;
        }
        if (!settings_read_) {
            settings_read_ = true;
            load_settings("viewer", settings_);
            char id[32];
            if (settings_.get("view", "dither", id, sizeof(id))) {
                for (int i = 0; i < image::kDitherCount; i++) {
                    if (strcmp(id, image::dither_info(static_cast<image::Dither>(i)).id) == 0) {
                        dither_ = static_cast<image::Dither>(i);
                    }
                }
            }
        }
        if (path == nullptr) {
            open_file_dialog(number(), "C:\\pictures", &ViewerWindow::file_chosen, this);
            return;
        }
        snprintf(path_, sizeof(path_), "%s", path);
        start();
    }

    void resized(int w, int h) override
    {
        width_ = w;
        height_ = h - kStatusH;
        status_.place({0, h - kStatusH, w, kStatusH});
        layout();
        // Fitting depends on the size: decode again for the new size.
        if (fit_ && path_[0] != 0 && on_screen() && (w != last_w_ || h != last_h_)) {
            start();
        }
        last_w_ = w;
        last_h_ = h;
    }

    void changed(app::Control) override { view_.redraw(); } // a scroll bar moved

    void paint_in(app::Control which, app::Canvas &c) override
    {
        if (which == status_) {
            const gfx::Rect r = {0, 0, width_, kStatusH};
            c.dent(r);
            const int ty = (kStatusH - 12) / 2;
            c.fill_around(r.inset(1), {4, ty, app::Canvas::text_width(status_text_), 12}, gfx::face());
            c.text_opaque(4, ty, status_text_, gfx::ink(), gfx::face());
            return;
        }
        // The ready rows of the picture, and the background around them,
        // each pixel once.
        const gfx::Rect a = area();
        const gfx::Rect at = picture_rect();
        const gfx::Rect shown = gfx::Rect(at.x, at.y, at.w, ready_).intersect(a);
        if (pixels_ != nullptr && !shown.empty()) {
            const gfx::Bitmap b = {w_, ready_, pixels_, -1};
            c.blit(b, at.x, at.y);
        }
        c.fill_around(a, shown, kBackground);
    }

    void menu_opening(int entry) override
    {
        if (entry != 1) {
            return;
        }
        view_items_[0].flags = fit_ ? wm::kMenuChecked : 0;
        view_items_[1].flags = fit_ ? 0 : wm::kMenuChecked;
        for (int i = 0; i < image::kDitherCount; i++) {
            dither_items_[i].flags = dither_ == kDithers[i] ? wm::kMenuChecked : 0;
        }
    }

    void closed() override { stop(); }

    // The picture and the working memory go back with the window.
    ~ViewerWindow() override
    {
        decoder_.release(program_port());
        converter_.release(program_port());
        free_program_memory(shown_);
    }

    // For tests and the debug link: decodes to the end at once (of an
    // animation, to the end of its first picture).
    void finish()
    {
        while (timer_ != 0 && !playing_) {
            slice(this);
        }
    }

private:
    // What the picture area shows: the pixels, and how many rows are ready.
    void show(const uint8_t *pixels, int w, int h)
    {
        pixels_ = pixels;
        w_ = w;
        h_ = h;
        ready_ = 0;
        vbar_.set_value(0);
        hbar_.set_value(0);
        layout();
        view_.redraw();
    }

    // Rows [from, to) became ready: repaint them only.
    void rows_ready(int from, int to)
    {
        ready_ = to;
        const gfx::Rect at = picture_rect();
        view_.redraw(gfx::Rect(at.x, at.y + from, at.w, to - from).intersect(area()));
    }

    // Where the picture area and the scroll bars go: a bar only where the
    // picture is larger than the area.
    void layout()
    {
        need_h_ = w_ > width_;
        need_v_ = h_ > height_ - (need_h_ ? kBar : 0);
        need_h_ = w_ > width_ - (need_v_ ? kBar : 0);
        const gfx::Rect a = area();
        view_.place(a);
        vbar_.place(need_v_ ? gfx::Rect{a.right(), 0, kBar, a.h} : gfx::Rect{0, 0, 0, 0});
        hbar_.place(need_h_ ? gfx::Rect{0, a.bottom(), a.w, kBar} : gfx::Rect{0, 0, 0, 0});
        vbar_.set_range(h_ > a.h ? h_ - a.h : 0, a.h);
        hbar_.set_range(w_ > a.w ? w_ - a.w : 0, a.w);
    }

    gfx::Rect area() const { return {0, 0, width_ - (need_v_ ? kBar : 0), height_ - (need_h_ ? kBar : 0)}; }

    gfx::Rect picture_rect() const
    {
        const gfx::Rect a = area();
        const int x = w_ <= a.w ? (a.w - w_) / 2 : -hbar_.value();
        const int y = h_ <= a.h ? (a.h - h_) / 2 : -vbar_.value();
        return {x, y, w_, h_};
    }

    void set_status(const char *text)
    {
        snprintf(status_text_, sizeof(status_text_), "%s", text);
        status_.redraw();
    }

    bool memory_ready()
    {
        if (shown_ == nullptr) {
            shown_ = static_cast<uint8_t *>(program_memory(kMaxShown));
        }
        if (shown_ == nullptr || !converter_.ready(program_port())) {
            message_box("Picture viewer", "There is not enough memory for the Picture viewer.");
            return false;
        }
        return true;
    }

    void stop()
    {
        if (timer_ != 0) {
            os::stop_timer(timer_);
            timer_ = 0;
        }
        playing_ = false;
        decoder_.close();
        if (source_.file != os::file::kNoFile) {
            os::file::close(source_.file);
            source_.file = os::file::kNoFile;
        }
    }

    void start()
    {
        stop();
        // The file is this program's, held while the picture is decoded a
        // piece at a time; the decoder only reads from it.
        decoder_.use_pool(program_port(), image::kPoolBytes);
        image::Error e = os::file::open(path_, ports::OpenMode::Read, source_.file) == ports::FsError::Ok
                             ? decoder_.open(source_, program_port(), info_, kBackgroundRgb)
                             : image::Error::NotFound;
        // An animation too large for the usual working memory: more of it,
        // while this picture is open, if that is not asking too much.
        const size_t pool = image::animation_pool(info_.width, info_.height);
        if (e == image::Error::None && info_.several && !info_.animated && pool <= kMaxAnimationPool) {
            decoder_.use_pool(program_port(), pool);
            image::Info again;
            if (decoder_.open(source_, program_port(), again, kBackgroundRgb) == image::Error::None) {
                info_ = again;
            } else { // no memory for it: the first picture, still
                decoder_.use_pool(program_port(), image::kPoolBytes);
                e = decoder_.open(source_, program_port(), info_, kBackgroundRgb);
            }
        }
        snprintf(title_, sizeof(title_), "%s - Picture viewer", fs::path::name(path_));
        set_title(title_);
        if (e != image::Error::None) {
            show(nullptr, 0, 0);
            set_status(fs::path::name(path_));
            snprintf(message_, sizeof(message_), "Cannot show %s:\n%s.", fs::path::name(path_), image::error_text(e));
            message_box("Picture viewer", message_);
            return;
        }
        // The size shown: fitted inside the area (never enlarged), or the
        // real size if it fits in memory.
        int w = info_.width, h = info_.height;
        const int aw = width_, ah = height_;
        bool fit = fit_ || static_cast<long>(w) * h > kMaxShown || w > image::Converter::kMaxWidth;
        if (fit && (w > aw || h > ah)) {
            if (static_cast<long>(w) * ah > static_cast<long>(h) * aw) {
                h = static_cast<int>(static_cast<long>(h) * aw / w);
                w = aw;
            } else {
                w = static_cast<int>(static_cast<long>(w) * ah / h);
                h = ah;
            }
        }
        w = w < 1 ? 1 : w;
        h = h < 1 ? 1 : h;
        shown_w_ = w;
        shown_h_ = h;
        converter_.begin(info_.width, info_.height, w, h, shown_, info_.animated ? image::steady(dither_) : dither_);
        show(shown_, w, h);
        started_us_ = os::now_us();
        busy_us_ = 0;
        g_timings[0] = g_timings[1] = g_timings[2] = 0;
        timer_ = os::start_timer(0, kTickUs, &ViewerWindow::slice, this);
        set_status("Loading...");
    }

    // One slice of decoding: rows until the time is up, then the new rows
    // are painted.
    static void slice(void *context)
    {
        auto *v = static_cast<ViewerWindow *>(context);
        const uint64_t start = os::now_us();
        if (v->playing_) {
            v->play(start);
            return;
        }
        const int before = v->converter_.rows_done();
        image::Error e = image::Error::None;
        while (!v->decoder_.done() && os::now_us() - start < kSliceUs) {
            e = v->decoder_.step(v->converter_, 4);
            if (e != image::Error::None) {
                break;
            }
        }
        const uint32_t spent = static_cast<uint32_t>(os::now_us() - start);
        v->busy_us_ += spent;
        if (spent > g_timings[2]) {
            g_timings[2] = spent;
        }
        const int after = v->converter_.rows_done();
        if (after > before) {
            v->rows_ready(before, after);
        }
        if (e != image::Error::None) {
            v->stop();
            snprintf(v->message_, sizeof(v->message_), "Cannot show all of %s:\n%s.", fs::path::name(v->path_),
                     image::error_text(e));
            message_box("Picture viewer", v->message_);
            v->set_status("The picture is damaged; part of it is shown.");
            return;
        }
        if (v->decoder_.done()) {
            g_timings[0] = v->busy_us_;
            g_timings[1] = static_cast<uint32_t>(os::now_us() - v->started_us_);
            if (v->decoder_.more_frames()) {
                v->playing_ = true; // the file stays open and the timer goes on
                v->waiting_ = true;
                v->frame_us_ = os::now_us();
                v->played_ = 0;
                v->played_us_ = 0;
                v->played_since_ = v->frame_us_;
            } else {
                v->stop();
            }
            char text[120];
            snprintf(text, sizeof(text), "%s   %d x %d   %s%s", fs::path::name(v->path_), v->info_.width,
                     v->info_.height, v->info_.detail,
                     v->shown_w_ != v->info_.width ? "   (fitted)" : "");
            v->set_status(text);
        } else {
            char text[60];
            snprintf(text, sizeof(text), "Loading... %d%%", v->decoder_.rows_done() * 100 / v->info_.height);
            v->set_status(text);
        }
    }

    // An animation, from the timer. Waiting: when the picture shown has
    // stayed its time, on to the next. Otherwise: a slice of decoding it,
    // and the whole of it painted when it is done, not row by row, so that
    // no picture is seen half made.
    void play(uint64_t now)
    {
        if (waiting_) {
            const uint64_t stay = static_cast<uint64_t>(decoder_.frame_ms()) * 1000;
            if (now - frame_us_ < stay) {
                return;
            }
            // Times are counted from when each picture was due, so that a
            // late tick does not slow the whole; unless it has fallen behind.
            frame_us_ = now - frame_us_ > 2 * stay ? now : frame_us_ + stay;
            if (!decoder_.more_frames() || decoder_.next_frame() != image::Error::None) {
                stop(); // the end, or damage further on: what is shown stays
                return;
            }
            // Only what this picture changes is converted and painted.
            int part[4], first = 0, rows = 0;
            decoder_.changed(part);
            if (converter_.begin_part(info_.width, info_.height, shown_w_, shown_h_, shown_, image::steady(dither_),
                                      part[0], part[1], part[2], part[3], first, rows, made_)) {
                decoder_.only_rows(first, rows);
            } else {
                converter_.begin(info_.width, info_.height, shown_w_, shown_h_, shown_, image::steady(dither_));
                made_[0] = made_[1] = 0;
                made_[2] = shown_w_;
                made_[3] = shown_h_;
            }
            waiting_ = false;
        }
        image::Error e = image::Error::None;
        while (!decoder_.done() && e == image::Error::None && os::now_us() - now < kSliceUs) {
            e = decoder_.step(converter_, 4);
        }
        played_us_ += static_cast<uint32_t>(os::now_us() - now);
        if (e != image::Error::None) {
            stop();
        } else if (decoder_.done()) {
            waiting_ = true;
            const gfx::Rect at = picture_rect();
            view_.redraw(gfx::Rect(at.x + made_[0], at.y + made_[1], made_[2], made_[3]).intersect(area()));
            // Once, five seconds in: how it is going, for whoever measures.
            played_++;
            if (played_since_ != 0 && os::now_us() - played_since_ >= kReportUs) {
                char line[96];
                snprintf(line, sizeof(line), "viewer: animation, %d pictures in %u ms, %u ms of them decoding", played_,
                         static_cast<unsigned>((os::now_us() - played_since_) / 1000),
                         static_cast<unsigned>(played_us_ / 1000));
                os::log(line);
                played_since_ = 0;
            }
            if (!decoder_.more_frames()) {
                stop();
            }
        }
    }

    static void file_chosen(const char *path, void *context) { static_cast<ViewerWindow *>(context)->open_path(path); }
    static void menu_open(void *c) { static_cast<ViewerWindow *>(c)->open_path(nullptr); }
    void again()
    {
        if (path_[0] != 0) {
            start();
        }
    }
    static void menu_fit(void *c)
    {
        auto *v = static_cast<ViewerWindow *>(c);
        v->fit_ = true;
        v->again();
    }
    static void menu_actual(void *c)
    {
        auto *v = static_cast<ViewerWindow *>(c);
        v->fit_ = false;
        v->again();
    }
    static void menu_dither(void *c) { set_dither(*static_cast<image::Dither *>(c)); }
    static void menu_exit(void *c) { static_cast<ViewerWindow *>(c)->close(); }
    static void menu_options(void *c)
    {
        auto *v = static_cast<ViewerWindow *>(c);
        if (has_dialog(v->number())) {
            return;
        }
        app::Look look;
        look.title = "Options";
        look.width = 380;
        look.height = 252;
        look.flags = app::kDialog;
        look.over = v->number();
        if (app::open<OptionsDialog>(look, v->dither_, &set_dither) == app::kNone) {
            no_memory_for("Options");
        }
    }

public:
    void use_dither(image::Dither d)
    {
        dither_ = d;
        settings_.set("view", "dither", image::dither_info(d).id); // kept for the next start
        save_settings("viewer", settings_);
        again();
    }

    uint32_t busy_us_ = 0;

private:
    char path_[fs::kMaxPath] = "";
    char title_[96] = "";
    char message_[200] = "";
    // The picture's file, as the decoder reads it: a handle of the OS.
    struct FromHandle : image::Source {
        bool read(void *data, size_t size, size_t &got) override
        {
            return os::file::read(file, data, size, got) == ports::FsError::Ok;
        }
        bool seek(uint32_t offset) override { return os::file::seek(file, offset) == ports::FsError::Ok; }
        os::file::Handle file = os::file::kNoFile;
    };
    FromHandle source_;
    image::Decoder decoder_;
    image::Converter converter_;
    image::Info info_;
    uint8_t *shown_ = nullptr;
    int shown_w_ = 0, shown_h_ = 0;
    int timer_ = 0;
    uint64_t started_us_ = 0;
    bool playing_ = false, waiting_ = false; // an animation: on, and between two pictures
    uint64_t frame_us_ = 0;                  // when the picture shown was due
    int made_[4] = {};                       // the part of it being made again: x, y, w, h
    int played_ = 0;                         // pictures shown since it began, for the log
    uint32_t played_us_ = 0;                 // and the time spent decoding them
    uint64_t played_since_ = 0;              // 0 once the log has its line
    bool fit_ = true;
    image::Dither dither_ = image::Dither::FloydSteinberg; // and in settings.ini
    bool settings_read_ = false;
    fs::Ini settings_; // C:\.config\viewer.ini
    int last_w_ = 0, last_h_ = 0;
    int width_ = 10, height_ = 10; // of the picture area and its scroll bars
    const uint8_t *pixels_ = nullptr;
    int w_ = 0, h_ = 0, ready_ = 0;
    bool need_v_ = false, need_h_ = false;
    char status_text_[120] = "";
    app::Picture view_;
    app::ScrollBar vbar_, hbar_;
    app::Picture status_;
    wm::MenuItem file_items_[3];
    wm::MenuItem view_items_[5];
    wm::MenuItem dither_items_[image::kDitherCount];
    wm::Menu file_ = {file_items_, 3};
    wm::Menu view_menu_ = {view_items_, 5};
    wm::Menu dither_menu_ = {dither_items_, image::kDitherCount};
    const wm::MenuBarEntry entries_[2] = {{"&File", &file_}, {"&View", &view_menu_}};
    const wm::MenuBar bar_ = {entries_, 2};
};

app::Number g_window = app::kNone; // the one there may be: a number, dead once it has closed

void set_dither(image::Dither d)
{
    if (ViewerWindow *viewer = app::program<ViewerWindow>(g_window)) {
        viewer->use_dither(d);
    }
}

} // namespace

void open_picture(const char *path)
{
    const PathArg p(path);
    in_program("Picture viewer", [p] {
        if (!app::bring_forward(g_window)) {
            app::Look look;
            look.title = "Picture viewer";
            look.x = 110;
            look.y = 40;
            look.width = 420;
            look.height = 360;
            look.icon = &gfx::viewer_icon_16();
            look.flags = app::kMenuBar;
            g_window = app::open<ViewerWindow>(look);
            if (g_window == app::kNone) {
                no_memory_for("Picture viewer");
                return;
            }
        }
        if (ViewerWindow *viewer = app::program<ViewerWindow>(g_window)) {
            viewer->open_path(p.get());
        }
    });
}

void finish_picture()
{
    if (ViewerWindow *viewer = app::program<ViewerWindow>(g_window)) {
        viewer->finish();
    }
}

const uint32_t *picture_timings() { return g_timings; }

} // namespace shell
