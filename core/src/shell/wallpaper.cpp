// The desktop's picture (backlog item 87): a file that Display names, shown
// over the desktop's colour. This is the system's half; the window is
// Display's (display.cpp), which asks through os::set_wallpaper().
//
// The file is decoded once, by a background job, into a picture of the
// screen's own colours that stays in memory for as long as it is shown (one
// byte per pixel: 300 KB for a whole screen of 640 x 480, 469 KB at
// 800 x 600). The window server paints from it (wm::Wm::set_wallpaper), so
// uncovering a part of the desktop is a copy of rows, not a decoding.
//
// The colour is what there is whenever the picture cannot be shown: no file
// named, the file gone, moved or renamed, not a picture, or no memory for
// it. Nothing is asked and nothing is written: the setting keeps the name,
// and if the file comes back the picture does too. The folder's change
// notices say when to look again (desktop.cpp, settle()).
//
// Where it stands is Display's Position (the owner's choice, 2026-11-20).
// Centre: in the middle at its own size, the colour around it; a picture
// larger than the screen is made smaller to fit, keeping its shape. Tile:
// the same picture side by side from the top left corner. Stretch: made the
// size of the screen, whatever its shape, larger if need be (Enlarger). A file
// named for a screen, "Mir 800x600.jpg", stands for its sisters: on a screen
// of 640 x 480 "Mir 640x480.jpg" beside it is shown, if it is there.
#include <cctype>
#include <cstdio>
#include <cstring>

#include "core/image/convert.hpp"
#include "core/image/image.hpp"
#include "core/job/job.hpp"
#include "core/os/os.hpp"
#include "internal.hpp"

namespace shell {

void fit_inside(int width, int height, int max_w, int max_h, int &w, int &h)
{
    w = width;
    h = height;
    if (w > max_w || h > max_h) {
        if (static_cast<long>(w) * max_h > static_cast<long>(h) * max_w) {
            h = static_cast<int>(static_cast<long>(h) * max_w / w);
            w = max_w;
        } else {
            w = static_cast<int>(static_cast<long>(w) * max_h / h);
            h = max_h;
        }
    }
    w = w < 1 ? 1 : w;
    h = h < 1 ? 1 : h;
}

namespace {

// Makes a picture larger on its way from the decoder to the converter,
// which only makes smaller: rows of src_w x src_h come in and rows of
// w x h go on, each pixel mixed from the four around its place (bilinear).
// Either side may stay as it is. Integers only, as the decoders, so the
// board and the PC make the same pixels.
class Enlarger : public image::RowSink {
public:
    bool ready(ports::Memory &memory, int src_w, int src_h, int w, int h, image::RowSink &to)
    {
        src_w_ = src_w;
        src_h_ = src_h;
        w_ = w;
        h_ = h;
        to_ = &to;
        next_ = 0;
        rows_ = static_cast<uint8_t *>(memory.allocate(static_cast<size_t>(w) * 3 * 3));
        before_ = rows_;
        now_ = rows_ != nullptr ? rows_ + static_cast<size_t>(w) * 3 : nullptr;
        return rows_ != nullptr;
    }
    void release(ports::Memory &memory)
    {
        if (rows_ != nullptr) {
            memory.release(rows_);
            rows_ = nullptr;
        }
    }
    void row(int y, const uint8_t *rgb) override
    {
        // Sideways first, into `now_`.
        if (w_ == src_w_) {
            memcpy(now_, rgb, static_cast<size_t>(w_) * 3);
        } else {
            for (int x = 0; x < w_; x++) {
                const long at = w_ > 1 ? static_cast<long>(x) * (src_w_ - 1) * 256 / (w_ - 1) : 0;
                const int left = static_cast<int>(at >> 8), f = static_cast<int>(at & 255);
                const uint8_t *a = rgb + left * 3, *b = a + (left + 1 < src_w_ ? 3 : 0);
                for (int k = 0; k < 3; k++) {
                    now_[x * 3 + k] = static_cast<uint8_t>((a[k] * (256 - f) + b[k] * f) >> 8);
                }
            }
        }
        // Then every row of the result that lies at or above this one.
        uint8_t *mixed = rows_ + static_cast<size_t>(w_) * 6;
        for (; next_ < h_; next_++) {
            const long at = h_ > 1 ? static_cast<long>(next_) * (src_h_ - 1) * 256 / (h_ - 1) : 0;
            const int above = static_cast<int>(at >> 8), f = static_cast<int>(at & 255);
            if (above == y && (f == 0 || y == src_h_ - 1)) {
                to_->row(next_, now_);
            } else if (above == y - 1) {
                for (int i = 0; i < w_ * 3; i++) {
                    mixed[i] = static_cast<uint8_t>((before_[i] * (256 - f) + now_[i] * f) >> 8);
                }
                to_->row(next_, mixed);
            } else {
                break; // it needs a row that has not come yet
            }
        }
        uint8_t *swap = before_;
        before_ = now_;
        now_ = swap;
    }

private:
    int src_w_ = 0, src_h_ = 0, w_ = 0, h_ = 0, next_ = 0;
    image::RowSink *to_ = nullptr;
    uint8_t *rows_ = nullptr; // three rows of the result: the one before, this one, the mix
    uint8_t *before_ = nullptr, *now_ = nullptr;
};

} // namespace

image::Error decode_fitted(const char *path, int max_w, int max_h, bool stretch, int part, int of,
                           const uint8_t background[3], const job::Job *job, FittedPicture &out)
{
    out = {};
    ports::Memory &memory = *os::installed_kit().memory;
    image::Decoder decoder;
    image::Converter converter;
    Enlarger enlarger;
    image::RowSink *sink = &converter;
    image::Info info;
    int from_w = 0, from_h = 0; // what the converter is given
    image::Error e = decoder.open(storage(), path, memory, info, background);
    if (e == image::Error::None) {
        if (stretch) {
            out.width = max_w;
            out.height = max_h;
        } else {
            fit_inside(info.width, info.height, max_w, max_h, out.width, out.height);
        }
        out.width = out.width * part / of > 0 ? out.width * part / of : 1;
        out.height = out.height * part / of > 0 ? out.height * part / of : 1;
        out.pixels = static_cast<uint8_t *>(memory.allocate(static_cast<size_t>(out.width) * out.height));
        if (out.pixels == nullptr || !converter.ready(memory)) {
            e = image::Error::NoMemory;
        }
        // A side that has to grow grows before the converter; the other
        // is made smaller by it.
        from_w = info.width > out.width ? info.width : out.width;
        from_h = info.height > out.height ? info.height : out.height;
        if (e == image::Error::None && (from_w != info.width || from_h != info.height)) {
            sink = &enlarger;
            if (!enlarger.ready(memory, info.width, info.height, from_w, from_h, converter)) {
                e = image::Error::NoMemory;
            }
        }
    }
    if (e == image::Error::None) {
        // Floyd-Steinberg, the Picture viewer's own first choice: the
        // picture is made once and copied from then on, so a dither that
        // looks at its neighbours costs nothing later.
        converter.begin(from_w, from_h, out.width, out.height, out.pixels, image::Dither::FloydSteinberg);
        int rows = 0;
        while (e == image::Error::None && !decoder.done()) {
            e = decoder.step(*sink, 8);
            if (job != nullptr && job->stopping()) {
                e = image::Error::NotFound; // the machine is going down: no picture
            }
            // The decoding of a large photograph is seconds of work on the
            // board: the others get the processor now and then.
            if (job != nullptr && job->own_task() && (rows += 8) >= 32) {
                rows = 0;
                job::jobs().rest(1);
            }
        }
    }
    decoder.release(memory);
    converter.release(memory);
    enlarger.release(memory);
    if (e != image::Error::None) {
        release_fitted(out);
    }
    return e;
}

void release_fitted(FittedPicture &picture)
{
    if (picture.pixels != nullptr) {
        os::installed_kit().memory->release(picture.pixels);
    }
    picture = {};
}

void wallpaper_file(const char *path, int width, int height, char *out, int capacity)
{
    fs::DirEntry entry;
    char sister[fs::kMaxPath];
    const bool there = wallpaper_for_screen(path, width, height, sister, sizeof(sister)) &&
                       storage().stat(sister, entry) == ports::FsError::Ok && !entry.folder;
    snprintf(out, static_cast<size_t>(capacity), "%s", there ? sister : path);
}

bool wallpaper_for_screen(const char *path, int width, int height, char *out, int capacity)
{
    // "<anything><space, - or _><digits>x<digits>.<extension>"
    const char *dot = strrchr(path, '.');
    const char *name = fs::path::name(path);
    if (dot == nullptr || dot <= name) {
        return false;
    }
    const char *p = dot;
    while (p > name && isdigit(static_cast<unsigned char>(p[-1]))) {
        p--;
    }
    if (p == dot || p == name || (p[-1] != 'x' && p[-1] != 'X')) {
        return false;
    }
    const char *x = --p;
    while (p > name && isdigit(static_cast<unsigned char>(p[-1]))) {
        p--;
    }
    if (p == x || p == name || (p[-1] != ' ' && p[-1] != '-' && p[-1] != '_')) {
        return false;
    }
    const int n = snprintf(out, static_cast<size_t>(capacity), "%.*s%dx%d%s", static_cast<int>(p - path), path,
                           width, height, dot);
    return n > 0 && n < capacity;
}

namespace {

// What the job is given and what it brings back. The job's from start_load()
// until loaded().
struct Load {
    char path[fs::kMaxPath]; // the file named in the settings
    int width, height;       // the screen's
    uint8_t background[3];   // behind a picture with clear parts
    os::Wallpaper how;
    char shown[fs::kMaxPath]; // the file that was up before, how, and its size and date
    os::Wallpaper shown_how;
    uint32_t shown_size;
    int64_t shown_modified;
    // Back:
    FittedPicture picture;
    char file[fs::kMaxPath]; // the file read: `path`, or its sister for this screen
    uint32_t size;
    int64_t modified;
};
enum Result : uint32_t { kNewPicture, kSamePicture, kNoPicture, kNoRoom };

char g_path[fs::kMaxPath] = ""; // the picture asked for; "": none
os::Wallpaper g_how = os::Wallpaper::Centre;
os::Wallpaper g_shown_how = os::Wallpaper::Centre; // how g_picture was made
FittedPicture g_picture;        // the one the window server shows
char g_file[fs::kMaxPath] = ""; // the file it was read from, and what that file was like then
uint32_t g_size = 0;
int64_t g_modified = 0;
int g_made_w = 0, g_made_h = 0; // the screen it was made for
Load g_load;
bool g_busy = false;   // a job is at work on g_load
bool g_again = false;  // what it was asked is out of date
bool g_owed = false;   // no job could be started: tried again at the next notice

void start_load();

void take_down()
{
    manager().set_wallpaper(nullptr, 0, 0);
    release_fitted(g_picture);
    g_file[0] = '\0';
}

// The job: any task.
uint32_t load_work(const job::Job &job, void *)
{
    Load &l = g_load;
    fs::DirEntry entry;
    wallpaper_file(l.path, l.width, l.height, l.file, sizeof(l.file));
    if (storage().stat(l.file, entry) != ports::FsError::Ok || entry.folder) {
        return kNoPicture; // gone, moved or renamed
    }
    l.size = entry.size;
    l.modified = entry.modified;
    if (strcmp(l.file, l.shown) == 0 && l.how == l.shown_how && l.size == l.shown_size &&
        l.modified == l.shown_modified) {
        return kSamePicture; // something else changed in its folder
    }
    const image::Error e = decode_fitted(l.file, l.width, l.height, l.how == os::Wallpaper::Stretch, 1, 1, l.background, &job,
                                         l.picture);
    return e == image::Error::None ? kNewPicture : e == image::Error::NoMemory ? kNoRoom : kNoPicture;
}

// The UI task, as the desktop.
void loaded(void *, uint32_t result, bool)
{
    g_busy = false;
    manager().hold_arrival(false);
    Load &l = g_load;
    if (g_again) {
        // Another picture, another screen or none was asked for meanwhile.
        g_again = false;
        release_fitted(l.picture);
        start_load();
        return;
    }
    if (result == kSamePicture) {
        return;
    }
    if (result != kNewPicture) {
        const bool had = g_picture.pixels != nullptr;
        take_down();
        // No room, perhaps only for a second picture beside the one that
        // was shown: once more, now that it is gone.
        if (result == kNoRoom && had) {
            start_load();
        }
        return;
    }
    FittedPicture old = g_picture;
    g_picture = l.picture;
    l.picture = {};
    snprintf(g_file, sizeof(g_file), "%s", l.file);
    g_size = l.size;
    g_modified = l.modified;
    g_made_w = l.width;
    g_made_h = l.height;
    g_shown_how = l.how;
    manager().set_wallpaper(g_picture.pixels, g_picture.width, g_picture.height, l.how == os::Wallpaper::Tile);
    release_fitted(old);
}

// The UI task, as anyone.
void start_load()
{
    if (g_path[0] == '\0') {
        return;
    }
    if (g_busy) {
        g_again = true;
        return;
    }
    Load &l = g_load;
    l = {};
    snprintf(l.path, sizeof(l.path), "%s", g_path);
    // The mode asked for, which the screen may not be in yet (at start-up
    // the settings name it before the first picture).
    const ports::VideoMode mode = os::video_mode_at(os::video_mode());
    l.width = mode.width;
    l.height = mode.height;
    const gfx::Rgb colour = manager().backdrop().colour;
    l.background[0] = colour.r;
    l.background[1] = colour.g;
    l.background[2] = colour.b;
    l.how = g_how;
    snprintf(l.shown, sizeof(l.shown), "%s", g_file);
    l.shown_how = g_shown_how;
    l.shown_size = g_size;
    l.shown_modified = g_modified;
    // The job is the desktop's, which never ends, so nothing waits for it.
    const proc::Scope as(&proc::table(), proc::table().find_by_name("desktop"));
    job::Spec spec;
    spec.name = "wallpaper";
    spec.work = &load_work;
    spec.done = &loaded;
    spec.stack_bytes = 12 * 1024; // the decoders and the file system's calls
    spec.priority = ports::Priority::Low; // the desktop goes on meanwhile
    // Where there are no tasks the work, and `loaded`, are over when
    // start() returns.
    g_busy = true;
    g_owed = false;
    manager().hold_arrival(true); // at start-up the desktop comes up with its picture
    if (!job::jobs().start(spec)) {
        g_busy = false;
        g_owed = true;
        manager().hold_arrival(false);
    }
}

// The screen has another size, and nothing has been painted on it yet: a
// picture made for the old one comes down and one is made for the new. At
// start-up the picture was made for the mode the settings name, and stays.
void screen_changed(void *)
{
    const ports::VideoMode mode = os::video_mode_at(os::video_mode());
    if (g_busy) {
        g_again = g_again || g_load.width != mode.width || g_load.height != mode.height;
    }
    if (g_picture.pixels != nullptr && (g_made_w != mode.width || g_made_h != mode.height)) {
        take_down();
    }
    if (g_picture.pixels == nullptr && !g_busy) {
        start_load();
    }
}

} // namespace

void set_wallpaper(const char *path, os::Wallpaper how)
{
    path = path != nullptr ? path : "";
    if (strcmp(path, g_path) == 0 && (how == g_how || path[0] == '\0')) {
        return; // Display says what it holds whenever it closes
    }
    snprintf(g_path, sizeof(g_path), "%s", path);
    g_how = how;
    if (g_path[0] == '\0') {
        take_down();
        g_again = g_busy; // what a job brings now is not wanted
        return;
    }
    // The picture shown stays until the new one is ready.
    start_load();
}

void wallpaper_files_changed(const FolderKeys &changed)
{
    if (g_path[0] == '\0') {
        return;
    }
    char folder[fs::kMaxPath];
    fs::path::parent(g_path, folder, sizeof(folder));
    if (g_owed || changed.all || changed.has(folder)) {
            start_load();
    }
}

void install_wallpaper()
{
    manager().set_screen_listener(&screen_changed, nullptr);
}

} // namespace shell
