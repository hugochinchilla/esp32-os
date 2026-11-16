// The 64x64 blue-noise threshold tile (tools/gen_blue_noise.py).
#pragma once

#include <cstdint>

namespace image {

extern const uint16_t kBlueNoise[64 * 64]; // each of 0..4095 once

} // namespace image
