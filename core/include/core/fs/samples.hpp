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
// The photographs that fill the screen, each at 800x600 and at 640x480
// (backlog item 85): about 930 KB, more than the drive of a test holds.
extern const SampleFile kBackgroundPictures[];
extern const int kBackgroundPictureCount;

} // namespace fs
