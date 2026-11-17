// The Picture viewer: BMP, PNG and JPEG, decoded in slices from a timer so
// the desktop stays responsive and the picture appears as it is decoded.
#include <cstdio>
#include <cstring>

#include "core/gfx/art.hpp"
#include "core/image/convert.hpp"
#include "core/image/image.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "core/wm/wm.hpp"
#include "internal.hpp"

namespace shell {

namespace {

constexpr int kMaxShown = 1600 * 1200;      // actual size up to this many pixels
constexpr uint32_t kSliceUs = 8000;          // decoding time per timer tick
constexpr uint32_t kTickUs = 15000;
constexpr gfx::Colour kBackground = gfx::kDarkGrey;
constexpr uint8_t kBackgroundRgb[3] = {73, 73, 73}; // shows through transparent PNGs

// The picture area: the converted picture, centred, with scroll bars when
// it is larger than the area (actual size).
class PictureView : public wm::Widget {
public:
    PictureView()
        : Widget({0, 0, 10, 10}), vbar_({0, 0, wm::ScrollBar::kThickness, 10}, true, &PictureView::scrolled, this),
          hbar_({0, 0, 10, wm::ScrollBar::kThickness}, false, &PictureView::scrolled, this)
    {
        vbar_.set_parent(this);
        hbar_.set_parent(this);
    }

    void show(const uint8_t *pixels, int w, int h)
    {
        pixels_ = pixels;
        w_ = w;
        h_ = h;
        ready_ = 0;
        vbar_.set_value_quietly(0);
        hbar_.set_value_quietly(0);
        layout();
        invalidate();
    }

    // Rows [from, to) became ready: repaint them only.
    void rows_ready(int from, int to)
    {
        ready_ = to;
        const gfx::Rect at = picture_rect();
        invalidate_part(gfx::Rect(at.x, at.y + from, at.w, to - from).intersect(area()));
    }

    void layout()
    {
        const int t = wm::ScrollBar::kThickness;
        need_h_ = w_ > rect.w;
        need_v_ = h_ > rect.h - (need_h_ ? t : 0);
        need_h_ = w_ > rect.w - (need_v_ ? t : 0);
        const gfx::Rect a = area();
        vbar_.rect = {a.right(), 0, t, a.h};
        hbar_.rect = {0, a.bottom(), a.w, t};
        vbar_.set_range(h_ > a.h ? h_ - a.h : 0, a.h);
        hbar_.set_range(w_ > a.w ? w_ - a.w : 0, a.w);
    }

    gfx::Rect area() const
    {
        const int t = wm::ScrollBar::kThickness;
        return {0, 0, rect.w - (need_v_ ? t : 0), rect.h - (need_h_ ? t : 0)};
    }

    bool layered() const override { return false; } // the picture, painted once already
    void paint(gfx::Canvas &c) override
    {
        const gfx::Rect a = area();
        const gfx::Rect at = picture_rect();
        // The ready rows of the picture, and the background around them,
        // each pixel once.
        const gfx::Rect shown = gfx::Rect(at.x, at.y, at.w, ready_).intersect(a);
        if (pixels_ != nullptr && !shown.empty()) {
            const gfx::Bitmap b = {w_, ready_, pixels_, -1};
            gfx::Canvas inner = c.sub(a);
            inner.blit(b, at.x, at.y);
        }
        c.fill_around(a, shown, kBackground);
        if (need_v_) {
            gfx::Canvas vb = c.sub(vbar_.rect);
            vbar_.paint(vb);
        }
        if (need_h_) {
            gfx::Canvas hb = c.sub(hbar_.rect);
            hbar_.paint(hb);
        }
        if (need_v_ && need_h_) {
            c.fill({a.right(), a.bottom(), wm::ScrollBar::kThickness, wm::ScrollBar::kThickness}, gfx::kGrey);
        }
    }

    bool wants_mouse() const override { return true; }
    void mouse(const wm::MouseMsg &m) override
    {
        if (m.kind == wm::MouseKind::Down) {
            in_bar_ = need_v_ && vbar_.rect.contains(m.x, m.y) ? 1 : (need_h_ && hbar_.rect.contains(m.x, m.y) ? 2 : 0);
        }
        if (in_bar_ != 0) {
            wm::ScrollBar &bar = in_bar_ == 1 ? vbar_ : hbar_;
            bar.mouse({m.kind, m.x - bar.rect.x, m.y - bar.rect.y});
            invalidate_part(bar.rect);
        }
    }

private:
    gfx::Rect picture_rect() const
    {
        const gfx::Rect a = area();
        const int x = w_ <= a.w ? (a.w - w_) / 2 : -hbar_.value();
        const int y = h_ <= a.h ? (a.h - h_) / 2 : -vbar_.value();
        return {x, y, w_, h_};
    }

    static void scrolled(wm::ScrollBar &, void *context)
    {
        auto *v = static_cast<PictureView *>(context);
        v->invalidate_part(v->area());
    }

    const uint8_t *pixels_ = nullptr;
    int w_ = 0, h_ = 0, ready_ = 0;
    bool need_v_ = false, need_h_ = false;
    int in_bar_ = 0;
    wm::ScrollBar vbar_, hbar_;
};

class StatusBar : public wm::Widget {
public:
    StatusBar() : Widget({0, 0, 10, 20}) {}
    void set_text(const char *text)
    {
        snprintf(text_, sizeof(text_), "%s", text);
        invalidate();
    }
    void paint(gfx::Canvas &c) override
    {
        const gfx::Rect r = {0, 0, rect.w, rect.h};
        c.edge(r, gfx::kDarkGrey, gfx::kWhite);
        const gfx::Rect inner = r.inset(1);
        const int ty = (rect.h - 12) / 2;
        const int tw = gfx::Canvas::text_width(text_);
        c.fill_around(inner, {4, ty, tw, 12}, gfx::kGrey);
        c.text_opaque(4, ty, text_, gfx::kBlack, gfx::kGrey);
    }

private:
    char text_[120] = "";
};

void close_window(void *context) { static_cast<wm::Window *>(context)->close(); }

const char *const *dither_names()
{
    static const char *names[image::kDitherCount];
    for (int i = 0; i < image::kDitherCount; i++) {
        names[i] = image::dither_info(static_cast<image::Dither>(i)).name;
    }
    return names;
}

// View > Options: the dithering, from a list with a line about each.
class OptionsDialog : public wm::Window {
public:
    using Done = void (*)(image::Dither d);

    OptionsDialog()
        : Window("Options", {0, 0, 380, 252}, nullptr),
          group_({8, 6, 358, 180}, "Dithering"),
          list_({18, 24, 338, 116}, dither_names(), image::kDitherCount, &OptionsDialog::selected, this),
          about_({18, 150, 338, 14}, ""),
          ok_({210, 196, 75, 23}, "OK", &OptionsDialog::ok, this),
          cancel_({291, 196, 75, 23}, "Cancel", &OptionsDialog::cancel, this)
    {
        set_dialog(true);
        add(group_);
        add(list_);
        add(about_);
        add(ok_);
        add(cancel_);
        ok_.set_default(true);
        set_default(&ok_);
    }

    void show(wm::Window *owner, image::Dither current, Done done)
    {
        if (open()) {
            close();
        }
        done_ = done;
        list_.select(static_cast<int>(current));
        set_modal(owner);
        set_frame(centred(shell::manager(), frame().w, frame().h));
        shell::manager().add(*this);
        set_focus(&list_);
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return Window::key(k);
    }

private:
    static void selected(wm::ListBox &list, void *context)
    {
        auto *d = static_cast<OptionsDialog *>(context);
        if (list.selected() >= 0) {
            d->about_.set_text(image::dither_info(static_cast<image::Dither>(list.selected())).description);
        }
    }
    static void ok(wm::Button &, void *context)
    {
        auto *d = static_cast<OptionsDialog *>(context);
        const int i = d->list_.selected();
        d->close();
        if (i >= 0 && d->done_ != nullptr) {
            d->done_(static_cast<image::Dither>(i));
        }
    }
    static void cancel(wm::Button &, void *context) { static_cast<OptionsDialog *>(context)->close(); }

    Done done_ = nullptr;
    wm::GroupFrame group_;
    wm::ListBox list_;
    wm::Label about_;
    wm::Button ok_, cancel_;
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

class ViewerWindow : public wm::Window {
public:
    ViewerWindow() : Window("Picture viewer", {110, 40, 420, 360}, &gfx::viewer_icon_16())
    {
        file_items_[0] = {"&Open...", &ViewerWindow::menu_open, this, nullptr, nullptr, 0};
        file_items_[1] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[2] = {"E&xit", &close_window, this, nullptr, nullptr, 0};
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
        add(view_);
        add(status_);
        resized();
    }

    void open_path(const char *path)
    {
        if (!memory_ready()) {
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
            show(*this);
            open_file_dialog(this, "C:\\pictures", &ViewerWindow::chosen, this);
            return;
        }
        snprintf(path_, sizeof(path_), "%s", path);
        show(*this);
        start();
    }

    void resized() override
    {
        const gfx::Rect c = client();
        view_.rect = {0, 0, c.w, c.h - 20};
        status_.rect = {0, c.h - 20, c.w, 20};
        view_.layout();
        // Fitting depends on the size: decode again for the new size.
        if (fit_ && path_[0] != 0 && open() && (c.w != last_w_ || c.h != last_h_)) {
            start();
        }
        last_w_ = c.w;
        last_h_ = c.h;
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
    ~ViewerWindow()
    {
        decoder_.release(program_port());
        converter_.release(program_port());
        free_program_memory(shown_);
    }

    // For tests and the debug link: decodes to the end at once.
    void finish()
    {
        while (timer_ != 0) {
            slice(this);
        }
    }

private:
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
            shell::manager().stop_timer(timer_);
            timer_ = 0;
        }
        decoder_.close();
    }

    void start()
    {
        stop();
        const image::Error e = decoder_.open(storage(), path_, program_port(), info_, kBackgroundRgb);
        snprintf(title_, sizeof(title_), "%s - Picture viewer", fs::path::name(path_));
        set_title(title_);
        if (e != image::Error::None) {
            view_.show(nullptr, 0, 0);
            status_.set_text(fs::path::name(path_));
            snprintf(message_, sizeof(message_), "Cannot show %s:\n%s.", fs::path::name(path_), image::error_text(e));
            message_box("Picture viewer", message_);
            return;
        }
        // The size shown: fitted inside the area (never enlarged), or the
        // real size if it fits in memory.
        int w = info_.width, h = info_.height;
        const gfx::Rect c = client();
        const int aw = c.w, ah = c.h - 20;
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
        converter_.begin(info_.width, info_.height, w, h, shown_, dither_);
        view_.show(shown_, w, h);
        started_us_ = shell::manager().now_us();
        busy_us_ = 0;
        g_timings[0] = g_timings[1] = g_timings[2] = 0;
        timer_ = shell::manager().start_timer(0, kTickUs, &ViewerWindow::slice, this);
        status_.set_text("Loading...");
    }

    // One slice of decoding: rows until the time is up, then the new rows
    // are painted.
    static void slice(void *context)
    {
        auto *v = static_cast<ViewerWindow *>(context);
        wm::Wm &wm = shell::manager();
        const uint64_t start = wm.now_us();
        const int before = v->converter_.rows_done();
        image::Error e = image::Error::None;
        while (!v->decoder_.done() && wm.now_us() - start < kSliceUs) {
            e = v->decoder_.step(v->converter_, 4);
            if (e != image::Error::None) {
                break;
            }
        }
        const uint32_t spent = static_cast<uint32_t>(wm.now_us() - start);
        v->busy_us_ += spent;
        if (spent > g_timings[2]) {
            g_timings[2] = spent;
        }
        const int after = v->converter_.rows_done();
        if (after > before) {
            v->view_.rows_ready(before, after);
        }
        if (e != image::Error::None) {
            v->stop();
            snprintf(v->message_, sizeof(v->message_), "Cannot show all of %s:\n%s.", fs::path::name(v->path_),
                     image::error_text(e));
            message_box("Picture viewer", v->message_);
            v->status_.set_text("The picture is damaged; part of it is shown.");
            return;
        }
        if (v->decoder_.done()) {
            g_timings[0] = v->busy_us_;
            g_timings[1] = static_cast<uint32_t>(wm.now_us() - v->started_us_);
            v->stop();
            char text[120];
            snprintf(text, sizeof(text), "%s   %d x %d   %s%s", fs::path::name(v->path_), v->info_.width,
                     v->info_.height, v->info_.detail,
                     v->shown_w_ != v->info_.width ? "   (fitted)" : "");
            v->status_.set_text(text);
        } else {
            char text[60];
            snprintf(text, sizeof(text), "Loading... %d%%", v->decoder_.rows_done() * 100 / v->info_.height);
            v->status_.set_text(text);
        }
    }

    static void chosen(const char *path, void *context) { static_cast<ViewerWindow *>(context)->open_path(path); }
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
    static void menu_options(void *c)
    {
        auto *v = static_cast<ViewerWindow *>(c);
        if (OptionsDialog *options = one<OptionsDialog>("Options")) {
            options->show(v, v->dither_, &set_dither);
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
    image::Decoder decoder_;
    image::Converter converter_;
    image::Info info_;
    uint8_t *shown_ = nullptr;
    int shown_w_ = 0, shown_h_ = 0;
    int timer_ = 0;
    uint64_t started_us_ = 0;
    bool fit_ = true;
    image::Dither dither_ = image::Dither::FloydSteinberg; // and in settings.ini
    bool settings_read_ = false;
    fs::Ini settings_; // C:\.config\viewer.ini
    int last_w_ = 0, last_h_ = 0;
    PictureView view_;
    StatusBar status_;
    wm::MenuItem file_items_[3];
    wm::MenuItem view_items_[5];
    wm::MenuItem dither_items_[image::kDitherCount];
    wm::Menu file_ = {file_items_, 3};
    wm::Menu view_menu_ = {view_items_, 5};
    wm::Menu dither_menu_ = {dither_items_, image::kDitherCount};
    const wm::MenuBarEntry entries_[2] = {{"&File", &file_}, {"&View", &view_menu_}};
    const wm::MenuBar bar_ = {entries_, 2};
};

void set_dither(image::Dither d)
{
    if (ViewerWindow *viewer = the<ViewerWindow>()) {
        viewer->use_dither(d);
    }
}

} // namespace

void open_picture(const char *path)
{
    const AsProgram as("Picture viewer");
    if (ViewerWindow *viewer = one<ViewerWindow>("Picture viewer")) {
        viewer->open_path(path);
        if (!viewer->open()) {
            discard(viewer); // no memory for the picture, or no room for the window
        }
    }
}

void finish_picture()
{
    if (ViewerWindow *viewer = the<ViewerWindow>()) {
        viewer->finish();
    }
}

const uint32_t *picture_timings() { return g_timings; }

} // namespace shell
