#include "core/fs/memory_volume.hpp"

#include <cstdio>
#include <cstring>

#include "core/fs/fs.hpp"

namespace fs {

using ports::DirEntry;
using ports::FsError;
using ports::OpenMode;

namespace {

char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool same_name(const char *a, const char *b, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (a[i] == 0 || lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return a[len] == 0;
}

bool separator(char c)
{
    return c == '\\' || c == '/';
}

} // namespace

MemoryVolume::MemoryVolume(void *pool, size_t bytes, const char *label)
{
    snprintf(label_, sizeof(label_), "%s", label);
    uint8_t *p = static_cast<uint8_t *>(pool);
    const size_t table = sizeof(Node) * kMaxNodes;
    nodes_ = reinterpret_cast<Node *>(p);
    // What is left is shared between the chain table and the blocks.
    const size_t rest = bytes > table ? bytes - table : 0;
    blocks_ = static_cast<int>(rest / (kBlock + sizeof(int32_t)));
    next_ = reinterpret_cast<int32_t *>(p + table);
    data_ = p + table + blocks_ * sizeof(int32_t);
    memset(nodes_, 0, table);
    for (int i = 0; i < blocks_; i++) {
        next_[i] = -2;
    }
    free_blocks_ = blocks_;
    nodes_[0].used = 1;
    nodes_[0].folder = 1;
    nodes_[0].parent = -1;
    nodes_[0].first = -1;
}

int MemoryVolume::child(int folder, const char *name, size_t len) const
{
    for (int i = 1; i < kMaxNodes; i++) {
        if (nodes_[i].used && nodes_[i].parent == folder && same_name(nodes_[i].name, name, len)) {
            return i;
        }
    }
    return -1;
}

int MemoryVolume::find(const char *path) const
{
    int at = 0;
    const char *p = path;
    while (*p != 0) {
        while (separator(*p)) {
            p++;
        }
        if (*p == 0) {
            break;
        }
        const char *end = p;
        while (*end != 0 && !separator(*end)) {
            end++;
        }
        if (!nodes_[at].folder) {
            return -1;
        }
        at = child(at, p, static_cast<size_t>(end - p));
        if (at < 0) {
            return -1;
        }
        p = end;
    }
    return at;
}

int MemoryVolume::find_parent(const char *path, const char *&leaf) const
{
    // Split at the last separator: the folder part must exist.
    char folder[kMaxPath];
    snprintf(folder, sizeof(folder), "%s", path);
    int n = static_cast<int>(strlen(folder));
    while (n > 0 && separator(folder[n - 1])) {
        folder[--n] = 0;
    }
    int cut = n;
    while (cut > 0 && !separator(folder[cut - 1])) {
        cut--;
    }
    leaf = path + cut;
    folder[cut] = 0;
    const int at = find(folder);
    return (at >= 0 && nodes_[at].folder) ? at : -1;
}

bool MemoryVolume::in_use(int node) const
{
    for (const Handle &h : handles_) {
        if (h.used && h.node == node) {
            return true;
        }
    }
    return false;
}

int MemoryVolume::new_node()
{
    for (int i = 1; i < kMaxNodes; i++) {
        if (!nodes_[i].used) {
            memset(&nodes_[i], 0, sizeof(Node));
            nodes_[i].first = -1;
            return i;
        }
    }
    return -1;
}

int32_t MemoryVolume::new_block()
{
    for (int i = 0; i < blocks_; i++) {
        if (next_[i] == -2) {
            next_[i] = -1;
            free_blocks_--;
            return i;
        }
    }
    return -1;
}

void MemoryVolume::free_chain(int32_t block)
{
    while (block >= 0) {
        const int32_t next = next_[block];
        next_[block] = -2;
        free_blocks_++;
        block = next;
    }
}

FsError MemoryVolume::info(ports::VolumeInfo &out)
{
    snprintf(out.label, sizeof(out.label), "%s", label_);
    out.total_bytes = static_cast<uint64_t>(blocks_) * kBlock;
    out.free_bytes = static_cast<uint64_t>(free_blocks_) * kBlock;
    out.keeps_files = false;
    return FsError::Ok;
}

FsError MemoryVolume::entry(const char *folder, int index, DirEntry &out)
{
    const int at = find(folder);
    if (at < 0) {
        return FsError::NotFound;
    }
    if (!nodes_[at].folder) {
        return FsError::NotFolder;
    }
    int seen = 0;
    for (int i = 1; i < kMaxNodes; i++) {
        if (nodes_[i].used && nodes_[i].parent == at && seen++ == index) {
            snprintf(out.name, sizeof(out.name), "%s", nodes_[i].name);
            out.folder = nodes_[i].folder;
            out.size = nodes_[i].size;
            out.modified = nodes_[i].modified;
            return FsError::Ok;
        }
    }
    return FsError::NotFound;
}

FsError MemoryVolume::stat(const char *path, DirEntry &out)
{
    const int at = find(path);
    if (at < 0) {
        return FsError::NotFound;
    }
    snprintf(out.name, sizeof(out.name), "%s", nodes_[at].name);
    out.folder = nodes_[at].folder;
    out.size = nodes_[at].size;
    out.modified = nodes_[at].modified;
    return FsError::Ok;
}

FsError MemoryVolume::make_folder(const char *path)
{
    const char *leaf;
    const int parent = find_parent(path, leaf);
    if (parent < 0) {
        return FsError::NotFound;
    }
    if (!path::valid_name(leaf)) {
        return FsError::BadName;
    }
    if (child(parent, leaf, strlen(leaf)) >= 0) {
        return FsError::Exists;
    }
    const int n = new_node();
    if (n < 0) {
        return FsError::NoSpace;
    }
    snprintf(nodes_[n].name, sizeof(nodes_[n].name), "%s", leaf);
    nodes_[n].parent = static_cast<int16_t>(parent);
    nodes_[n].folder = 1;
    nodes_[n].used = 1;
    nodes_[n].modified = now();
    return FsError::Ok;
}

FsError MemoryVolume::remove(const char *path)
{
    const int at = find(path);
    if (at < 0) {
        return FsError::NotFound;
    }
    if (at == 0) {
        return FsError::BadName;
    }
    if (in_use(at)) {
        return FsError::InUse;
    }
    if (nodes_[at].folder) {
        for (int i = 1; i < kMaxNodes; i++) {
            if (nodes_[i].used && nodes_[i].parent == at) {
                return FsError::NotEmpty;
            }
        }
    }
    free_chain(nodes_[at].first);
    nodes_[at].used = 0;
    return FsError::Ok;
}

FsError MemoryVolume::rename(const char *from, const char *to)
{
    const int at = find(from);
    if (at <= 0) {
        return at == 0 ? FsError::BadName : FsError::NotFound;
    }
    const char *leaf;
    const int parent = find_parent(to, leaf);
    if (parent < 0) {
        return FsError::NotFound;
    }
    if (!path::valid_name(leaf)) {
        return FsError::BadName;
    }
    const int existing = child(parent, leaf, strlen(leaf));
    if (existing >= 0 && existing != at) {
        return FsError::Exists;
    }
    // A folder cannot go inside itself.
    for (int p = parent; p >= 0; p = nodes_[p].parent) {
        if (p == at) {
            return FsError::BadName;
        }
    }
    snprintf(nodes_[at].name, sizeof(nodes_[at].name), "%s", leaf);
    nodes_[at].parent = static_cast<int16_t>(parent);
    return FsError::Ok;
}

FsError MemoryVolume::replace(const char *from, const char *to)
{
    const int at = find(from);
    if (at < 0) {
        return FsError::NotFound;
    }
    const int old = find(to);
    if (at == 0 || nodes_[at].folder || (old >= 0 && nodes_[old].folder)) {
        return FsError::IsFolder;
    }
    if (old == at) {
        return FsError::Ok;
    }
    if (in_use(at) || (old >= 0 && in_use(old))) {
        return FsError::InUse;
    }
    if (old < 0) {
        return rename(from, to);
    }
    // The old file's entry takes the new contents, so it keeps its name as
    // written and its place in the folder.
    free_chain(nodes_[old].first);
    nodes_[old].first = nodes_[at].first;
    nodes_[old].size = nodes_[at].size;
    nodes_[old].modified = nodes_[at].modified;
    nodes_[at].used = 0;
    return FsError::Ok;
}

FsError MemoryVolume::open(const char *path, OpenMode mode, int &file)
{
    int slot = -1;
    for (int i = 0; i < kMaxOpen; i++) {
        if (!handles_[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return FsError::TooManyOpen;
    }
    int at = find(path);
    if (at >= 0 && nodes_[at].folder) {
        return FsError::IsFolder;
    }
    if (at < 0) {
        if (mode != OpenMode::Write) {
            return FsError::NotFound;
        }
        const char *leaf;
        const int parent = find_parent(path, leaf);
        if (parent < 0) {
            return FsError::NotFound;
        }
        if (!path::valid_name(leaf)) {
            return FsError::BadName;
        }
        at = new_node();
        if (at < 0) {
            return FsError::NoSpace;
        }
        snprintf(nodes_[at].name, sizeof(nodes_[at].name), "%s", leaf);
        nodes_[at].parent = static_cast<int16_t>(parent);
        nodes_[at].used = 1;
        nodes_[at].modified = now();
    } else if (mode != OpenMode::Read && in_use(at)) {
        return FsError::InUse;
    } else if (mode == OpenMode::Write) {
        free_chain(nodes_[at].first);
        nodes_[at].first = -1;
        nodes_[at].size = 0;
        nodes_[at].modified = now();
    }
    handles_[slot] = {true, mode != OpenMode::Read, false, at, 0, nodes_[at].first, 0};
    file = slot;
    return FsError::Ok;
}

// Points h.block at the block holding h.pos, adding blocks when growing.
bool MemoryVolume::locate(Handle &h, bool grow)
{
    Node &n = nodes_[h.node];
    if (h.block < 0 || h.pos < h.block_base) {
        h.block = n.first;
        h.block_base = 0;
        if (h.block < 0) {
            if (!grow) {
                return false;
            }
            h.block = new_block();
            if (h.block < 0) {
                return false;
            }
            n.first = h.block;
        }
    }
    while (h.pos >= h.block_base + kBlock) {
        int32_t next = next_[h.block];
        if (next < 0) {
            if (!grow) {
                return false;
            }
            next = new_block();
            if (next < 0) {
                return false;
            }
            next_[h.block] = next;
        }
        h.block = next;
        h.block_base += kBlock;
    }
    return true;
}

FsError MemoryVolume::read(int file, void *data, size_t size, size_t &done)
{
    done = 0;
    if (file < 0 || file >= kMaxOpen || !handles_[file].used) {
        return FsError::NotFound;
    }
    Handle &h = handles_[file];
    const uint32_t length = nodes_[h.node].size;
    uint8_t *out = static_cast<uint8_t *>(data);
    while (done < size && h.pos < length && locate(h, false)) {
        const uint32_t in_block = h.pos - h.block_base;
        size_t n = kBlock - in_block;
        n = n < size - done ? n : size - done;
        n = n < length - h.pos ? n : length - h.pos;
        memcpy(out + done, data_ + static_cast<size_t>(h.block) * kBlock + in_block, n);
        done += n;
        h.pos += static_cast<uint32_t>(n);
    }
    return FsError::Ok;
}

FsError MemoryVolume::write(int file, const void *data, size_t size)
{
    if (file < 0 || file >= kMaxOpen || !handles_[file].used) {
        return FsError::NotFound;
    }
    Handle &h = handles_[file];
    if (!h.writable) {
        return FsError::InUse;
    }
    Node &n = nodes_[h.node];
    h.wrote = true;
    const uint8_t *in = static_cast<const uint8_t *>(data);
    size_t done = 0;
    while (done < size) {
        if (!locate(h, true)) {
            return FsError::NoSpace;
        }
        const uint32_t in_block = h.pos - h.block_base;
        size_t count = kBlock - in_block;
        count = count < size - done ? count : size - done;
        memcpy(data_ + static_cast<size_t>(h.block) * kBlock + in_block, in + done, count);
        done += count;
        h.pos += static_cast<uint32_t>(count);
        if (h.pos > n.size) {
            n.size = h.pos;
        }
    }
    return FsError::Ok;
}

FsError MemoryVolume::seek(int file, uint32_t offset)
{
    if (file < 0 || file >= kMaxOpen || !handles_[file].used) {
        return FsError::NotFound;
    }
    Handle &h = handles_[file];
    if (offset > nodes_[h.node].size) {
        return FsError::Io; // no holes in files
    }
    h.pos = offset;
    return FsError::Ok;
}

FsError MemoryVolume::close(int file)
{
    if (file < 0 || file >= kMaxOpen || !handles_[file].used) {
        return FsError::NotFound;
    }
    if (handles_[file].wrote) {
        nodes_[handles_[file].node].modified = now();
    }
    handles_[file].used = false;
    return FsError::Ok;
}

uint32_t MemoryVolume::now() const
{
    int64_t seconds = 0;
    return clock_ != nullptr && clock_->utc_time(seconds) && seconds > 0 ? static_cast<uint32_t>(seconds) : 0;
}

} // namespace fs
