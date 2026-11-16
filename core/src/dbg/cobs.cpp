#include "core/dbg/cobs.hpp"

namespace dbg {

size_t cobs_encode(const uint8_t *in, size_t size, uint8_t *out)
{
    size_t write = 1;    // next output position
    size_t code_at = 0;  // where the current block's length byte goes
    uint8_t code = 1;
    for (size_t i = 0; i < size; i++) {
        if (in[i] == 0) {
            out[code_at] = code;
            code_at = write++;
            code = 1;
        } else {
            out[write++] = in[i];
            code++;
            if (code == 0xFF) {
                out[code_at] = code;
                code_at = write++;
                code = 1;
            }
        }
    }
    out[code_at] = code;
    return write;
}

size_t cobs_decode(const uint8_t *in, size_t size, uint8_t *out)
{
    size_t read = 0;
    size_t write = 0;
    while (read < size) {
        uint8_t code = in[read++];
        if (code == 0 || read + (code - 1) > size) {
            return 0;
        }
        for (uint8_t i = 1; i < code; i++) {
            uint8_t byte = in[read++];
            if (byte == 0) {
                return 0;
            }
            out[write++] = byte;
        }
        if (code != 0xFF && read < size) {
            out[write++] = 0;
        }
    }
    return write;
}

} // namespace dbg
