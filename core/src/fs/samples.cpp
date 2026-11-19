// What a new drive gets where nothing copies files to it afterwards: the PC
// programs, the browser version and the tests. On the board the pictures
// are not in the firmware: tools/flash.py copies them to C: (backlog item
// 77). This is a file of its own so that the firmware, which does not call
// add_samples(), does not carry the pictures either.
#include "core/fs/fs.hpp"
#include "core/fs/samples.hpp"

#include <cstdio>

namespace fs {

void add_samples(Storage &storage, char drive)
{
    add_first_files(storage, drive);
    // Pictures made by tools/gen_pictures.py, and two NASA photographs
    // (public domain, see third_party/samples/README.md).
    const char root[4] = {drive, ':', '\\', 0};
    for (int i = 0; i < kSamplePictureCount; i++) {
        char rel[96];
        char p[kMaxPath];
        snprintf(rel, sizeof(rel), "pictures\\%s", kSamplePictures[i].name);
        path::join(root, rel, p, sizeof(p));
        storage.write_file(p, kSamplePictures[i].data, kSamplePictures[i].size);
    }
}

} // namespace fs
