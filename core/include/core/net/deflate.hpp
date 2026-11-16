// A small zlib compressor for the remote screen: one continuous stream, fed
// in pieces, each piece flushed so the other side can decode it at once
// (what zlib calls a sync flush). It finds repeats up to 32 KB back with a
// hash of three bytes and writes them with the fixed codes, which is simple,
// quick, and good on screens (flat colours, repeating dither).
#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

class Deflater {
public:
    static constexpr size_t kWindow = 32768;
    static constexpr size_t kHashSize = 8192;
    // Working memory, given by the caller: the last 64 KB of data and two
    // tables of positions.
    static constexpr size_t kMemory = 2 * kWindow + (kHashSize + kWindow) * sizeof(uint16_t);
    static constexpr size_t kMaxPiece = kWindow;
    // The most a piece of `size` bytes can take once compressed.
    static constexpr size_t bound(size_t size) { return size + size / 8 + 16; }

    void init(void *memory);
    // Starts a new stream (a new connection).
    void reset();
    // Compresses one piece (at most kMaxPiece bytes) into `out`, which must
    // hold bound(size) bytes. Returns the bytes written.
    size_t compress(const uint8_t *in, size_t size, uint8_t *out);

private:
    void put_bits(uint32_t value, int count);
    void put_code(uint32_t code, int length); // a Huffman code, top bit first
    void literal(uint8_t value);
    void match(int length, int distance);

    uint8_t *window_ = nullptr;
    uint16_t *head_ = nullptr;
    uint16_t *prev_ = nullptr;
    size_t pos_ = 0;      // where the next byte goes in the window
    bool started_ = false;
    uint8_t *out_ = nullptr;
    size_t out_size_ = 0;
    uint32_t bits_ = 0;
    int bit_count_ = 0;
};

} // namespace net
