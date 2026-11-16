// The Defragmenter's pretend disk. Flash storage gains nothing from being
// defragmented, and rewriting it would only wear it out, so this program is
// a show: a made-up block map that tidies itself. Nothing here reads or
// writes a drive.
#pragma once

#include <cstdint>

namespace shell {

class DefragModel {
public:
    static constexpr int kColumns = 48, kRows = 18;
    static constexpr int kCells = kColumns * kRows;
    static constexpr int kMaxFiles = 96;
    static constexpr int kMaxRun = 6;       // blocks moved in one go
    static constexpr int kMaxChanged = 2 * kMaxRun;

    enum class Mark : uint8_t { None, Reading, Writing };

    DefragModel() { scramble(1); }

    // A new made-up layout: files in pieces all over the map. The same seed
    // gives the same map.
    void scramble(uint32_t seed);
    // One step of the show: pick a move, show it being read, show it being
    // written, settle. Returns false once everything is in order.
    bool step();
    bool done() const { return placed_ == used_; }

    // What a block holds: 0 free, otherwise the number of its file.
    int file(int cell) const { return owner_[cell]; }
    Mark mark(int cell) const { return mark_[cell]; }
    int file_count() const { return files_; }
    int used() const { return used_; }
    // Blocks already in their final place, and that as a percentage.
    int placed() const { return placed_; }
    int percent() const { return used_ == 0 ? 100 : placed_ * 100 / used_; }
    // The file being moved (0: none).
    int current_file() const { return moving_file_; }
    uint32_t moves() const { return moves_; }
    // The blocks the last step() changed, for painting.
    int changed_count() const { return changed_count_; }
    int changed(int i) const { return changed_[i]; }
    // True when every file's blocks follow each other in order from block 0.
    bool tidy() const;

private:
    enum class Phase : uint8_t { Pick, Read, Write, Settle };

    uint32_t random();
    void touch(int cell);
    int find(int file, int piece) const;

    uint8_t owner_[kCells];
    uint16_t piece_[kCells]; // which block of its file this is
    Mark mark_[kCells];
    uint16_t start_[kMaxFiles + 2]; // where each file belongs (index: file number)
    int files_ = 0;
    int used_ = 0;
    int placed_ = 0;
    Phase phase_ = Phase::Pick;
    int from_ = 0, to_ = 0, run_ = 0;
    int moving_file_ = 0;
    uint32_t moves_ = 0;
    uint32_t seed_ = 1;
    int changed_[kMaxChanged];
    int changed_count_ = 0;
};

// The Defragmenter window (opens it, or brings it to the front) and its
// disk, for tests.
void open_defragmenter();
const DefragModel &defragmenter_disk();
constexpr uint32_t kDefragStepUs = 30000;

} // namespace shell
