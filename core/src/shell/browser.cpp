// Vizcacha, the web browser: pages of the years around HTML 3.2 over plain
// HTTP, and .htm files from the drives. No scripts, no style sheets, no
// encryption (docs/journal/2026-11-17-web-browser.md).
//
// The page is read and laid out by core/www (html.hpp); it is fetched by
// core/www/http.hpp over the system's outgoing connections (os::tcp), from
// a timer, so the desktop goes on while a page loads. This file is the
// window: the buttons, the address, painting the page, the mouse and the
// keys in it, and where the browser has been.
#include <cstdio>
#include <cstring>
#include <new>
#include <strings.h>

#include "core/app/app.hpp"
#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/os/os.hpp"
#include "core/os/tcp.hpp"
#include "core/shell/shell.hpp"
#include "core/image/convert.hpp"
#include "core/image/image.hpp"
#include "core/www/html.hpp"
#include "core/www/http.hpp"
#include "core/www/url.hpp"
#include "internal.hpp"

namespace shell {

namespace {

constexpr const char *kName = "Vizcacha";
constexpr const char *kHome = "about:home";
constexpr int kBarHeight = 54;    // the buttons and the address
constexpr int kStatusHeight = 18;
constexpr int kHistory = 16;      // ponytail: pages it can go back through
constexpr int kFavourites = 8;
constexpr uint32_t kPollUs = 30000;
constexpr int kLineStep = 3 * (www::kCharHeight + 2);
// ponytail: how many of a page's pictures are fetched, and how much memory
// they may take once decoded (a byte a pixel). The rest keep their boxes.
constexpr int kMaxPictures = 24;
constexpr size_t kMaxPictureBytes = 1536 * 1024;
constexpr int kMaxPictureHeight = 2000;
constexpr uint32_t kDecodeSliceUs = 8000; // of decoding in one turn of the timer

// A picture that has come: a board colour for each pixel.
struct Loaded {
    uint8_t *pixels;
    uint16_t w, h;
};

// ponytail: how many of a page's pictures may move at once, and how much
// memory they may take between them (each one's file and the working
// memory that plays it). The rest show their first picture, still.
constexpr int kMaxAnimations = 6;
constexpr size_t kMaxAnimationBytes = 768 * 1024;

// A picture's bytes in memory, as the decoder reads them.
struct FromBytes final : image::Source {
    bool read(void *data, size_t wanted, size_t &got) override
    {
        got = wanted < size - at ? wanted : size - at;
        memcpy(data, bytes + at, got);
        at += got;
        return true;
    }
    bool seek(uint32_t offset) override
    {
        if (offset > size) {
            return false;
        }
        at = offset;
        return true;
    }
    const uint8_t *bytes = nullptr;
    size_t size = 0, at = 0;
};

// A picture of the page that moves (an animated GIF): its file, kept, and
// a decoder of its own that stays open on it between one picture of the
// animation and the next.
struct Animation {
    image::Decoder decoder;
    FromBytes source;
    uint8_t *bytes = nullptr;
    size_t memory = 0;     // what it counts for against kMaxAnimationBytes
    int picture = -1;      // which of the page's pictures it is
    int src_w = 0, src_h = 0;
    uint64_t frame_us = 0; // when the picture shown was due
};

// The system's outgoing connections, as the fetch wants them.
class OsWire final : public www::Wire {
public:
    int connect(const char *host, int port) override { return os::tcp::connect(host, port); }
    State state(int connection) override
    {
        switch (os::tcp::state(connection)) {
        case os::tcp::State::Connecting: return State::Connecting;
        case os::tcp::State::Open: return State::Open;
        default: return State::Failed;
        }
    }
    const char *why(int connection) override { return net::dial_failure_text(os::tcp::failure(connection)); }
    int read(int connection, uint8_t *out, size_t capacity) override
    {
        return os::tcp::read(connection, out, capacity);
    }
    int write(int connection, const uint8_t *data, size_t size) override
    {
        return os::tcp::write(connection, data, size);
    }
    void close(int connection) override { os::tcp::close(connection); }
};

// The page the browser starts on.
const char kHomePage[] =
    "<html><head><title>Home</title></head><body>"
    "<center><h1><font color=\"#804000\">Vizcacha</font></h1>"
    "The web browser of Chinchilla<br><br>"
    "<form action=\"http://frogfind.com/\" method=\"get\">"
    "Search the web with FrogFind: <input type=\"text\" size=\"30\" name=\"q\"> <input type=\"submit\" value=\"Search\">"
    "</form></center><hr>"
    "<p>This browser shows pages as the web wrote them in the 1990s: text, links, lists and forms. "
    "It runs no scripts and cannot open secure (https) addresses, which is most of today's web. "
    "<a href=\"http://frogfind.com/\">FrogFind</a> fetches today's pages and hands them over in a form "
    "this browser can read, so a search there is the way in.</p>"
    "<ul><li><a href=\"http://frogfind.com/\">FrogFind</a>, a search engine for old computers"
    "<li><a href=\"http://frogfind.com/news\">FrogFind News</a>"
    "<li><a href=\"http://frogfind.com/about\">About FrogFind</a></ul>"
    "<p>Type an address above and press Enter. Pages saved on a drive open with File, Open File, "
    "or by typing their path (C:\\Documents\\page.htm).</p>"
    "</body></html>";

app::Number g_browser = app::kNone; // the one there may be: dead once it has closed

class BrowserWindow : public app::Window {
public:
    BrowserWindow()
        : back_(button({4, 3, 52, 23}, "&Back")),
          forward_(button({58, 3, 64, 23}, "&Forward")),
          reload_(button({124, 3, 58, 23}, "&Reload")),
          stop_(button({184, 3, 46, 23}, "&Stop")),
          home_(button({232, 3, 46, 23}, "&Home")),
          address_label_(label({6, 33, 52, 14}, "A&ddress:")),
          address_(text_field({60, 30, 300, 21})),
          go_(button({364, 29, 36, 23}, "Go")),
          page_(picture({0, kBarHeight, 100, 100}, app::kMouse | app::kKeys)),
          scroll_(scroll_bar({100, kBarHeight, os::ui::kScrollBarThickness, 100}, true)),
          status_(plain({4, 0, 100, 14})),
          doc_(os::memory()),
          fetch_(wire_, os::memory()),
          picture_fetch_(wire_, os::memory())
    {
        file_items_[0] = {"&Open File...", &BrowserWindow::menu_open, this, nullptr, nullptr, 0};
        file_items_[1] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[2] = {"&Close", &BrowserWindow::menu_close, this, nullptr, nullptr, 0};
        go_items_[0] = {"&Back", &BrowserWindow::menu_back, this, nullptr, nullptr, 0};
        go_items_[1] = {"&Forward", &BrowserWindow::menu_forward, this, nullptr, nullptr, 0};
        go_items_[2] = {"&Home", &BrowserWindow::menu_home, this, nullptr, nullptr, 0};
        go_items_[3] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        go_items_[4] = {"&Reload", &BrowserWindow::menu_reload, this, nullptr, nullptr, 0};
        go_items_[5] = {"&Stop", &BrowserWindow::menu_stop, this, nullptr, nullptr, 0};
        load_settings("browser", settings_);
        fill_favourites();
        set_menu_bar(&bar_);
        set_default(go_);
        const gfx::Rect c = client_size();
        resized(c.w, c.h);
        update_buttons();
    }

    ~BrowserWindow()
    {
        forget_pictures();
        stop_timer();
    }

    // Opens an address as if it had been typed.
    void open_typed(const char *typed)
    {
        char url[www::kMaxUrl];
        if (!www::from_typed(typed, url, sizeof(url))) {
            message_box(kName, "That address is too long, or empty.");
            return;
        }
        visit(url);
    }

    void command(app::Control which) override
    {
        if (which == back_) {
            move(-1);
        } else if (which == forward_) {
            move(1);
        } else if (which == reload_) {
            reload();
        } else if (which == stop_) {
            stop_loading("Stopped.");
        } else if (which == home_) {
            go_home();
        } else if (which == go_) {
            char typed[2 * app::TextField::kCapacity + 1];
            address_.get(typed, sizeof(typed));
            // The field shows only the start of a long address: left as it
            // is, it means the page that is open.
            if (strcmp(typed, shown_address_) == 0 && current_[0] != 0) {
                reload();
            } else {
                open_typed(typed);
            }
        }
    }

    void changed(app::Control which) override
    {
        if (which == scroll_) {
            top_ = scroll_.value();
            page_.redraw();
            update_caret();
        }
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (!k.pressed) {
            return false;
        }
        if ((k.mods & wm::kAlt) && k.usage == wm::key::Left) {
            move(-1);
            return true;
        }
        if ((k.mods & wm::kAlt) && k.usage == wm::key::Right) {
            move(1);
            return true;
        }
        if ((k.mods & wm::kAlt) && k.usage == wm::key::A + ('d' - 'a')) {
            set_focus(address_);
            address_.select_all();
            return true;
        }
        if (k.usage == wm::key::Escape && fetch_.busy()) {
            stop_loading("Stopped.");
            return true;
        }
        return false;
    }

    bool key_in(app::Control, const wm::KeyMsg &k) override
    {
        if (!k.pressed) {
            return false;
        }
        if (typing_ >= 0) {
            www::Field &field = doc_.field(typing_);
            if (k.usage == wm::key::Backspace) {
                // The last character, which may be two bytes.
                size_t length = strlen(field.typed);
                while (length > 0 && (static_cast<unsigned char>(field.typed[length - 1]) & 0xC0) == 0x80) {
                    length--;
                }
                if (length > 0) {
                    length--;
                }
                field.typed[length] = 0;
                redraw_field(typing_);
                return true;
            }
            if (k.usage == wm::key::Enter) {
                submit(typing_);
                return true;
            }
            if (k.usage == wm::key::Escape) {
                set_typing(-1);
                return true;
            }
            if (k.usage == wm::key::Space) {
                return false; // typed, not a page down
            }
        }
        const int view = view_height();
        switch (k.usage) {
        case wm::key::Up: scroll_to(top_ - kLineStep); return true;
        case wm::key::Down: scroll_to(top_ + kLineStep); return true;
        case wm::key::PageUp: scroll_to(top_ - (view - kLineStep)); return true;
        case wm::key::Space:
        case wm::key::PageDown: scroll_to(top_ + (view - kLineStep)); return true;
        case wm::key::Home: scroll_to(0); return true;
        case wm::key::End: scroll_to(doc_.height()); return true;
        case wm::key::Backspace: move(-1); return true;
        default: break;
        }
        return false;
    }

    void text_in(app::Control, const char *utf8) override
    {
        if (typing_ < 0) {
            return;
        }
        www::Field &field = doc_.field(typing_);
        const size_t length = strlen(field.typed), more = strlen(utf8);
        if (length + more + 1 <= www::Document::kTypedBytes) {
            memcpy(field.typed + length, utf8, more + 1);
            redraw_field(typing_);
        }
    }

    void mouse(app::Control, const wm::MouseMsg &m) override
    {
        const int at = doc_.item_at(m.x, m.y + top_);
        if (m.kind == wm::MouseKind::Down) {
            set_focus(page_);
            pressed_ = at;
            const bool on_text = at >= 0 && doc_.item(at).kind == www::Kind::Field &&
                                 (doc_.field(doc_.item(at).extra).type == www::FieldType::Text ||
                                  doc_.field(doc_.item(at).extra).type == www::FieldType::Password);
            set_typing(on_text ? doc_.item(at).extra : -1);
            redraw_pressed();
            return;
        }
        if (m.kind != wm::MouseKind::Up) {
            return;
        }
        const int was = pressed_;
        redraw_pressed();
        pressed_ = -1;
        if (at < 0 || at != was) {
            // Let go somewhere else: a link's other words still count.
            if (at < 0 || was < 0 || doc_.item(at).link == 0 || doc_.item(at).link != doc_.item(was).link) {
                return;
            }
        }
        const www::Item &item = doc_.item(at);
        if (item.kind == www::Kind::Field) {
            click_field(item.extra);
        } else if (item.link != 0) {
            follow(doc_.link(item.link));
        }
    }

    void paint_in(app::Control, app::Canvas &c) override
    {
        const gfx::Rect size = page_size();
        const gfx::Rect part = c.limit();
        c.fill({0, 0, size.w, size.h}, doc_.background());
        for (int i = doc_.first_at(top_ + part.y); i < doc_.item_count(); i++) {
            const www::Item &item = doc_.item(i);
            if (item.y - top_ >= part.y + part.h) {
                break;
            }
            paint_item(c, i, item);
        }
    }

    void resized(int w, int h) override
    {
        const int bar = os::ui::kScrollBarThickness;
        const int field = w - 60 - 44;
        address_.place({60, 30, field > 40 ? field : 40, 21});
        go_.place({w - 40, 29, 36, 23});
        const int view = h - kBarHeight - kStatusHeight;
        page_.place({0, kBarHeight, w - bar, view > 0 ? view : 0});
        scroll_.place({w - bar, kBarHeight, bar, view > 0 ? view : 0});
        status_.place({4, h - kStatusHeight + 3, w - 8, 14});
        if (w - bar != doc_.width()) {
            // The same words are at the top after the lines have moved.
            const int first = doc_.first_at(top_);
            doc_.layout(w - bar);
            top_ = first < doc_.item_count() ? doc_.item(first).y : 0;
        }
        update_scroll();
        page_.redraw();
        update_caret();
    }

    void menu_opening(int which) override
    {
        if (which == 1) {
            go_items_[0].flags = at_ > 0 ? 0 : wm::kMenuDisabled;
            go_items_[1].flags = at_ + 1 < count_ ? 0 : wm::kMenuDisabled;
            go_items_[5].flags = fetch_.busy() || picture_at_ >= 0 ? 0 : wm::kMenuDisabled;
        }
    }

    void closed() override
    {
        forget_pictures();
        fetch_.stop();
        stop_timer();
    }

private:
    /* ---- Where it has been ---- */

    // Opens an address and remembers it after the one shown.
    void visit(const char *address)
    {
        // A copy: the address may be one of the history's own lines, which
        // are about to move.
        char url[www::kMaxUrl];
        snprintf(url, sizeof(url), "%s", address);
        remember_top();
        if (count_ > 0 && at_ + 1 < count_) {
            count_ = at_ + 1; // what lay ahead is forgotten
        }
        if (count_ == kHistory) {
            memmove(&history_[0], &history_[1], sizeof(history_[0]) * (kHistory - 1));
            memmove(&tops_[0], &tops_[1], sizeof(tops_[0]) * (kHistory - 1));
            count_--;
        }
        snprintf(history_[count_], sizeof(history_[count_]), "%s", url);
        tops_[count_] = 0;
        at_ = count_++;
        load(url, 0);
    }
    void move(int by)
    {
        const int to = at_ + by;
        if (to < 0 || to >= count_) {
            return;
        }
        remember_top();
        at_ = to;
        load(history_[at_], tops_[at_]);
    }
    void reload()
    {
        if (count_ > 0) {
            load(history_[at_], top_);
        }
    }
    void go_home()
    {
        char home[www::kMaxUrl];
        if (!settings_.get("browser", "home", home, sizeof(home)) || home[0] == 0) {
            snprintf(home, sizeof(home), "%s", kHome);
        }
        visit(home);
    }
    void remember_top()
    {
        if (count_ > 0 && !fetch_.busy()) {
            tops_[at_] = top_;
        }
    }
    void follow(const char *reference)
    {
        char url[www::kMaxUrl];
        const char *against = doc_.base()[0] != 0 ? doc_.base() : current_;
        if (!www::resolve(against, reference, url, sizeof(url))) {
            set_status("That link's address cannot be followed.");
            return;
        }
        visit(url);
    }

    /* ---- Loading ---- */

    void load(const char *url, int top)
    {
        stop_picture(); // those that have come stay until the next page is shown
        fetch_.stop();
        stop_timer();
        wanted_top_ = top;
        snprintf(loading_, sizeof(loading_), "%s", url);
        switch (www::scheme_of(url)) {
        case www::Scheme::About:
            if (strcasecmp(url, "about:blank") == 0) {
                show(url, "", 0, false);
            } else {
                show(url, kHomePage, sizeof(kHomePage) - 1, false);
            }
            break;
        case www::Scheme::File: load_file(url); break;
        case www::Scheme::Http:
            if (!os::tcp::available()) {
                trouble(url, "This copy of Chinchilla has no connection to the internet.", false);
                break;
            }
            if (!fetch_.start(url, static_cast<uint32_t>(os::now_us()))) {
                trouble(url, fetch_.error(), false);
                break;
            }
            set_address(url);
            status_host("Looking for ", url);
            timer_ = os::start_timer(kPollUs, kPollUs, &BrowserWindow::tick, this);
            if (timer_ == 0) {
                fetch_.stop();
                trouble(url, "The system has no timer free for the browser.", false);
            }
            break;
        case www::Scheme::Https:
            trouble(url, "That is a secure (https) address, and this browser cannot encrypt.", true);
            break;
        default: trouble(url, "This browser opens http addresses and files, and that is neither.", false); break;
        }
        update_buttons();
    }

    void load_file(const char *url)
    {
        char path[fs::kMaxPath];
        fs::DirEntry entry;
        if (!www::file_path(url, path, sizeof(path))) {
            trouble(url, "That is not the path of a file.", false);
            return;
        }
        const os::file::Error found = os::file::stat(path, entry);
        if (found != os::file::Error::Ok || entry.folder) {
            char why[80];
            snprintf(why, sizeof(why), "The file could not be opened: %s.", fs::error_text(found));
            trouble(url, entry.folder && found == os::file::Error::Ok ? "That is a folder, not a page." : why, false);
            return;
        }
        if (entry.size > www::Fetch::kMaxBody) {
            trouble(url, "The file is too large for the browser.", false);
            return;
        }
        const os::Busy busy;
        void *bytes = os::allocate(entry.size + 1);
        size_t size = 0;
        if (bytes == nullptr) {
            trouble(url, "There is not enough memory for this page.", false);
            return;
        }
        const os::file::Error read = os::file::read_file(path, bytes, entry.size, size);
        if (read != os::file::Error::Ok) {
            os::release(bytes);
            char why[80];
            snprintf(why, sizeof(why), "The file could not be read: %s.", fs::error_text(read));
            trouble(url, why, false);
            return;
        }
        char ext[8];
        fs::path::extension(path, ext, sizeof(ext));
        const bool plain = strcmp(ext, "htm") != 0 && strcmp(ext, "html") != 0;
        show(url, static_cast<const char *>(bytes), size, plain);
        os::release(bytes);
    }

    static void tick(void *self) { static_cast<BrowserWindow *>(self)->poll(); }
    void poll()
    {
        animate();
        if (!fetch_.busy()) {
            if (picture_at_ >= 0) {
                poll_picture();
            } else {
                stop_timer();
            }
            return;
        }
        const www::Fetch::State before = fetch_.state();
        const www::Fetch::State now = fetch_.poll(static_cast<uint32_t>(os::now_us()));
        if (now == www::Fetch::State::Failed) {
            stop_timer();
            char why[sizeof(error_)];
            snprintf(why, sizeof(why), "%s", fetch_.error());
            char url[www::kMaxUrl];
            snprintf(url, sizeof(url), "%s", fetch_.url());
            trouble(url, why, www::scheme_of(url) == www::Scheme::Https);
            update_buttons();
            return;
        }
        if (now == www::Fetch::State::Done) {
            stop_timer();
            fetched();
            update_buttons();
            return;
        }
        if (now != before || now == www::Fetch::State::Receiving) {
            switch (now) {
            case www::Fetch::State::Connecting: status_host("Looking for ", fetch_.url()); break;
            case www::Fetch::State::Sending:
            case www::Fetch::State::Waiting: status_host("Waiting for ", fetch_.url()); break;
            case www::Fetch::State::Receiving:
                if (fetch_.size() / 1024 != shown_kb_ || now != before) {
                    shown_kb_ = fetch_.size() / 1024;
                    char text[64];
                    snprintf(text, sizeof(text), "Receiving the page: %u KB", static_cast<unsigned>(shown_kb_));
                    set_status(text);
                }
                break;
            default: break;
            }
        }
    }

    // The fetch has ended with an answer.
    void fetched()
    {
        char url[www::kMaxUrl];
        snprintf(url, sizeof(url), "%s", fetch_.url());
        if (count_ > 0) {
            snprintf(history_[at_], sizeof(history_[at_]), "%s", url); // where a moved page ended up
        }
        const char *type = fetch_.content_type();
        const bool html = type[0] == 0 || strncasecmp(type, "text/html", 9) == 0 ||
                          strncasecmp(type, "application/xhtml", 17) == 0;
        const bool plain = strncasecmp(type, "text/", 5) == 0 && !html;
        if (strncasecmp(type, "image/", 6) == 0) {
            // A picture by itself: a page of one picture, made of the bytes
            // that have just come.
            char *page = static_cast<char *>(os::allocate(6 * www::kMaxUrl + 64));
            if (page != nullptr) {
                const int n = snprintf(page, 16, "<img src=\"");
                escape(url, page + n, 6 * www::kMaxUrl);
                snprintf(page + strlen(page), 16, "\">");
                show(url, page, strlen(page), false, true);
                os::release(page);
                return;
            }
        }
        if (!html && !plain) {
            char why[120];
            snprintf(why, sizeof(why), "It is not a page but a file of the kind \"%.40s\", which the browser cannot show.",
                     type);
            fetch_.stop();
            trouble(url, why, false);
            return;
        }
        const bool cut = fetch_.cut();
        const int status = fetch_.status();
        show(url, reinterpret_cast<const char *>(fetch_.body()), fetch_.size(), plain);
        fetch_.stop(); // the page is read: the bytes go back
        char text[80];
        if (cut) {
            snprintf(text, sizeof(text), "Done. The page was longer than %u KB and was cut there.",
                     static_cast<unsigned>(www::Fetch::kMaxBody / 1024));
            set_status(text);
        } else if (status >= 400) {
            snprintf(text, sizeof(text), "The site answered with error %d.", status);
            set_status(text);
        }
    }

    // Puts a page in the window.
    void show(const char *url, const char *bytes, size_t size, bool plain, bool direct = false)
    {
        set_typing(-1);
        pressed_ = -1;
        forget_pictures();
        direct_ = direct;
        if (!doc_.load(reinterpret_cast<const uint8_t *>(bytes), size, plain)) {
            doc_.load(reinterpret_cast<const uint8_t *>(""), 0);
            set_status("There is not enough memory for this page.");
        } else {
            set_status("Done.");
        }
        snprintf(current_, sizeof(current_), "%s", url);
        set_address(url);
        doc_.layout(page_size().w);
        top_ = wanted_top_;
        update_scroll();
        const char *title = doc_.title()[0] != 0 ? doc_.title() : url;
        snprintf(title_, sizeof(title_), "%.70s - %s", title, kName);
        set_title(title_);
        page_.redraw();
        set_focus(page_);
        start_pictures();
    }

    /* ---- Pictures ---- */

    // The pictures of the page just shown are fetched one after another,
    // from the same timer as the page was, and each is drawn when it is
    // whole. The page can be read meanwhile.
    void start_pictures()
    {
        picture_at_ = -1;
        pictures_failed_ = 0;
        const int count = doc_.picture_count() < kMaxPictures ? doc_.picture_count() : kMaxPictures;
        if (count > 0) {
            loaded_ = static_cast<Loaded *>(os::allocate(static_cast<size_t>(count) * sizeof(Loaded)));
        }
        if (loaded_ == nullptr) {
            end_direct();
            return;
        }
        memset(loaded_, 0, static_cast<size_t>(count) * sizeof(Loaded));
        loaded_count_ = count;
        if (timer_ == 0) {
            timer_ = os::start_timer(kPollUs, kPollUs, &BrowserWindow::tick, this);
        }
        if (timer_ == 0) {
            end_direct();
            return; // no timer free: the page without its pictures
        }
        next_picture();
    }

    void next_picture()
    {
        while (++picture_at_ < loaded_count_ && loaded_bytes_ < kMaxPictureBytes) {
            if (begin_picture()) {
                update_buttons();
                return;
            }
            pictures_failed_++;
        }
        picture_at_ = -1;
        end_direct();
        stop_timer();
        if (pictures_failed_ > 0) {
            char text[64];
            snprintf(text, sizeof(text), pictures_failed_ == 1 ? "Done. One picture could not be shown."
                                                               : "Done. %d pictures could not be shown.",
                     pictures_failed_);
            set_status(text);
        } else {
            set_status("Done.");
        }
        update_buttons();
    }

    // True: picture_at_ is being fetched or decoded.
    bool begin_picture()
    {
        if (direct_) {
            return begin_decode(fetch_.body(), fetch_.size());
        }
        const char *source = doc_.text(doc_.picture(picture_at_).source);
        char url[www::kMaxUrl];
        if (source[0] == 0 ||
            !www::resolve(doc_.base()[0] != 0 ? doc_.base() : current_, source, url, sizeof(url))) {
            return false;
        }
        char text[48];
        snprintf(text, sizeof(text), "Fetching picture %d of %d...", picture_at_ + 1, loaded_count_);
        switch (www::scheme_of(url)) {
        case www::Scheme::File:
            {
                char path[fs::kMaxPath];
                fs::DirEntry entry;
                size_t size = 0;
                if (!www::file_path(url, path, sizeof(path)) || os::file::stat(path, entry) != os::file::Error::Ok ||
                    entry.folder || entry.size > www::Fetch::kMaxBody) {
                    return false;
                }
                file_bytes_ = os::allocate(entry.size + 1);
                if (file_bytes_ == nullptr ||
                    os::file::read_file(path, file_bytes_, entry.size, size) != os::file::Error::Ok ||
                    !begin_decode(static_cast<const uint8_t *>(file_bytes_), size)) {
                    os::release(file_bytes_);
                    file_bytes_ = nullptr;
                    return false;
                }
                return true;
            }
        case www::Scheme::Http:
            if (!os::tcp::available() || !picture_fetch_.start(url, static_cast<uint32_t>(os::now_us()))) {
                picture_fetch_.stop();
                return false;
            }
            set_status(text);
            return true;
        default: return false;
        }
    }

    void poll_picture()
    {
        if (decoding_ == nullptr) {
            if (animating_ >= 0) {
                return; // the converter is busy with a picture that moves: next turn
            }
            const www::Fetch::State now = picture_fetch_.poll(static_cast<uint32_t>(os::now_us()));
            if (now == www::Fetch::State::Done && picture_fetch_.status() < 400 && !picture_fetch_.cut() &&
                begin_decode(picture_fetch_.body(), picture_fetch_.size())) {
                return;
            }
            if (!picture_fetch_.busy()) {
                picture_fetch_.stop();
                pictures_failed_++;
                next_picture();
            }
            return;
        }
        const uint64_t start = os::now_us();
        image::Error e = image::Error::None;
        image::Decoder &decoder = starting_ != nullptr ? starting_->decoder : decoder_;
        while (!decoder.done() && e == image::Error::None && os::now_us() - start < kDecodeSliceUs) {
            e = decoder.step(converter_, 4);
        }
        if (e != image::Error::None) {
            end_decode(false);
            pictures_failed_++;
            next_picture();
        } else if (decoder.done()) {
            end_decode(true);
            next_picture();
        }
    }

    // Opens the picture in these bytes and makes room for it at the size it
    // will have in the page: the page's own figures if it gives them, never
    // larger than the picture is, never wider than the page.
    bool begin_decode(const uint8_t *bytes, size_t size)
    {
        source_.bytes = bytes;
        source_.size = size;
        source_.at = 0;
        const gfx::Rgb paper = gfx::shown(doc_.background());
        const uint8_t behind[3] = {paper.r, paper.g, paper.b};
        image::Info info;
        if (bytes == nullptr || decoder_.open(source_, os::memory(), info, behind) != image::Error::None ||
            info.width <= 0 || info.height <= 0) {
            decoder_.release(os::memory());
            return false;
        }
        const www::Picture &asked = doc_.picture(picture_at_);
        long w = info.width, h = info.height;
        if (asked.width > 0 && asked.height > 0) {
            w = asked.width;
            h = asked.height;
        } else if (asked.width > 0) {
            w = asked.width;
            h = static_cast<long>(info.height) * w / info.width;
        } else if (asked.height > 0) {
            h = asked.height;
            w = static_cast<long>(info.width) * h / info.height;
        }
        const auto fit = [&w, &h](long most_w, long most_h) {
            if (w > most_w) {
                h = h * most_w / w;
                w = most_w;
            }
            if (h > most_h) {
                w = w * most_h / h;
                h = most_h;
            }
        };
        const int room = page_size().w - 2 * www::Document::kMargin;
        fit(info.width, info.height);
        fit(room > 16 ? room : 16, kMaxPictureHeight);
        w = w < 1 ? 1 : w;
        h = h < 1 ? 1 : h;
        const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
        if (loaded_bytes_ + pixels <= kMaxPictureBytes) {
            decoding_ = static_cast<uint8_t *>(os::allocate(pixels));
        }
        if (decoding_ == nullptr || !converter_.ready(os::memory())) {
            os::release(decoding_);
            decoding_ = nullptr;
            decoder_.release(os::memory());
            return false;
        }
        converting_ = true;
        memset(decoding_, doc_.background(), pixels);
        decode_w_ = static_cast<int>(w);
        decode_h_ = static_cast<int>(h);
        // A file of several pictures gets a decoder of its own, which
        // decodes the first one too, if there is room for one more.
        if (info.several && begin_animation(bytes, size, info, behind)) {
            decoder_.release(os::memory());
        }
        converter_.begin(info.width, info.height, decode_w_, decode_h_, decoding_,
                         starting_ != nullptr ? image::Dither::Bayer4 : image::Dither::FloydSteinberg);
        return true;
    }

    /* ---- Pictures that move ---- */

    // A decoder for the animation in these bytes, with a copy of them and
    // working memory of the size its screen needs. False if the page has
    // its fill of them or there is no memory: the picture then stays still.
    bool begin_animation(const uint8_t *bytes, size_t size, const image::Info &info, const uint8_t behind[3])
    {
        const size_t pool = image::animation_pool(info.width, info.height);
        if (animation_count_ == kMaxAnimations || animation_bytes_ + pool + size > kMaxAnimationBytes) {
            return false;
        }
        void *place = os::allocate(sizeof(Animation));
        if (place == nullptr) {
            return false;
        }
        Animation *a = new (place) Animation;
        a->bytes = static_cast<uint8_t *>(os::allocate(size));
        image::Info again;
        if (a->bytes != nullptr) {
            memcpy(a->bytes, bytes, size);
            a->source.bytes = a->bytes;
            a->source.size = size;
            a->decoder.use_pool(os::memory(), pool);
        }
        if (a->bytes == nullptr || a->decoder.open(a->source, os::memory(), again, behind) != image::Error::None ||
            !again.animated) {
            end_animation(a);
            return false;
        }
        a->memory = pool + size;
        a->src_w = info.width;
        a->src_h = info.height;
        starting_ = a;
        return true;
    }

    void end_animation(Animation *a)
    {
        a->decoder.release(os::memory());
        os::release(a->bytes);
        a->~Animation();
        os::release(a);
    }

    // Where a picture of the page is in the window, if any of it shows.
    gfx::Rect picture_on_screen(int picture) const
    {
        for (int i = 0; i < doc_.item_count(); i++) {
            const www::Item &item = doc_.item(i);
            if (item.kind == www::Kind::Image && item.extra == picture) {
                return gfx::Rect(item.x, item.y - top_, item.w, item.h).intersect(page_size());
            }
        }
        return {0, 0, 0, 0};
    }

    // From the timer: the pictures that move. One is decoded at a time (the
    // converter is one), a slice a turn, and painted when it is whole. A
    // picture whose time has come is begun when nothing else is being
    // decoded; one that cannot be seen waits.
    void animate()
    {
        const uint64_t now = os::now_us();
        if (animating_ >= 0) {
            animation_slice(now);
            return;
        }
        if (decoding_ != nullptr || !on_screen()) {
            return;
        }
        for (int i = 0; i < animation_count_ && animating_ < 0; i++) {
            Animation &a = *animations_[i];
            const uint64_t stay = static_cast<uint64_t>(a.decoder.frame_ms()) * 1000;
            if (now - a.frame_us < stay) {
                continue;
            }
            const Loaded &to = loaded_[a.picture];
            if (picture_on_screen(a.picture).empty() || !converter_.ready(os::memory())) {
                a.frame_us = now;
                continue;
            }
            // Counted from when it was due, so that a late turn does not
            // slow the whole; unless it has fallen behind.
            a.frame_us = now - a.frame_us > 2 * stay ? now : a.frame_us + stay;
            if (a.decoder.next_frame() != image::Error::None) {
                drop_animation(i--); // damage further on: what is shown stays
                continue;
            }
            converter_.begin(a.src_w, a.src_h, to.w, to.h, to.pixels, image::Dither::Bayer4);
            animating_ = i;
            animation_slice(now);
        }
    }

    void animation_slice(uint64_t start)
    {
        Animation &a = *animations_[animating_];
        image::Error e = image::Error::None;
        while (!a.decoder.done() && e == image::Error::None && os::now_us() - start < kDecodeSliceUs) {
            e = a.decoder.step(converter_, 4);
        }
        if (e == image::Error::None && !a.decoder.done()) {
            return;
        }
        const int which = animating_;
        animating_ = -1;
        if (e == image::Error::None) {
            page_.redraw(picture_on_screen(a.picture));
        }
        if (e != image::Error::None || !a.decoder.more_frames()) {
            drop_animation(which); // the end: the last picture stays
        }
    }

    // The picture stays as it is; what played it is given back.
    void drop_animation(int index)
    {
        animation_bytes_ -= animations_[index]->memory;
        end_animation(animations_[index]);
        for (int i = index + 1; i < animation_count_; i++) {
            animations_[i - 1] = animations_[i];
        }
        animation_count_--;
    }

    // Gives back what decoding borrowed. `shown`: the picture is whole and
    // takes its place in the page.
    void end_decode(bool shown)
    {
        decoder_.release(os::memory());
        // A picture that will move: it plays from now, if it came whole.
        if (starting_ != nullptr) {
            if (shown && decoding_ != nullptr && starting_->decoder.more_frames()) {
                starting_->picture = picture_at_;
                starting_->frame_us = os::now_us();
                animation_bytes_ += starting_->memory;
                animations_[animation_count_++] = starting_;
            } else {
                end_animation(starting_);
            }
            starting_ = nullptr;
        }
        if (converting_) {
            if (animation_count_ == 0) { // they go on needing it
                converter_.release(os::memory());
            }
            converting_ = false;
        }
        picture_fetch_.stop();
        os::release(file_bytes_);
        file_bytes_ = nullptr;
        end_direct();
        if (!shown || decoding_ == nullptr) {
            os::release(decoding_);
            decoding_ = nullptr;
            return;
        }
        loaded_[picture_at_] = {decoding_, static_cast<uint16_t>(decode_w_), static_cast<uint16_t>(decode_h_)};
        loaded_bytes_ += static_cast<size_t>(decode_w_) * static_cast<size_t>(decode_h_);
        decoding_ = nullptr;
        doc_.set_picture_size(picture_at_, decode_w_, decode_h_);
        // The same words stay at the top while the lines below make room.
        const int first = doc_.first_at(top_);
        const int into = first < doc_.item_count() ? top_ - doc_.item(first).y : 0;
        doc_.layout(doc_.width());
        if (top_ != 0 && first < doc_.item_count()) {
            top_ = doc_.item(first).y + into;
        }
        update_scroll();
        page_.redraw();
        update_caret();
    }

    // The bytes of a picture opened by itself are the page fetch's.
    void end_direct()
    {
        if (direct_) {
            direct_ = false;
            fetch_.stop();
        }
    }

    // Stops fetching pictures; those that have come stay.
    void stop_picture()
    {
        if (picture_at_ >= 0) {
            end_decode(false);
            picture_at_ = -1;
        }
        end_direct();
    }

    void forget_pictures()
    {
        stop_picture();
        while (animation_count_ > 0) {
            drop_animation(animation_count_ - 1);
        }
        animating_ = -1;
        converter_.release(os::memory());
        for (int i = 0; i < loaded_count_; i++) {
            os::release(loaded_[i].pixels);
        }
        os::release(loaded_);
        loaded_ = nullptr;
        loaded_count_ = 0;
        loaded_bytes_ = 0;
    }

    // A page of the browser's own that says what went wrong.
    void trouble(const char *url, const char *why, bool secure)
    {
        snprintf(error_, sizeof(error_), "%s", why);
        wanted_top_ = 0;
        // The page is written in borrowed memory: it is some 3 KB, and the
        // reading of it follows on the same stack.
        constexpr size_t kPage = 1400, kQuery = www::kMaxUrl, kLink = www::kMaxUrl + 100;
        char *page = static_cast<char *>(os::allocate(kPage));
        char *query = static_cast<char *>(os::allocate(kQuery + kLink));
        if (page == nullptr || query == nullptr) {
            os::release(page);
            os::release(query);
            show(url, "<h2>The page could not be opened</h2>", 37, false);
            set_status("The page could not be opened.");
            return;
        }
        char *link = query + kQuery;
        char shown[200];
        escape(url, shown, sizeof(shown));
        int n = snprintf(page, kPage,
                         "<html><head><title>Page not opened</title></head><body><h2>The page could not be opened</h2>"
                         "<p>%s</p><p><tt>%s</tt></p>",
                         error_, shown);
        if (secure && n < static_cast<int>(kPage)) {
            // FrogFind reads secure pages for browsers like this one.
            snprintf(query, kQuery, "http://frogfind.com/?q=");
            www::append_escaped(query, strlen(query), kQuery, url);
            escape(query, link, kLink);
            n += snprintf(page + n, kPage - static_cast<size_t>(n),
                          "<p>Most of today's web is secure only. <a href=\"%s\">Look for this page with FrogFind</a>, "
                          "which reads such pages and hands them on in a form this browser can show.</p>",
                          link);
        }
        if (n < static_cast<int>(kPage)) {
            snprintf(page + n, kPage - static_cast<size_t>(n), "</body></html>");
        }
        os::release(query);
        show(url, page, strlen(page), false);
        os::release(page);
        set_status("The page could not be opened.");
    }
    // Text for inside a page: no tags, no entities.
    static void escape(const char *text, char *out, size_t capacity)
    {
        size_t n = 0;
        for (; *text != 0 && n + 7 < capacity; text++) {
            const char *as = *text == '<' ? "&lt;" : *text == '>' ? "&gt;" : *text == '&' ? "&amp;"
                             : *text == '"' ? "&quot;" : nullptr;
            if (as != nullptr) {
                n += static_cast<size_t>(snprintf(out + n, capacity - n, "%s", as));
            } else {
                out[n++] = *text;
            }
        }
        out[n] = 0;
    }

    void stop_loading(const char *say)
    {
        if (fetch_.busy() || picture_at_ >= 0) {
            stop_picture();
            fetch_.stop();
            stop_timer();
            set_address(current_);
            set_status(say);
            update_buttons();
        }
    }
    // The timer has nothing more to do, unless pictures are moving.
    void stop_timer()
    {
        if (timer_ != 0 && animation_count_ == 0) {
            os::stop_timer(timer_);
            timer_ = 0;
        }
    }

    /* ---- Forms ---- */

    void click_field(int number)
    {
        www::Field &field = doc_.field(number);
        switch (field.type) {
        case www::FieldType::CheckBox:
            field.checked = !field.checked;
            page_.redraw();
            break;
        case www::FieldType::Radio:
            doc_.choose_radio(number);
            page_.redraw();
            break;
        case www::FieldType::Select:
            // ponytail: a click takes the next choice; a list that drops is for later.
            if (field.option_count > 0) {
                field.chosen = static_cast<uint16_t>((field.chosen + 1) % field.option_count);
                page_.redraw();
            }
            break;
        case www::FieldType::Reset:
            doc_.reset(field.form);
            page_.redraw();
            break;
        case www::FieldType::Submit: submit(number); break;
        default: break;
        }
    }
    void submit(int number)
    {
        char url[www::kMaxUrl];
        bool posts = false;
        if (doc_.submit(number, current_, url, sizeof(url), posts)) {
            visit(url);
        } else if (posts) {
            message_box(kName, "This form sends its answers in a way (POST)\nthat the browser does not do yet.");
        } else {
            set_status("The form cannot be sent: its address is too long.");
        }
    }
    void set_typing(int field)
    {
        if (field != typing_) {
            typing_ = field;
            page_.redraw();
        }
        update_caret();
    }
    int item_of_field(int field) const
    {
        for (int i = 0; i < doc_.item_count(); i++) {
            if (doc_.item(i).kind == www::Kind::Field && doc_.item(i).extra == field) {
                return i;
            }
        }
        return -1;
    }
    void redraw_field(int field)
    {
        const int i = item_of_field(field);
        if (i >= 0) {
            const www::Item &item = doc_.item(i);
            page_.redraw({item.x, item.y - top_, item.w, item.line});
        }
        update_caret();
    }
    void redraw_pressed()
    {
        if (pressed_ >= 0 && pressed_ < doc_.item_count()) {
            const www::Item &item = doc_.item(pressed_);
            page_.redraw({item.x, item.y - top_, item.w, item.line});
        }
    }
    // What of a text field's text is shown: the end of it, if it is too long.
    static const char *visible_tail(const char *text, int room)
    {
        int count = 0;
        for (const char *p = text; *p != 0; p++) {
            count += (static_cast<unsigned char>(*p) & 0xC0) != 0x80;
        }
        while (count > room && *text != 0) {
            text++;
            while ((static_cast<unsigned char>(*text) & 0xC0) == 0x80) {
                text++;
            }
            count--;
        }
        return text;
    }
    // Where the text cursor is, for the system's blink.
    void update_caret()
    {
        const int i = typing_ >= 0 ? item_of_field(typing_) : -1;
        if (i < 0) {
            page_.set_caret({0, 0, 0, 0});
            return;
        }
        const www::Item &item = doc_.item(i);
        const www::Field &field = doc_.field(typing_);
        const char *tail = visible_tail(field.typed, field.size);
        const int x = item.x + 4 + app::Canvas::text_width(tail);
        page_.set_caret({x, item.y - top_ + (item.line - item.h) + 2, 1, item.h - 4});
    }

    /* ---- Painting ---- */

    void paint_item(app::Canvas &c, int index, const www::Item &item)
    {
        const int y = item.y - top_ + (item.line - 2 - item.h > 0 ? item.line - 2 - item.h : 0);
        switch (item.kind) {
        case www::Kind::Word:
            {
                const char *text = doc_.text(item);
                const bool bold = (item.style & www::kBold) != 0;
                if (item.scale == 1 && !(item.style & www::kItalic)) {
                    c.text(item.x, y, text, item.colour, bold);
                } else {
                    c.text_scaled(item.x, y, text, item.colour, bold, (item.style & www::kItalic) != 0, item.scale);
                }
                if (item.style & (www::kUnderline | www::kStrike)) {
                    // The line runs on under the space before a word of the same link.
                    int from = item.x;
                    if ((item.style & www::kSpace) && index > 0) {
                        const www::Item &before = doc_.item(index - 1);
                        if (before.kind == www::Kind::Word && before.y == item.y && before.link == item.link &&
                            (before.style & (www::kUnderline | www::kStrike)) ==
                                (item.style & (www::kUnderline | www::kStrike))) {
                            from = before.x + before.w;
                        }
                    }
                    if (item.style & www::kUnderline) {
                        c.fill({from, y + item.h - item.scale, item.x + item.w - from, item.scale}, item.colour);
                    }
                    if (item.style & www::kStrike) {
                        c.fill({from, y + item.h / 2, item.x + item.w - from, item.scale}, item.colour);
                    }
                }
            }
            break;
        case www::Kind::Rule:
            c.hline(item.x, item.y - top_ + 3, item.w, gfx::kDarkGrey);
            c.hline(item.x, item.y - top_ + 4, item.w, doc_.background() == gfx::kWhite ? gfx::kGrey : gfx::kWhite);
            break;
        case www::Kind::Marker:
            if (item.extra == 0) {
                c.fill({item.x + 1, y + 4, 4, 4}, item.colour);
            } else {
                char number[12];
                snprintf(number, sizeof(number), "%u.", static_cast<unsigned>(item.extra));
                c.text(item.x, y, number, item.colour);
            }
            break;
        case www::Kind::Field: paint_field(c, index, item, y); break;
        case www::Kind::Image:
            {
                const gfx::Rect box = {item.x, y, item.w, item.h};
                if (item.extra < loaded_count_ && loaded_[item.extra].pixels != nullptr) {
                    const Loaded &picture = loaded_[item.extra];
                    app::Canvas inside = c.sub(box);
                    inside.blit({picture.w, picture.h, picture.pixels, -1}, 0, 0);
                    break;
                }
                c.edge(box, gfx::kDarkGrey, gfx::kDarkGrey);
                app::Canvas inside = c.sub({box.x + 2, box.y + 1, box.w - 4, box.h - 2});
                inside.text(1, 0, doc_.text(doc_.picture(item.extra).alt), item.link != 0 ? item.colour : gfx::kDarkGrey);
            }
            break;
        default: break;
        }
    }

    void paint_field(app::Canvas &c, int index, const www::Item &item, int y)
    {
        const www::Field &field = doc_.field(item.extra);
        const gfx::Rect box = {item.x, y, item.w, item.h};
        switch (field.type) {
        case www::FieldType::Text:
        case www::FieldType::Password:
            {
                c.sunken(box);
                c.fill({box.x + 2, box.y + 2, box.w - 4, box.h - 4}, gfx::kWhite);
                char dots[www::Document::kTypedBytes];
                const char *text = field.typed;
                if (field.type == www::FieldType::Password) {
                    size_t n = 0;
                    for (const char *p = field.typed; *p != 0; p++) {
                        if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80) {
                            dots[n++] = '*';
                        }
                    }
                    dots[n] = 0;
                    text = dots;
                }
                const char *tail = visible_tail(text, field.size);
                const int end = c.text(box.x + 4, box.y + 3, tail, gfx::kBlack);
                if (typing_ == item.extra && page_.has_keys() && caret_lit()) {
                    c.vline(end, box.y + 3, box.h - 6, gfx::kBlack);
                }
            }
            break;
        case www::FieldType::CheckBox:
            c.sunken(box);
            c.fill({box.x + 2, box.y + 2, box.w - 4, box.h - 4}, gfx::kWhite);
            if (field.checked) {
                static const char *const kTick[] = {"......X", ".....XX", "X...XXX", "XX.XXX.", "XXXXX..", ".XXX...", "..X...."};
                c.pattern(box.x + 3, box.y + 3, kTick, 7, gfx::kBlack);
            }
            break;
        case www::FieldType::Radio:
            {
                static const char *const kRing[] = {"....XXXX....", "..XX....XX..", ".X........X.", ".X........X.",
                                                    "X..........X", "X..........X", "X..........X", "X..........X",
                                                    ".X........X.", ".X........X.", "..XX....XX..", "....XXXX...."};
                static const char *const kPaper[] = {"....XXXX....", "..XXXXXXXX..", ".XXXXXXXXXX.", ".XXXXXXXXXX.",
                                                     "XXXXXXXXXXXX", "XXXXXXXXXXXX", "XXXXXXXXXXXX", "XXXXXXXXXXXX",
                                                     ".XXXXXXXXXX.", ".XXXXXXXXXX.", "..XXXXXXXX..", "....XXXX...."};
                static const char *const kDot[] = {".XX.", "XXXX", "XXXX", ".XX."};
                c.pattern(box.x, box.y, kPaper, 12, gfx::kWhite);
                c.pattern(box.x, box.y, kRing, 12, gfx::kDarkGrey);
                if (field.checked) {
                    c.pattern(box.x + 4, box.y + 4, kDot, 4, gfx::kBlack);
                }
            }
            break;
        case www::FieldType::Select:
            {
                c.sunken(box);
                c.fill({box.x + 2, box.y + 2, box.w - 4, box.h - 4}, gfx::kWhite);
                c.text(box.x + 4, box.y + 3, doc_.shown(field), gfx::kBlack);
                const gfx::Rect arrow = {box.x + box.w - 16, box.y + 2, 14, box.h - 4};
                c.button(arrow, pressed_ == index ? gfx::ButtonLook::Pressed : gfx::ButtonLook::Raised);
                static const char *const kDown[] = {"XXXXXXX", ".XXXXX.", "..XXX..", "...X..."};
                c.pattern(arrow.x + 3, arrow.y + 5, kDown, 4, gfx::kBlack);
            }
            break;
        default:
            {
                const bool down = pressed_ == index;
                c.button(box, down ? gfx::ButtonLook::Pressed : gfx::ButtonLook::Raised);
                const char *label = doc_.shown(field);
                const int w = app::Canvas::text_width(label);
                app::Canvas inside = c.sub({box.x + 2, box.y + 2, box.w - 4, box.h - 4});
                inside.text((box.w - 4 - w) / 2 + (down ? 1 : 0), 2 + (down ? 1 : 0), label, gfx::kBlack);
            }
            break;
        }
    }

    /* ---- The window's own things ---- */

    gfx::Rect page_size() const
    {
        const gfx::Rect c = client_size();
        const int view = c.h - kBarHeight - kStatusHeight;
        return {0, 0, c.w - os::ui::kScrollBarThickness, view > 0 ? view : 0};
    }
    int view_height() const { return page_size().h; }
    void update_scroll()
    {
        const int view = view_height();
        const int most = doc_.height() > view ? doc_.height() - view : 0;
        top_ = top_ < 0 ? 0 : top_ > most ? most : top_;
        scroll_.set_range(most, view);
        scroll_.set_value(top_);
    }
    void scroll_to(int top)
    {
        const int before = top_;
        top_ = top;
        update_scroll();
        if (top_ != before) {
            page_.redraw();
            update_caret();
        }
    }
    void set_status(const char *text) { status_.set(text); }
    void status_host(const char *what, const char *url)
    {
        char host[64] = "";
        int port = 0;
        const char *path = nullptr;
        www::split_http(url, host, sizeof(host), port, path);
        char text[96];
        snprintf(text, sizeof(text), "%s%s...", what, host);
        set_status(text);
    }
    void set_address(const char *url)
    {
        address_.set(url);
        address_.get(shown_address_, sizeof(shown_address_));
    }
    void update_buttons()
    {
        back_.set_enabled(at_ > 0);
        forward_.set_enabled(at_ + 1 < count_);
        stop_.set_enabled(fetch_.busy() || picture_at_ >= 0);
    }

    /* ---- Menus ---- */

    static BrowserWindow *self(void *context) { return static_cast<BrowserWindow *>(context); }
    static void menu_open(void *context)
    {
        open_file_dialog(self(context)->number(), "C:\\", &BrowserWindow::file_chosen, context);
    }
    static void file_chosen(const char *path, void *context) { self(context)->open_typed(path); }
    static void menu_close(void *context) { self(context)->close(); }
    static void menu_back(void *context) { self(context)->move(-1); }
    static void menu_forward(void *context) { self(context)->move(1); }
    static void menu_home(void *context) { self(context)->go_home(); }
    static void menu_reload(void *context) { self(context)->reload(); }
    static void menu_stop(void *context) { self(context)->stop_loading("Stopped."); }

    // Favourites: the pages kept in C:\.config\browser.ini.
    struct Favourite {
        BrowserWindow *window;
        int index;
    };
    void fill_favourites()
    {
        int n = 0;
        favourite_items_[n++] = {"&Add This Page", &BrowserWindow::menu_add_favourite, this, nullptr, nullptr, 0};
        favourite_items_[n++] = {"&Remove This Page", &BrowserWindow::menu_remove_favourite, this, nullptr, nullptr, 0};
        favourite_count_ = 0;
        for (int i = 0; i < kFavourites; i++) {
            char key[8], name_key[8];
            snprintf(key, sizeof(key), "url%d", i + 1);
            snprintf(name_key, sizeof(name_key), "name%d", i + 1);
            if (!settings_.get("favourites", key, favourite_urls_[favourite_count_], sizeof(favourite_urls_[0])) ||
                favourite_urls_[favourite_count_][0] == 0) {
                continue;
            }
            char name[40] = "";
            settings_.get("favourites", name_key, name, sizeof(name));
            // '&' would be read as an access letter.
            for (char *p = name; *p != 0; p++) {
                if (*p == '&') {
                    *p = '+';
                }
            }
            snprintf(favourite_names_[favourite_count_], sizeof(favourite_names_[0]), "%.38s",
                     name[0] != 0 ? name : favourite_urls_[favourite_count_]);
            if (favourite_count_ == 0) {
                favourite_items_[n++] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
            }
            favourite_slots_[favourite_count_] = {this, favourite_count_};
            favourite_items_[n++] = {favourite_names_[favourite_count_], &BrowserWindow::menu_favourite,
                                     &favourite_slots_[favourite_count_], nullptr, nullptr, 0};
            favourite_count_++;
        }
        favourites_.count = n;
    }
    void save_favourites()
    {
        for (int i = 0; i < kFavourites; i++) {
            char key[8], name_key[8];
            snprintf(key, sizeof(key), "url%d", i + 1);
            snprintf(name_key, sizeof(name_key), "name%d", i + 1);
            settings_.set("favourites", key, i < favourite_count_ ? favourite_urls_[i] : "");
            settings_.set("favourites", name_key, i < favourite_count_ ? favourite_names_[i] : "");
        }
        if (!save_settings("browser", settings_)) {
            message_box(kName, "The favourites could not be saved.");
        }
        fill_favourites();
    }
    static void menu_add_favourite(void *context)
    {
        BrowserWindow *w = self(context);
        if (w->current_[0] == 0 || strlen(w->current_) >= sizeof(w->favourite_urls_[0])) {
            message_box(kName, "This page's address is too long to keep.");
            return;
        }
        for (int i = 0; i < w->favourite_count_; i++) {
            if (strcmp(w->favourite_urls_[i], w->current_) == 0) {
                return; // it is there already
            }
        }
        if (w->favourite_count_ == kFavourites) {
            message_box(kName, "The favourites are full.\nRemove one first.");
            return;
        }
        snprintf(w->favourite_urls_[w->favourite_count_], sizeof(w->favourite_urls_[0]), "%.199s", w->current_);
        snprintf(w->favourite_names_[w->favourite_count_], sizeof(w->favourite_names_[0]), "%.38s",
                 w->doc_.title()[0] != 0 ? w->doc_.title() : w->current_);
        w->favourite_count_++;
        w->save_favourites();
    }
    static void menu_remove_favourite(void *context)
    {
        BrowserWindow *w = self(context);
        for (int i = 0; i < w->favourite_count_; i++) {
            if (strcmp(w->favourite_urls_[i], w->current_) == 0) {
                for (int k = i; k + 1 < w->favourite_count_; k++) {
                    memcpy(w->favourite_urls_[k], w->favourite_urls_[k + 1], sizeof(w->favourite_urls_[0]));
                    memcpy(w->favourite_names_[k], w->favourite_names_[k + 1], sizeof(w->favourite_names_[0]));
                }
                w->favourite_count_--;
                w->save_favourites();
                return;
            }
        }
    }
    static void menu_favourite(void *context)
    {
        const Favourite *f = static_cast<const Favourite *>(context);
        f->window->visit(f->window->favourite_urls_[f->index]);
    }

    app::Button back_, forward_, reload_, stop_, home_;
    app::Label address_label_;
    app::TextField address_;
    app::Button go_;
    app::Picture page_;
    app::ScrollBar scroll_;
    app::Plain status_;

    OsWire wire_;
    www::Document doc_;
    www::Fetch fetch_;
    int timer_ = 0;
    int top_ = 0;        // the page's y at the top of the view
    int wanted_top_ = 0; // where the page being loaded is to be shown from
    int pressed_ = -1;   // the item the mouse went down on
    int typing_ = -1;    // the text field the keys go to
    size_t shown_kb_ = 0;

    www::Fetch picture_fetch_;
    Loaded *loaded_ = nullptr; // one for each of the page's first pictures
    int loaded_count_ = 0;
    size_t loaded_bytes_ = 0;
    int picture_at_ = -1;      // the one being fetched or decoded; -1 for none
    int pictures_failed_ = 0;
    bool direct_ = false;      // the page is one picture, and fetch_ holds its bytes
    image::Decoder decoder_;
    image::Converter converter_;
    bool converting_ = false;
    FromBytes source_;
    void *file_bytes_ = nullptr;
    uint8_t *decoding_ = nullptr; // the pixels of the picture being decoded
    int decode_w_ = 0, decode_h_ = 0;
    Animation *animations_[kMaxAnimations] = {}; // the pictures of the page that move
    int animation_count_ = 0;
    size_t animation_bytes_ = 0;
    Animation *starting_ = nullptr; // the one whose first picture is being decoded
    int animating_ = -1;            // the one whose next picture is being decoded; -1 for none

    char current_[www::kMaxUrl] = "";  // the page shown
    char loading_[www::kMaxUrl] = "";
    char shown_address_[2 * app::TextField::kCapacity + 1] = "";
    char title_[96] = "";
    char error_[160] = "";
    char history_[kHistory][www::kMaxUrl];
    int tops_[kHistory] = {};
    int at_ = 0, count_ = 0;

    fs::Ini settings_;
    char favourite_urls_[kFavourites][200];
    char favourite_names_[kFavourites][40];
    Favourite favourite_slots_[kFavourites];
    int favourite_count_ = 0;

    wm::MenuItem file_items_[3];
    wm::MenuItem go_items_[6];
    wm::MenuItem favourite_items_[kFavourites + 3];
    wm::Menu file_ = {file_items_, 3};
    wm::Menu go_menu_ = {go_items_, 6};
    wm::Menu favourites_ = {favourite_items_, 2};
    const wm::MenuBarEntry entries_[3] = {{"&File", &file_}, {"&Go", &go_menu_}, {"F&avourites", &favourites_}};
    const wm::MenuBar bar_ = {entries_, 3};
};

} // namespace

void open_browser(const char *address)
{
    // On the browser's own task where it has one: whoever asks (the File
    // manager, with a page on a drive) lends neither its process nor its
    // stack to the reading of a page.
    const PathArg given(address);
    in_program(kName, [given] {
        const char *address = given.get();
        if (!app::bring_forward(g_browser)) {
            app::Look look;
            look.title = kName;
            look.width = 600;
            look.height = 420;
            look.icon = &gfx::browser_icon_16();
            look.flags = app::kMenuBar;
            g_browser = app::open<BrowserWindow>(look);
            if (g_browser == app::kNone) {
                no_memory_for(kName);
                return;
            }
            if (address == nullptr) {
                address = kHome;
            }
        }
        if (address != nullptr) {
            if (BrowserWindow *w = app::program<BrowserWindow>(g_browser)) {
                w->open_typed(address);
            }
        }
    });
}

void open_browser_home() { open_browser(nullptr); }

} // namespace shell
