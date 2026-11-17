// Updates over the network: the conversation with the sender, an image that
// arrives in pieces of every size, and everything that can go wrong leaving
// the running system as it was.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/dbg/crc.hpp"
#include "core/net/update.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

// The connection: what the sender sent and has not been read, what the
// board answered, and how much each side takes at a time.
class Pipe : public net::Stream, public net::Listener {
public:
    std::string to_board, from_board;
    bool waiting = false, open = false, broken = false;
    size_t read_at_most = 1 << 20, write_at_most = 1 << 20;
    int closes = 0;

    net::Stream *accept() override
    {
        if (!waiting) {
            return nullptr;
        }
        waiting = false;
        open = true;
        broken = false;
        from_board.clear();
        return this;
    }
    int read(uint8_t *out, size_t capacity) override
    {
        if (to_board.empty()) {
            return broken ? -1 : 0;
        }
        const size_t n = std::min(std::min(capacity, to_board.size()), read_at_most);
        memcpy(out, to_board.data(), n);
        to_board.erase(0, n);
        return static_cast<int>(n);
    }
    int write(const uint8_t *data, size_t size) override
    {
        if (broken) {
            return -1;
        }
        const size_t n = std::min(size, write_at_most);
        from_board.append(reinterpret_cast<const char *>(data), n);
        return static_cast<int>(n);
    }
    void close() override
    {
        open = false;
        closes++;
    }
};

std::string logged;
void keep(const char *line, void *)
{
    logged += line;
    logged += "\n";
}

struct Told {
    int times = 0;
    bool restart = false;
};
void written(bool restart, void *context)
{
    auto *t = static_cast<Told *>(context);
    t->times++;
    t->restart = restart;
}

std::string image_of(size_t size, uint8_t first = 0xE9)
{
    std::string image(size, 0);
    for (size_t i = 0; i < size; i++) {
        image[i] = static_cast<char>((i * 7 + i / 251) & 0xFF);
    }
    image[0] = static_cast<char>(first);
    return image;
}

std::string header(const std::string &image, const char *extra = "")
{
    char line[80];
    snprintf(line, sizeof(line), "UPDATE %u %08x%s\n", static_cast<unsigned>(image.size()),
             static_cast<unsigned>(dbg::crc32(reinterpret_cast<const uint8_t *>(image.data()), image.size())), extra);
    return line;
}

struct Rig {
    host::MemoryUpdater updater;
    host::FixedClock clock;
    Pipe pipe;
    Told told;
    net::Update update{updater, clock};

    Rig()
    {
        update.set_listener(&pipe);
        update.on_written(&written, &told);
        update.set_log(&keep, nullptr);
        logged.clear();
    }
    // The services loop, until the connection is over or nothing moves.
    void rounds(int most = 100000)
    {
        for (int i = 0; i < most; i++) {
            const size_t before = pipe.to_board.size() + pipe.from_board.size();
            const bool was_open = pipe.open;
            update.poll();
            clock.now += 1000;
            if (!pipe.open && !pipe.waiting && !was_open) {
                break;
            }
            if (pipe.to_board.size() + pipe.from_board.size() == before && pipe.open == was_open && !pipe.waiting) {
                break; // waiting for the sender
            }
        }
    }
    void call(const std::string &bytes)
    {
        pipe.to_board = bytes;
        pipe.waiting = true;
        rounds();
    }
};

void test_update()
{
    CHECK(dbg::crc32(reinterpret_cast<const uint8_t *>("123456789"), 9) == 0xCBF43926u); // the usual CRC-32, zlib's

    // Nobody: nothing. Then a whole update in one go.
    {
        Rig r;
        r.update.poll();
        CHECK(!r.update.busy() && r.updater.begins == 0);
        const std::string image = image_of(300000);
        r.call(header(image) + image);
        CHECK(r.pipe.from_board == "OK\nDONE\n" && r.pipe.closes == 1 && !r.update.busy());
        CHECK(r.updater.finished == 1 && r.updater.abandoned == 0 && r.updater.installed.size() == image.size());
        CHECK(memcmp(r.updater.installed.data(), image.data(), image.size()) == 0);
        CHECK(r.told.times == 1 && !r.told.restart && r.update.done() == 1 && r.update.failed() == 0);
        CHECK(logged == "update: receiving 300000 bytes\nupdate: 300000 bytes written; it runs from the next start\n");

        // The next one, asking for a restart, arriving a few bytes at a time
        // to a board whose answers leave a byte at a time too.
        const std::string second = image_of(5000);
        r.pipe.read_at_most = 7;
        r.pipe.write_at_most = 1;
        r.call(header(second, " restart") + second);
        CHECK(r.pipe.from_board == "OK\nDONE\n" && r.updater.finished == 2 && r.updater.installed.size() == 5000);
        CHECK(r.told.times == 2 && r.told.restart && r.update.done() == 2);
    }
    // The sender waits for OK before it sends the image, as the tool does.
    {
        Rig r;
        const std::string image = image_of(2500);
        r.call(header(image));
        CHECK(r.pipe.from_board == "OK\n" && r.update.busy() && r.update.size() == 2500 && r.update.received() == 0);
        r.pipe.to_board = image.substr(0, 1000);
        r.rounds();
        CHECK(r.update.received() == 1000 && r.update.busy());
        r.pipe.to_board = image.substr(1000);
        r.rounds();
        CHECK(r.pipe.from_board == "OK\nDONE\n" && !r.update.busy() && r.updater.installed.size() == 2500);
    }
    // First lines that are not understood: said, hung up, nothing begun.
    {
        const char *const wrong[] = {"HELLO\n", "UPDATE\n", "UPDATE 100\n", "UPDATE x 00000000\n", "UPDATE 0 00000000\n",
                                     "UPDATE 100 123\n", "UPDATE 100 0000000g\n", "UPDATE 100 00000000 now\n",
                                     "UPDATE 100 00000000 restart more\n", "UPDATE 99999999 00000000\n",
                                     "GET / HTTP/1.1\r\n"};
        for (const char *line : wrong) {
            Rig r;
            r.call(line);
            CHECK(r.pipe.from_board.rfind("ERROR ", 0) == 0 && r.pipe.closes == 1 && !r.update.busy());
            CHECK(r.updater.begins == 0 && r.told.times == 0 && r.update.failed() == 1);
        }
        Rig r;
        r.call(std::string(200, 'A') + "\n");
        CHECK(r.pipe.from_board == "ERROR the first line is too long\n" && r.updater.begins == 0);
    }
    // No room for it.
    {
        Rig r;
        r.updater.room = 1000;
        const std::string image = image_of(1001);
        r.call(header(image) + image);
        CHECK(r.pipe.from_board == "ERROR no room for it here\n" && r.updater.finished == 0 && r.told.times == 0);
    }
    // A byte changed on the way: the check differs, the update is given up.
    {
        Rig r;
        std::string image = image_of(4000);
        const std::string first = header(image);
        image[1234] ^= 1;
        r.call(first + image);
        CHECK(r.pipe.from_board == "OK\nERROR what arrived is not what was sent (the check differs)\n");
        CHECK(r.updater.finished == 0 && r.updater.abandoned == 1 && r.updater.installed.empty() && r.told.times == 0);
        CHECK(r.update.failed() == 1 && r.update.done() == 0);
    }
    // Not an image the machine can start.
    {
        Rig r;
        const std::string image = image_of(4000, 0x7F);
        r.call(header(image) + image);
        CHECK(r.pipe.from_board == "OK\nERROR it is not an image this machine can start\n");
        CHECK(r.updater.installed.empty() && r.told.times == 0 && r.update.failed() == 1);
    }
    // Writing fails half way.
    {
        Rig r;
        r.updater.fail_after = 3000;
        const std::string image = image_of(8000);
        r.call(header(image) + image);
        CHECK(r.pipe.from_board == "OK\nERROR it could not be written\n" && r.updater.abandoned == 1 && r.told.times == 0);
    }
    // The sender goes away half way, or goes quiet: given up, and the next
    // sender is served.
    {
        Rig r;
        const std::string image = image_of(6000);
        r.call(header(image) + image.substr(0, 2000));
        CHECK(r.update.busy() && r.update.received() == 2000);
        r.pipe.broken = true;
        r.rounds();
        CHECK(!r.update.busy() && r.updater.abandoned == 1 && r.update.failed() == 1 && r.pipe.closes == 1);
        CHECK(logged.find("the sender went away at 2000 of 6000 bytes") != std::string::npos);

        r.call(header(image) + image.substr(0, 100));
        CHECK(r.update.busy());
        r.clock.now += net::Update::kQuietUs - 500000;
        r.update.poll();
        CHECK(r.update.busy());
        r.clock.now += 600000;
        r.update.poll();
        CHECK(!r.update.busy() && r.updater.abandoned == 2 && r.update.failed() == 2);
        CHECK(logged.find("the sender went quiet") != std::string::npos);

        r.call(header(image) + image);
        CHECK(r.pipe.from_board == "OK\nDONE\n" && r.updater.installed.size() == 6000 && r.told.times == 1);
    }
    // Someone who connects and says nothing.
    {
        Rig r;
        r.call("");
        CHECK(r.update.busy());
        r.clock.now += net::Update::kQuietUs + 1;
        r.update.poll();
        CHECK(!r.update.busy() && r.updater.begins == 0);
    }
}

} // namespace

int main()
{
    test_update();
    if (failures == 0) {
        printf("update: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
