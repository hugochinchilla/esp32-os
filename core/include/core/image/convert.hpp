// Decoded rows to the screen's 256 colours, shrunk to the size wanted.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/image/image.hpp"
#include "core/ports.hpp"

namespace image {

enum class Dither : uint8_t {
    Ordered,   // the 4x4 pattern used everywhere on the desktop (default)
    Diffusion, // Floyd-Steinberg error diffusion: finer, but it crawls when redrawn
    None,      // nearest colour: banding in gradients
};

// A RowSink that turns each row into board colours in `out`. Shrinking uses
// a box filter (the average of the source pixels each one covers); the same
// size is copied. It never enlarges.
class Converter : public RowSink {
public:
    static constexpr int kMaxWidth = 2048; // of the result

    // Its working memory (sums and error rows), once; false if none.
    bool ready(ports::Memory &memory);
    // dst_w x dst_h colours into `out`, from a src_w x src_h picture.
    void begin(int src_w, int src_h, int dst_w, int dst_h, uint8_t *out, Dither dither);
    void row(int y, const uint8_t *rgb) override;
    int rows_done() const { return dy_; } // finished rows of the result

private:
    void emit();

    uint32_t *sums_ = nullptr;   // per result column, R G B
    int16_t *errors_ = nullptr;  // two rows of (dst_w + 2) x 3, for diffusion
    uint16_t *col_start_ = nullptr;
    int src_w_ = 0, src_h_ = 0, dst_w_ = 0, dst_h_ = 0;
    uint8_t *out_ = nullptr;
    Dither dither_ = Dither::Ordered;
    int dy_ = 0;        // result row being gathered
    int rows_in_ = 0;   // source rows added to it
};

} // namespace image
