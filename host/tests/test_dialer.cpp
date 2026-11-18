// Outgoing connections over real sockets (net::PosixDialer, the board's and
// the PC's): a page fetched from a server on this machine, a port nobody
// listens on, and a name that does not exist.
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "core/net/posix_dialer.hpp"
#include "core/www/http.hpp"
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

// A Wire straight onto the dialer; in the system os::tcp stands between,
// with the dialling on a job.
class DialWire : public www::Wire {
public:
    explicit DialWire(net::Dialer &dialer) : dialer_(dialer) {}
    int connect(const char *host, int port) override
    {
        line_ = dialer_.connect(host, port);
        return 1;
    }
    State state(int) override { return line_ >= 0 ? State::Open : State::Failed; }
    const char *why(int) override { return net::dial_failure_text(static_cast<net::DialFailure>(-line_)); }
    int read(int, uint8_t *out, size_t capacity) override { return dialer_.read(line_, out, capacity); }
    int write(int, const uint8_t *data, size_t size) override { return dialer_.write(line_, data, size); }
    void close(int) override
    {
        if (line_ >= 0) {
            dialer_.hang_up(line_);
        }
        line_ = -1;
    }

private:
    net::Dialer &dialer_;
    int line_ = -1;
};

www::Fetch::State run(www::Fetch &fetch)
{
    uint32_t now = 0;
    for (int i = 0; i < 5000 && fetch.busy(); i++) {
        fetch.poll(now += 1000);
        usleep(1000);
    }
    return fetch.state();
}

} // namespace

int main()
{
    // A server of one answer, on a port the system picks.
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    CHECK(listen(listener, 1) == 0);
    socklen_t length = sizeof(address);
    getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length);
    const int port = ntohs(address.sin_port);
    std::string asked;
    std::thread server([&] {
        const int client = accept(listener, nullptr, nullptr);
        char buffer[1024];
        while (asked.find("\r\n\r\n") == std::string::npos) {
            const ssize_t got = recv(client, buffer, sizeof(buffer), 0);
            if (got <= 0) {
                break;
            }
            asked.append(buffer, static_cast<size_t>(got));
        }
        const char answer[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\n"
                              "5\r\n<p>hi\r\n4\r\n</p>\r\n0\r\n\r\n";
        send(client, answer, sizeof(answer) - 1, 0);
        close(client);
    });

    host::HostMemory memory;
    net::PosixDialer dialer;
    DialWire wire(dialer);
    char url[80];
    {
        www::Fetch fetch(wire, memory);
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/page?x=1", port);
        CHECK(fetch.start(url, 0));
        CHECK(run(fetch) == www::Fetch::State::Done);
        CHECK(fetch.status() == 200);
        CHECK(std::string(reinterpret_cast<const char *>(fetch.body()), fetch.size()) == "<p>hi</p>");
        CHECK(asked.compare(0, 23, "GET /page?x=1 HTTP/1.0\r") == 0);
        CHECK(dialer.lines_in_use() == 0);
    }
    server.join();
    close(listener); // and now nobody listens there
    {
        www::Fetch fetch(wire, memory);
        CHECK(fetch.start(url, 0));
        CHECK(run(fetch) == www::Fetch::State::Failed);
        CHECK(strstr(fetch.error(), "Could not reach 127.0.0.1") != nullptr);
        CHECK(dialer.lines_in_use() == 0);
    }
    {
        www::Fetch fetch(wire, memory);
        CHECK(fetch.start("http://no-such-host.invalid/", 0));
        CHECK(run(fetch) == www::Fetch::State::Failed);
        CHECK(dialer.lines_in_use() == 0);
    }
    printf(failures == 0 ? "ok\n" : "%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
