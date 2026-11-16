// A volume kept in RAM: the RAM disk. Everything lives in one block of
// memory given at construction (external RAM on the board): a table of
// files and folders, a chain table, and 512-byte data blocks, like a small
// FAT. Nothing is allocated afterwards. The contents are lost at restart.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ports.hpp"

namespace fs {

class MemoryVolume : public ports::Volume {
public:
    static constexpr int kBlock = 512;
    static constexpr int kMaxNodes = 256; // files and folders together
    static constexpr int kMaxOpen = 6;

    MemoryVolume(void *pool, size_t bytes, const char *label);

    ports::FsError info(ports::VolumeInfo &out) override;
    ports::FsError entry(const char *folder, int index, ports::DirEntry &out) override;
    ports::FsError stat(const char *path, ports::DirEntry &out) override;
    ports::FsError make_folder(const char *path) override;
    ports::FsError remove(const char *path) override;
    ports::FsError rename(const char *from, const char *to) override;
    ports::FsError replace(const char *from, const char *to) override;
    ports::FsError open(const char *path, ports::OpenMode mode, int &file) override;
    ports::FsError read(int file, void *data, size_t size, size_t &done) override;
    ports::FsError write(int file, const void *data, size_t size) override;
    ports::FsError seek(int file, uint32_t offset) override;
    ports::FsError close(int file) override;

private:
    struct Node {
        char name[64];
        int16_t parent; // -1 for the root
        uint8_t used;
        uint8_t folder;
        uint32_t size;
        int32_t first; // first data block, -1 none
    };
    struct Handle {
        bool used;
        bool writable;
        int node;
        uint32_t pos;
        int32_t block;       // the block holding pos (cached), -1 unknown
        uint32_t block_base; // file offset where that block starts
    };

    // The node for a path, or -1. With `parent`, also finds the folder the
    // last name would go in and gives that name.
    int find(const char *path) const;
    int find_parent(const char *path, const char *&leaf) const;
    int child(int folder, const char *name, size_t len) const;
    bool in_use(int node) const;
    int new_node();
    int32_t new_block();
    void free_chain(int32_t block);
    bool locate(Handle &h, bool grow);

    Node *nodes_;
    int32_t *next_; // per block: the next block, -1 end, -2 free
    uint8_t *data_;
    int blocks_ = 0;
    int free_blocks_ = 0;
    char label_[24];
    Handle handles_[kMaxOpen] = {};
};

} // namespace fs
