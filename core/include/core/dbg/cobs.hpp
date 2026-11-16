// Consistent Overhead Byte Stuffing: removes zero bytes from a packet so that
// a zero can mark where packets start and end.
#pragma once

#include <cstddef>
#include <cstdint>

namespace dbg {

// Largest encoded size for an input of `size` bytes.
constexpr size_t cobs_max_encoded(size_t size) { return size + size / 254 + 1; }

// Returns the encoded size. `out` must hold cobs_max_encoded(size) bytes.
size_t cobs_encode(const uint8_t *in, size_t size, uint8_t *out);

// Returns the decoded size, or 0 if the input is malformed. `out` may be the
// same buffer as `in`.
size_t cobs_decode(const uint8_t *in, size_t size, uint8_t *out);

} // namespace dbg
