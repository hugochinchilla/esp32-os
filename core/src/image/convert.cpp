#include "core/image/convert.hpp"

#include <cstring>

#include "blue_noise.hpp"
#include "core/gfx/canvas.hpp"

namespace image {

namespace {

const DitherInfo kInfo[kDitherCount] = {
    {"none", "None", "&None", "The nearest colour: flat areas, bands in gradients."},
    {"bayer2", "Bayer 2x2", "Bayer &2x2", "A coarse regular pattern of four steps."},
    {"bayer4", "Bayer 4x4", "Bayer &4x4", "The desktop's regular pattern of 16 steps."},
    {"bayer8", "Bayer 8x8", "Bayer &8x8", "A finer regular pattern of 64 steps."},
    {"blue-noise", "Blue noise", "&Blue noise", "An even, random-looking pattern with no grid."},
    {"floyd-steinberg", "Floyd-Steinberg", "&Floyd-Steinberg", "Error diffusion: fine detail, some worms."},
    {"floyd-serpentine", "Floyd-Steinberg serpentine", "Floyd-Steinberg &serpentine",
     "Floyd-Steinberg, alternate rows reversed: fewer worms."},
    {"atkinson", "Atkinson", "&Atkinson", "Error diffusion with more contrast, as on the Mac."},
    {"jarvis-judice-ninke", "Jarvis-Judice-Ninke", "&Jarvis-Judice-Ninke", "Error spread over 12 neighbours: smooth, slower."},
    {"stucki", "Stucki", "S&tucki", "Like Jarvis-Judice-Ninke, a little sharper."},
    {"sierra-lite", "Sierra Lite", "Sierra &Lite", "Error spread over 3 neighbours: quick."},
};

// Error diffusion kernels: (dx, dy, weight) taps and the divisor.
struct Tap {
    int8_t dx, dy, w;
};
struct Kernel {
    const Tap *taps;
    int count;
    int divisor;
    bool serpentine;
};
const Tap kFloyd[] = {{1, 0, 7}, {-1, 1, 3}, {0, 1, 5}, {1, 1, 1}};
const Tap kAtkinson[] = {{1, 0, 1}, {2, 0, 1}, {-1, 1, 1}, {0, 1, 1}, {1, 1, 1}, {0, 2, 1}};
const Tap kJarvis[] = {{1, 0, 7},  {2, 0, 5},  {-2, 1, 3}, {-1, 1, 5}, {0, 1, 7}, {1, 1, 5},
                       {2, 1, 3},  {-2, 2, 1}, {-1, 2, 3}, {0, 2, 5},  {1, 2, 3}, {2, 2, 1}};
const Tap kStucki[] = {{1, 0, 8},  {2, 0, 4},  {-2, 1, 2}, {-1, 1, 4}, {0, 1, 8}, {1, 1, 4},
                       {2, 1, 2},  {-2, 2, 1}, {-1, 2, 2}, {0, 2, 4},  {1, 2, 2}, {2, 2, 1}};
const Tap kSierraLite[] = {{1, 0, 2}, {-1, 1, 1}, {0, 1, 1}};

const Kernel *kernel(Dither d)
{
    static const Kernel kFs = {kFloyd, 4, 16, false};
    static const Kernel kFsSerp = {kFloyd, 4, 16, true};
    static const Kernel kAt = {kAtkinson, 6, 8, false};
    static const Kernel kJjn = {kJarvis, 12, 48, false};
    static const Kernel kSt = {kStucki, 12, 42, false};
    static const Kernel kSl = {kSierraLite, 3, 4, false};
    switch (d) {
    case Dither::FloydSteinberg: return &kFs;
    case Dither::FloydSerpentine: return &kFsSerp;
    case Dither::Atkinson: return &kAt;
    case Dither::JarvisJudiceNinke: return &kJjn;
    case Dither::Stucki: return &kSt;
    case Dither::SierraLite: return &kSl;
    default: return nullptr;
    }
}

// Bayer matrix of size n (a power of two) at (x, y), from the 2x2 one.
constexpr unsigned bayer(unsigned n, unsigned x, unsigned y)
{
    constexpr uint8_t k2[2][2] = {{0, 2}, {3, 1}};
    return n == 1 ? 0 : 4 * bayer(n / 2, x % (n / 2), y % (n / 2)) + k2[y / (n / 2)][x / (n / 2)];
}

struct Bayer8 {
    uint8_t m[8][8];
    constexpr Bayer8() : m()
    {
        for (unsigned y = 0; y < 8; y++) {
            for (unsigned x = 0; x < 8; x++) {
                m[y][x] = static_cast<uint8_t>(bayer(8, x, y));
            }
        }
    }
};
constexpr Bayer8 kBayer8;

} // namespace

const DitherInfo &dither_info(Dither d)
{
    const int i = static_cast<int>(d);
    return kInfo[i >= 0 && i < kDitherCount ? i : 0];
}

Converter::Converter() = default;

bool Converter::ready(ports::Memory &memory)
{
    if (sums_ == nullptr) {
        sums_ = static_cast<uint32_t *>(memory.allocate(sizeof(uint32_t) * kMaxWidth * 3));
        errors_ = static_cast<int16_t *>(memory.allocate(sizeof(int16_t) * (kMaxWidth + 2 * kMargin) * 3 * 3));
        col_start_ = static_cast<uint16_t *>(memory.allocate(sizeof(uint16_t) * (kMaxWidth + 1)));
        avg_ = static_cast<int *>(memory.allocate(sizeof(int) * kMaxWidth * 3));
    }
    return sums_ != nullptr && errors_ != nullptr && col_start_ != nullptr && avg_ != nullptr;
}

void Converter::release(ports::Memory &memory)
{
    memory.release(sums_);
    memory.release(errors_);
    memory.release(col_start_);
    memory.release(avg_);
    sums_ = nullptr;
    errors_ = nullptr;
    col_start_ = nullptr;
    avg_ = nullptr;
}

void Converter::begin(int src_w, int src_h, int dst_w, int dst_h, uint8_t *out, Dither dither)
{
    src_w_ = src_w;
    src_h_ = src_h;
    dst_w_ = dst_w < 1 ? 1 : (dst_w > src_w ? src_w : (dst_w > kMaxWidth ? kMaxWidth : dst_w));
    dst_h_ = dst_h < 1 ? 1 : (dst_h > src_h ? src_h : dst_h);
    out_ = out;
    dither_ = dither;
    dy_ = 0;
    rows_in_ = 0;
    for (int x = 0; x <= dst_w_; x++) {
        col_start_[x] = static_cast<uint16_t>(static_cast<int64_t>(x) * src_w_ / dst_w_);
    }
    memset(sums_, 0, sizeof(uint32_t) * dst_w_ * 3);
    memset(errors_, 0, sizeof(int16_t) * (dst_w_ + 2 * kMargin) * 3 * 3);
}

void Converter::row(int y, const uint8_t *rgb)
{
    if (dy_ >= dst_h_) {
        return;
    }
    for (int x = 0; x < dst_w_; x++) {
        uint32_t r = 0, g = 0, b = 0;
        for (int sx = col_start_[x]; sx < col_start_[x + 1]; sx++) {
            r += rgb[sx * 3];
            g += rgb[sx * 3 + 1];
            b += rgb[sx * 3 + 2];
        }
        sums_[x * 3] += r;
        sums_[x * 3 + 1] += g;
        sums_[x * 3 + 2] += b;
    }
    rows_in_++;
    // The last source row of this result row?
    const int next_start = static_cast<int>(static_cast<int64_t>(dy_ + 1) * src_h_ / dst_h_);
    if (y + 1 >= next_start) {
        emit();
    }
}

void Converter::emit()
{
    uint8_t *o = out_ + static_cast<size_t>(dy_) * dst_w_;
    for (int x = 0; x < dst_w_; x++) {
        const uint32_t area = static_cast<uint32_t>(col_start_[x + 1] - col_start_[x]) * rows_in_;
        for (int c = 0; c < 3; c++) {
            avg_[x * 3 + c] = static_cast<int>((sums_[x * 3 + c] + area / 2) / area);
        }
    }
    const unsigned y = static_cast<unsigned>(dy_);
    switch (dither_) {
    case Dither::None:
    case Dither::Bayer2:
    case Dither::Bayer4:
    case Dither::Bayer8:
    case Dither::BlueNoise:
        for (int x = 0; x < dst_w_; x++) {
            const gfx::Rgb c = {static_cast<uint8_t>(avg_[x * 3]), static_cast<uint8_t>(avg_[x * 3 + 1]),
                                static_cast<uint8_t>(avg_[x * 3 + 2])};
            const unsigned ux = static_cast<unsigned>(x);
            switch (dither_) {
            case Dither::None: o[x] = gfx::nearest(c); break;
            case Dither::Bayer2: o[x] = gfx::threshold(c, bayer(2, ux & 1, y & 1), 4); break;
            case Dither::Bayer4: o[x] = gfx::dither(c, x, dy_); break;
            case Dither::Bayer8: o[x] = gfx::threshold(c, kBayer8.m[y & 7][ux & 7], 64); break;
            default: o[x] = gfx::threshold(c, kBlueNoise[(y & 63) * 64 + (ux & 63)], 4096); break;
            }
        }
        break;
    default:
        diffuse(avg_);
        break;
    }
    memset(sums_, 0, sizeof(uint32_t) * dst_w_ * 3);
    rows_in_ = 0;
    dy_++;
}

// Error diffusion of one row. Errors are in the same gamma-encoded 0..255
// values as the picture, against the level the board really shows (blue:
// 0, 73, 182, 255), and each tap's share is rounded towards zero.
void Converter::diffuse(const int *avg)
{
    const Kernel &k = *kernel(dither_);
    const int stride = (dst_w_ + 2 * kMargin) * 3;
    int16_t *rows[3];
    for (int i = 0; i < 3; i++) {
        rows[i] = errors_ + ((dy_ + i) % 3) * stride + kMargin * 3;
    }
    uint8_t *o = out_ + static_cast<size_t>(dy_) * dst_w_;
    const bool back = k.serpentine && (dy_ & 1);
    const int s = dither_ == Dither::FloydSteinberg ? 0 : 4;
    const int half = s == 0 ? 0 : 1 << (s - 1);
    for (int i = 0; i < dst_w_; i++) {
        const int x = back ? dst_w_ - 1 - i : i;
        // Errors are kept in 16ths (s = 4), except by the original
        // Floyd-Steinberg, kept as it was first written (whole values, s = 0).
        int v[3], t[3];
        for (int c = 0; c < 3; c++) {
            t[c] = (avg[x * 3 + c] << s) + rows[0][x * 3 + c];
            t[c] = t[c] < 0 ? 0 : (t[c] > (255 << s) ? (255 << s) : t[c]);
            v[c] = (t[c] + half) >> s;
        }
        gfx::Rgb got;
        o[x] = gfx::nearest({static_cast<uint8_t>(v[0]), static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2])},
                            &got);
        const int e[3] = {t[0] - (got.r << s), t[1] - (got.g << s), t[2] - (got.b << s)};
        for (int t = 0; t < k.count; t++) {
            const Tap &tap = k.taps[t];
            int16_t *dst = rows[tap.dy] + (x + (back ? -tap.dx : tap.dx)) * 3;
            for (int c = 0; c < 3; c++) {
                dst[c] = static_cast<int16_t>(dst[c] + e[c] * tap.w / k.divisor);
            }
        }
    }
    memset(rows[0] - kMargin * 3, 0, sizeof(int16_t) * stride); // becomes the row two below the next
}

} // namespace image
