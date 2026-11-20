// Decoded rows to the screen's 256 colours, shrunk to the size wanted.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/image/image.hpp"
#include "core/ports.hpp"

namespace image {

// How colours between the board's levels are made. Every one works row by
// row with integers only, so the PC and the board give the same pixels.
enum class Dither : uint8_t {
    None,              // nearest colour
    Bayer2,            // ordered, 2x2 matrix
    Bayer4,            // ordered, 4x4 matrix (the desktop's pattern)
    Bayer8,            // ordered, 8x8 matrix
    BlueNoise,         // ordered, 64x64 blue-noise mask
    FloydSteinberg,    // error diffusion, left to right
    FloydSerpentine,   // Floyd-Steinberg, every other row right to left
    Atkinson,          // error diffusion, 3/4 of the error kept
    JarvisJudiceNinke, // error diffusion over three rows
    Stucki,            // error diffusion over three rows
    SierraLite,        // error diffusion, three neighbours
};
constexpr int kDitherCount = 11;

struct DitherInfo {
    const char *id;          // for settings files, e.g. "floyd-steinberg"
    const char *name;        // e.g. "Floyd-Steinberg"
    const char *menu;        // with its access letter, e.g. "&Floyd-Steinberg"
    const char *description; // one line
};
const DitherInfo &dither_info(Dither d);
// The dither for a picture that moves, when `d` is the one chosen. Error
// diffusion carries the errors of the part that changes through the parts
// that do not, which then shimmer; those become Bayer4. The ordered ones
// give a pixel the same colour whatever is around it, and stay.
Dither steady(Dither d);

// A RowSink that turns each row into board colours in `out`. Shrinking uses
// a box filter (the average of the source pixels each one covers); the same
// size is copied. It never enlarges.
class Converter : public RowSink {
public:
    static constexpr int kMaxWidth = 2048; // of the result

    // Its working memory (sums and error rows), once; false if none.
    bool ready(ports::Memory &memory);
    // Gives that memory back.
    void release(ports::Memory &memory);
    // dst_w x dst_h colours into `out`, from a src_w x src_h picture.
    void begin(int src_w, int src_h, int dst_w, int dst_h, uint8_t *out, Dither dither);
    void row(int y, const uint8_t *rgb) override;
    int rows_done() const { return dy_; } // finished rows of the result

private:
    static constexpr int kMargin = 2; // error columns beyond each edge
    void emit();
    void diffuse(const int *avg_row);

    uint32_t *sums_ = nullptr;   // per result column, R G B
    int16_t *errors_ = nullptr;  // three rows of (dst_w + 4) x 3
    uint16_t *col_start_ = nullptr;
    int src_w_ = 0, src_h_ = 0, dst_w_ = 0, dst_h_ = 0;
    uint8_t *out_ = nullptr;
    Dither dither_ = Dither::Bayer4;
    int dy_ = 0;        // result row being gathered
    int rows_in_ = 0;   // source rows added to it
    int *avg_ = nullptr; // the averaged row, R G B
};

} // namespace image
