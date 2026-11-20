// Tests for the picture decoders, against the pictures and expected pixels
// written by tools/gen_pictures.py tests <folder>:
//   test_image <folder>
// Also feeds every picture truncated and with damaged bytes: decoding must
// end in an error or finish, never crash (the PC build runs with address
// and undefined-behaviour checks).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "core/gfx/canvas.hpp"
#include "core/image/convert.hpp"
#include "core/image/image.hpp"
#include "../folder_volume.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

host::HostMemory memory;
fs::Storage storage;
std::string folder;

struct Collect : image::RowSink {
    std::vector<uint8_t> pixels;
    int width = 0, rows = 0, last_y = -1;
    bool in_order = true;
    void row(int y, const uint8_t *rgb) override
    {
        in_order = in_order && y == last_y + 1;
        last_y = y;
        pixels.insert(pixels.end(), rgb, rgb + width * 3);
        rows++;
    }
};

image::Error decode(image::Decoder &d, const char *path, image::Info &info, Collect &out)
{
    image::Error e = d.open(storage, path, memory, info);
    if (e != image::Error::None) {
        return e;
    }
    out.width = info.width;
    while (!d.done()) {
        e = d.step(out, 7);
        if (e != image::Error::None) {
            return e;
        }
    }
    return image::Error::None;
}

std::vector<uint8_t> read_all(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

double psnr(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    double se = 0;
    for (size_t i = 0; i < a.size(); i++) {
        const double d = static_cast<double>(a[i]) - b[i];
        se += d * d;
    }
    const double mse = se / static_cast<double>(a.size());
    return mse == 0 ? 99 : 10 * std::log10(255.0 * 255.0 / mse);
}

image::Error error_named(const std::string &s)
{
    if (s == "Unsupported") {
        return image::Error::Unsupported;
    }
    if (s == "TooLarge") {
        return image::Error::TooLarge;
    }
    return image::Error::Malformed;
}

// An animated GIF: each picture in turn against the pixels the generator
// worked out, how long each stays, and that it stops after the last time
// round. A screen too large for the usual working memory shows its first
// picture, and plays once the decoder is given what animation_pool() says.
int test_animation(image::Decoder &d, const std::string &name, std::istringstream &in)
{
    int w = 0, h = 0, count = 0, rounds = 0;
    in >> w >> h >> count >> rounds;
    std::vector<int> delays(count);
    for (int &ms : delays) {
        in >> ms;
    }
    const std::string path = "D:\\" + name;
    image::Info info;
    Collect got;
    image::Error e = decode(d, path.c_str(), info, got);
    CHECK(e == image::Error::None && info.several && info.width == w && info.height == h);
    const bool large = !info.animated;
    CHECK(large == (name == "anim-large.gif"));
    if (large) {
        CHECK(got.pixels == read_all(folder + "/" + name + ".0.rgb"));
        CHECK(!d.more_frames() && d.next_frame() != image::Error::None);
        CHECK(image::animation_pool(w, h) > image::kPoolBytes);
        d.use_pool(memory, image::animation_pool(w, h));
        got = Collect();
        e = decode(d, path.c_str(), info, got);
        CHECK(e == image::Error::None && info.animated);
    }
    const int total = rounds == 0 ? count * 2 + 1 : count * rounds; // "for ever": a little over twice
    for (int i = 0; i < total && e == image::Error::None; i++) {
        const int f = i % count;
        if (got.pixels != read_all(folder + "/" + name + "." + std::to_string(f) + ".rgb") || got.rows != h || !got.in_order) {
            printf("  %s: picture %d (the %dth shown) is not as expected\n", name.c_str(), f, i);
            failures++;
        }
        CHECK(d.frame_ms() == delays[f]);
        if (i + 1 == total) {
            CHECK(d.more_frames() == (rounds == 0));
            break;
        }
        CHECK(d.more_frames());
        e = d.next_frame();
        CHECK(e == image::Error::None);
        got = Collect();
        got.width = w;
        while (!d.done() && e == image::Error::None) {
            e = d.step(got, 5);
        }
        CHECK(e == image::Error::None);
    }
    // Again, as a program plays it that keeps the picture before: only the
    // rows of the part that changed are asked for, and nothing outside
    // that part is different.
    got = Collect();
    e = decode(d, path.c_str(), info, got);
    std::vector<uint8_t> kept = got.pixels;
    struct Rows : image::RowSink {
        std::vector<uint8_t> *to = nullptr;
        int width = 0, rows = 0;
        void row(int y, const uint8_t *rgb) override
        {
            memcpy(to->data() + static_cast<size_t>(y) * width * 3, rgb, static_cast<size_t>(width) * 3);
            rows++;
        }
    };
    for (int i = 1; i < count * 2 + 2 && e == image::Error::None && d.more_frames(); i++) {
        const std::vector<uint8_t> before = kept;
        e = d.next_frame();
        int part[4];
        d.changed(part);
        CHECK(part[0] >= 0 && part[1] >= 0 && part[0] + part[2] <= w && part[1] + part[3] <= h);
        Rows rows;
        rows.to = &kept;
        rows.width = w;
        if (part[3] > 0) {
            d.only_rows(part[1], part[3]);
        }
        while (!d.done() && e == image::Error::None) {
            e = d.step(rows, 5);
        }
        CHECK(e == image::Error::None && rows.rows == (part[3] > 0 ? part[3] : h));
        const std::vector<uint8_t> want = read_all(folder + "/" + name + "." + std::to_string(i % count) + ".rgb");
        bool same = kept == want, inside = true;
        for (int y = 0; y < h && want.size() == before.size(); y++) {
            for (int x = 0; x < w; x++) {
                const bool in = x >= part[0] && x < part[0] + part[2] && y >= part[1] && y < part[1] + part[3];
                inside = inside && (in || memcmp(&want[(y * w + x) * 3], &before[(y * w + x) * 3], 3) == 0);
            }
        }
        if (!same || !inside) {
            printf("  %s: picture %d by its changed part: %s\n", name.c_str(), i % count,
                   same ? "a change outside the part" : "not as expected");
            failures++;
        }
    }
    if (large) {
        d.use_pool(memory, image::kPoolBytes);
    }
    return 1;
}

void test_manifest()
{
    std::ifstream m(folder + "/manifest.txt");
    std::string line;
    int checked = 0;
    image::Decoder d;
    while (std::getline(m, line)) {
        std::istringstream in(line);
        std::string name, kind;
        in >> name >> kind;
        const std::string path = "D:\\" + name;
        image::Info info;
        Collect got;
        const image::Error e = decode(d, path.c_str(), info, got);
        if (kind == "frames") {
            checked += test_animation(d, name, in);
            continue;
        }
        if (kind == "error") {
            std::string want;
            in >> want;
            if (e != error_named(want)) {
                printf("  %s: got '%s'\n", name.c_str(), image::error_text(e));
            }
            CHECK(e == error_named(want));
            checked++;
            continue;
        }
        int w = 0, h = 0;
        in >> w >> h;
        if (e != image::Error::None) {
            printf("  %s: %s\n", name.c_str(), image::error_text(e));
        }
        CHECK(e == image::Error::None);
        CHECK(info.width == w && info.height == h && got.rows == h && got.in_order);
        const std::vector<uint8_t> want = read_all(folder + "/" + name + ".rgb");
        CHECK(want.size() == got.pixels.size());
        if (want.size() != got.pixels.size()) {
            continue;
        }
        if (kind == "exact") {
            if (want != got.pixels) {
                size_t i = 0;
                while (want[i] == got.pixels[i]) {
                    i++;
                }
                printf("  %s differs at pixel %zu (%d,%d): want %d got %d\n", name.c_str(), i / 3,
                       static_cast<int>(i / 3 % w), static_cast<int>(i / 3 / w), want[i], got.pixels[i]);
            }
            CHECK(want == got.pixels);
        } else {
            int floor = 30;
            in >> floor; // from the manifest: lower for 4:2:0
            const double p = psnr(want, got.pixels);
            if (p < floor) {
                printf("  %s: PSNR %.1f dB, wanted %d\n", name.c_str(), p, floor);
            }
            CHECK(p >= floor);
        }
        checked++;
    }
    CHECK(checked >= 30);
}

// Every picture cut short at many places and with bytes changed at random:
// each decode must finish or give an error, and never crash.
void test_damaged()
{
    std::ifstream m(folder + "/manifest.txt");
    std::string line;
    std::mt19937 random(11);
    image::Decoder d;
    int runs = 0, errors = 0;
    while (std::getline(m, line)) {
        std::istringstream in(line);
        std::string name;
        in >> name;
        const std::vector<uint8_t> good = read_all(folder + "/" + name);
        auto attempt = [&](const std::vector<uint8_t> &bytes) {
            std::ofstream(folder + "/damaged.bin", std::ios::binary)
                .write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            image::Info info;
            Collect got;
            image::Error e = decode(d, "D:\\damaged.bin", info, got);
            errors += e != image::Error::None;
            runs++;
            // What is left of an animation plays on without harm.
            for (int n = 0; n < 12 && e == image::Error::None && d.more_frames(); n++) {
                e = d.next_frame();
                while (!d.done() && e == image::Error::None) {
                    e = d.step(got, 7);
                }
            }
        };
        for (int cut = 0; cut < 24; cut++) {
            std::vector<uint8_t> b(good.begin(), good.begin() + static_cast<long>(good.size() * cut / 24));
            attempt(b);
        }
        for (int n = 0; n < 40; n++) {
            std::vector<uint8_t> b = good;
            const int changes = 1 + static_cast<int>(random() % 8);
            for (int k = 0; k < changes && !b.empty(); k++) {
                b[random() % b.size()] = static_cast<uint8_t>(random());
            }
            attempt(b);
        }
    }
    CHECK(runs > 1500);
    CHECK(errors > runs / 4); // most damage is noticed; the rest decodes to something
    printf("damaged pictures: %d decodes, %d gave an error\n", runs, errors);
}

void test_too_wide()
{
    // A PNG header claiming 100000 pixels across.
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                0, 1, 0x86, 0xA0, 0, 0, 0, 10, 8, 2, 0, 0, 0, 0, 0, 0, 0};
    std::ofstream(folder + "/wide.png", std::ios::binary)
        .write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    image::Decoder d;
    image::Info info;
    CHECK(d.open(storage, "D:\\wide.png", memory, info) == image::Error::TooLarge);
    CHECK(d.open(storage, "D:\\missing.png", memory, info) == image::Error::NotFound);
}

// Shrinking averages; each dither keeps a flat colour's average; None is
// the nearest colour.
void test_converter()
{
    static image::Converter conv;
    CHECK(conv.ready(memory));
    static uint8_t out[64 * 64];
    // 4x2 source, black and white columns, shrunk to 2x1: grey 128.
    const uint8_t stripes[2][12] = {{0, 0, 0, 255, 255, 255, 0, 0, 0, 255, 255, 255},
                                    {0, 0, 0, 255, 255, 255, 0, 0, 0, 255, 255, 255}};
    conv.begin(4, 2, 2, 1, out, image::Dither::None);
    conv.row(0, stripes[0]);
    conv.row(1, stripes[1]);
    CHECK(conv.rows_done() == 1);
    CHECK(out[0] == gfx::nearest({128, 128, 128}) && out[1] == out[0]);
    // A flat colour between board colours, 64x64, each way.
    const gfx::Rgb flat = {150, 100, 60};
    uint8_t row[64 * 3];
    for (int x = 0; x < 64; x++) {
        row[x * 3] = flat.r;
        row[x * 3 + 1] = flat.g;
        row[x * 3 + 2] = flat.b;
    }
    for (image::Dither d : {image::Dither::Bayer4, image::Dither::FloydSteinberg, image::Dither::None}) {
        conv.begin(64, 64, 64, 64, out, d);
        for (int y = 0; y < 64; y++) {
            conv.row(y, row);
        }
        CHECK(conv.rows_done() == 64);
        long sum[3] = {};
        for (uint8_t c : out) {
            const gfx::Rgb s = gfx::shown(c);
            sum[0] += s.r;
            sum[1] += s.g;
            sum[2] += s.b;
        }
        const int avg[3] = {int(sum[0] / 4096), int(sum[1] / 4096), int(sum[2] / 4096)};
        if (d == image::Dither::None) {
            CHECK(out[0] == gfx::nearest(flat) && out[4095] == out[0]);
        } else {
            CHECK(std::abs(avg[0] - flat.r) <= 12 && std::abs(avg[1] - flat.g) <= 12 &&
                  std::abs(avg[2] - flat.b) <= 12);
        }
    }
}


// A part of a picture converted alone, over what was there, gives what
// converting the whole changed picture gives: same size and shrunk, with
// each dither that allows it; the others refuse.
void test_converter_part()
{
    static image::Converter conv;
    CHECK(conv.ready(memory));
    const int w = 61, h = 47;
    std::vector<uint8_t> a(w * h * 3), b;
    std::mt19937 random(5);
    for (uint8_t &v : a) {
        v = static_cast<uint8_t>(random());
    }
    b = a;
    const int px = 17, py = 9, pw = 23, ph = 14;
    for (int y = py; y < py + ph; y++) {
        for (int x = px; x < px + pw; x++) {
            b[(y * w + x) * 3 + 1] ^= 0xFF;
        }
    }
    const int sizes[3][2] = {{w, h}, {40, 31}, {13, 5}};
    for (const auto &size : sizes) {
        for (int i = 0; i < image::kDitherCount; i++) {
            const auto d = static_cast<image::Dither>(i);
            std::vector<uint8_t> whole(size[0] * size[1]), part(size[0] * size[1]);
            conv.begin(w, h, size[0], size[1], part.data(), d);
            for (int y = 0; y < h; y++) {
                conv.row(y, &a[y * w * 3]);
            }
            conv.begin(w, h, size[0], size[1], whole.data(), d);
            for (int y = 0; y < h; y++) {
                conv.row(y, &b[y * w * 3]);
            }
            int first = 0, rows = 0, made[4];
            const bool can = conv.begin_part(w, h, size[0], size[1], part.data(), d, px, py, pw, ph, first, rows, made);
            CHECK(can == (image::steady(d) == d));
            if (!can) {
                continue;
            }
            CHECK(first <= py && first + rows >= py + ph && rows <= h);
            for (int y = first; y < first + rows; y++) {
                conv.row(y, &b[y * w * 3]);
            }
            CHECK(part == whole);
        }
    }
}

// Each dither: the same as tools/dither.py on a small picture, the same
// twice, board colours stay flat, flat colours keep their average.
void test_dithers()
{
    static const char *const kNames[image::kDitherCount] = {
        "none", "bayer2", "bayer4", "bayer8", "blue-noise", "floyd-steinberg", "floyd-serpentine",
        "atkinson", "jarvis-judice-ninke", "stucki", "sierra-lite"};
    static image::Converter conv;
    CHECK(conv.ready(memory));
    const int w = 70, h = 40;
    const std::vector<uint8_t> in = read_all(folder + "/dither-in.rgb");
    CHECK(in.size() == static_cast<size_t>(w * h * 3));
    std::vector<uint8_t> out(w * h), again(w * h);
    for (int i = 0; i < image::kDitherCount && in.size() == static_cast<size_t>(w * h * 3); i++) {
        const auto d = static_cast<image::Dither>(i);
        for (std::vector<uint8_t> *o : {&out, &again}) {
            conv.begin(w, h, w, h, o->data(), d);
            for (int y = 0; y < h; y++) {
                conv.row(y, &in[y * w * 3]);
            }
        }
        const std::vector<uint8_t> want = read_all(folder + "/dither-" + kNames[i] + ".idx");
        if (out != want || out != again) {
            printf("dither %s differs from tools/dither.py (or from itself)\n", kNames[i]);
            failures++;
        }
        for (int x = 0; x < w; x++) {
            CHECK(out[9 * w + x] == gfx::rgb(5, 2, 3)); // the flat board colour row
        }
        // A flat in-between colour: its average stays close.
        static uint8_t flat_out[64 * 64];
        uint8_t row[64 * 3];
        for (int x = 0; x < 64; x++) {
            row[x * 3] = 150;
            row[x * 3 + 1] = 100;
            row[x * 3 + 2] = 60;
        }
        conv.begin(64, 64, 64, 64, flat_out, d);
        for (int y = 0; y < 64; y++) {
            conv.row(y, row);
        }
        long sum[3] = {};
        for (uint8_t c : flat_out) {
            const gfx::Rgb s = gfx::shown(c);
            sum[0] += s.r;
            sum[1] += s.g;
            sum[2] += s.b;
        }
        const int avg[3] = {int(sum[0] / 4096), int(sum[1] / 4096), int(sum[2] / 4096)};
        // None and Atkinson (which drops a quarter of the error) are not exact.
        const int slack = d == image::Dither::None ? 40 : (d == image::Dither::Atkinson ? 20 : 8);
        if (std::abs(avg[0] - 150) > slack || std::abs(avg[1] - 100) > slack || std::abs(avg[2] - 60) > slack) {
            printf("dither %s: flat (150,100,60) averages (%d,%d,%d)\n", kNames[i], avg[0], avg[1], avg[2]);
            failures++;
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: test_image <folder made by tools/gen_pictures.py tests>\n");
        return 2;
    }
    folder = argv[1];
    static host::FolderVolume volume(folder);
    storage.mount('D', volume);
    test_converter();
    test_converter_part();
    test_dithers();
    test_manifest();
    test_too_wide();
    test_damaged();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
