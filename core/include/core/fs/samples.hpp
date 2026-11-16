// Sample pictures for the RAM disk, made by tools/gen_pictures.py embed.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fs {

struct SampleFile {
    const char *name;
    const uint8_t *data;
    size_t size;
};

extern const SampleFile kSamplePictures[];
extern const int kSamplePictureCount;

} // namespace fs
