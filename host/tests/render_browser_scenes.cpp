// The web browser on the desktop, against a web that is a table in this
// file: renders its screens for the picture tests (tools/check_pictures.py)
// and checks what it asked the network for.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/fs/memory_volume.hpp"
#include "core/init/init.hpp"
#include "core/net/dialer.hpp"
#include "core/os/tcp.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"

namespace {

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
wm::Wm desktop(display, pointer, clock_);
std::string folder;
int failures = 0;

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

// The web: what each address answers. A line is a request being read, then
// its answer being handed over.
class TableWeb : public net::Dialer {
public:
    std::map<std::string, std::string> pages; // "host/path" -> the whole answer
    std::vector<std::string> asked;
    std::string agent;

    int connect(const char *host, int port) override
    {
        if (strcmp(host, "nowhere.example") == 0) {
            return -static_cast<int>(net::DialFailure::NotFound);
        }
        for (int i = 0; i < 4; i++) {
            if (!lines_[i].used) {
                lines_[i] = Line();
                lines_[i].used = true;
                lines_[i].host = port == 80 ? host : std::string(host) + ":" + std::to_string(port);
                return i;
            }
        }
        return -static_cast<int>(net::DialFailure::NoLines);
    }
    int read(int line, uint8_t *out, size_t capacity) override
    {
        Line &l = lines_[line];
        if (!l.answering) {
            return 0;
        }
        if (l.at >= l.answer.size()) {
            return -1;
        }
        size_t n = l.answer.size() - l.at;
        n = n > capacity ? capacity : n;
        n = n > 700 ? 700 : n; // in pieces, as a network gives them
        memcpy(out, l.answer.data() + l.at, n);
        l.at += n;
        return static_cast<int>(n);
    }
    int write(int line, const uint8_t *data, size_t size) override
    {
        Line &l = lines_[line];
        l.request.append(reinterpret_cast<const char *>(data), size);
        if (!l.answering && l.request.find("\r\n\r\n") != std::string::npos) {
            const size_t path_end = l.request.find(" HTTP/");
            const std::string address = l.host + l.request.substr(4, path_end - 4);
            asked.push_back(address);
            const size_t ua = l.request.find("User-Agent: ");
            agent = l.request.substr(ua + 12, l.request.find("\r\n", ua) - ua - 12);
            const auto found = pages.find(address);
            l.answer = found != pages.end() ? found->second
                                            : "HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\n\r\n"
                                              "<h1>404</h1>Nothing is at this address.";
            l.answering = true;
        }
        return static_cast<int>(size);
    }
    void hang_up(int line) override { lines_[line].used = false; }
    int in_use() const
    {
        int n = 0;
        for (const Line &l : lines_) {
            n += l.used;
        }
        return n;
    }

private:
    struct Line {
        bool used = false, answering = false;
        std::string host, request, answer;
        size_t at = 0;
    };
    Line lines_[4];
};

TableWeb web;

std::string page(const std::string &html)
{
    return "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + std::to_string(html.size()) +
           "\r\n\r\n" + html;
}

std::string answer(const std::string &type, const std::string &body)
{
    return "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\n\r\n" + body;
}

// A picture made here: a BMP of 24 bits, hills under a sky with a sun.
std::string bmp(int width, int height)
{
    const int row = (width * 3 + 3) / 4 * 4;
    std::string out(54 + static_cast<size_t>(row) * height, '\0');
    const auto put = [&out](size_t at, uint32_t value, int bytes) {
        for (int i = 0; i < bytes; i++) {
            out[at + i] = static_cast<char>(value >> (8 * i));
        }
    };
    out[0] = 'B';
    out[1] = 'M';
    put(2, static_cast<uint32_t>(out.size()), 4);
    put(10, 54, 4);
    put(14, 40, 4);
    put(18, static_cast<uint32_t>(width), 4);
    put(22, static_cast<uint32_t>(height), 4);
    put(26, 1, 2);
    put(28, 24, 2);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int from_top = height - 1 - y; // a BMP's rows run upwards
            int r = 120 + 100 * from_top / height, g = 170 + 60 * from_top / height, b = 240;
            const int dx = x - width * 3 / 4, dy = from_top - height / 4, sun = height / 6;
            if (dx * dx + dy * dy < sun * sun) {
                r = 250, g = 210, b = 60;
            }
            const int hill = height * 2 / 3 + ((x * 7 / width) % 2 == 0 ? x % (width / 7 + 1) : width / 7 - x % (width / 7 + 1)) / 3;
            if (from_top > hill) {
                r = 60 + 40 * x / width, g = 140, b = 70;
            }
            const size_t at = 54 + static_cast<size_t>(y) * row + static_cast<size_t>(x) * 3;
            out[at] = static_cast<char>(b);
            out[at + 1] = static_cast<char>(g);
            out[at + 2] = static_cast<char>(r);
        }
    }
    return out;
}

// A picture made here: an interlaced GIF with a transparent background, a
// frog's face. The LZW is the plainest there is: a clear code often enough
// that every code stays 9 bits wide and stands for one pixel.
std::string gif(int width, int height)
{
    std::string out = "GIF89a";
    const auto put16 = [&out](int v) {
        out += static_cast<char>(v & 255);
        out += static_cast<char>(v >> 8);
    };
    put16(width);
    put16(height);
    out += std::string("\xF2\x00\x00", 3); // its own colours, 8 of them
    const uint8_t colours[8][3] = {{255, 0, 255}, {40, 150, 60}, {20, 90, 40}, {255, 255, 255},
                                   {0, 0, 0},     {230, 90, 90}, {0, 0, 0},    {0, 0, 0}};
    out.append(reinterpret_cast<const char *>(colours), sizeof(colours));
    out += std::string("\x21\xF9\x04\x01\x00\x00\x00\x00", 8); // colour 0 is transparent
    out += ',';
    put16(0);
    put16(0);
    put16(width);
    put16(height);
    out += '\x40'; // interlaced
    out += '\x08';
    const auto pixel = [width, height](int x, int y) -> int {
        const auto inside = [x, y](int cx, int cy, int rx, int ry) {
            const long dx = x - cx, dy = y - cy;
            return dx * dx * ry * ry + dy * dy * rx * rx < static_cast<long>(rx) * rx * ry * ry;
        };
        const int cx = width / 2, cy = height * 5 / 8, eye = height / 5;
        for (int side = -1; side <= 1; side += 2) {
            const int ex = cx + side * width / 6, ey = height / 4;
            if (inside(ex, ey, eye / 3, eye / 3)) {
                return 4;
            }
            if (inside(ex, ey, eye * 3 / 4, eye * 3 / 4)) {
                return 3;
            }
            if (inside(ex, ey, eye, eye)) {
                return 2;
            }
        }
        if (inside(cx, cy + height / 10, width / 5, height / 16)) {
            return 5;
        }
        if (inside(cx, cy, width * 3 / 10 - 3, height * 3 / 8 - 3)) {
            return 1;
        }
        return inside(cx, cy, width * 3 / 10, height * 3 / 8) ? 2 : 0;
    };
    std::string data;
    uint32_t bits = 0;
    int nbits = 0, since_clear = 0;
    const auto code = [&](uint32_t c) {
        bits |= c << nbits;
        for (nbits += 9; nbits >= 8; nbits -= 8, bits >>= 8) {
            data += static_cast<char>(bits & 255);
        }
    };
    code(256);
    static const int first[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
    for (int pass = 0; pass < 4; pass++) {
        for (int y = first[pass]; y < height; y += step[pass]) {
            for (int x = 0; x < width; x++) {
                if (++since_clear == 250) {
                    code(256);
                    since_clear = 1;
                }
                code(static_cast<uint32_t>(pixel(x, y)));
            }
        }
    }
    code(257);
    if (nbits > 0) {
        data += static_cast<char>(bits & 255);
    }
    for (size_t at = 0; at < data.size(); at += 255) {
        const size_t n = data.size() - at < 255 ? data.size() - at : 255;
        out += static_cast<char>(n);
        out.append(data, at, n);
    }
    out += '\0';
    out += ';';
    return out;
}

void save(const char *name)
{
    desktop.update();
    std::string path = folder + "/" + name + ".raw";
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr || fwrite(display.pixels(), 1, 640 * 480, f) != 640 * 480) {
        printf("cannot write %s\n", path.c_str());
        failures++;
    }
    if (f != nullptr) {
        fclose(f);
    }
}

// Time passes: the browser's timer fetches the page.
void wait(int ms)
{
    for (int i = 0; i < ms / 10; i++) {
        clock_.now += 10000;
        desktop.tick();
    }
}

void mouse(int x, int y, uint8_t buttons)
{
    clock_.now += 300000;
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
}

void click(int x, int y)
{
    mouse(x, y, 0);
    mouse(x, y, 1);
    mouse(x, y, 0);
}

void key(uint16_t usage, bool alt = false)
{
    if (alt) {
        desktop.key(wm::key::LeftAlt, true);
    }
    desktop.key(usage, true);
    desktop.key(usage, false);
    if (alt) {
        desktop.key(wm::key::LeftAlt, false);
    }
    clock_.now += 100000;
}

const char *title() { return desktop.active() != nullptr ? desktop.active()->title() : ""; }

// FrogFind's pages as it wrote them on 2026-11-17: its markup, other words.
const char *const kFrogHome =
    "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n<html>\n<head>\n"
    "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n<title>FrogFind!</title>\n</head>\n"
    "<body class=\"column\">\n<br><br>\n"
    "<center><h1><font size=\"7\"><font color=\"#008000\">Frog</font>Find!</font></h1></center>\n"
    "<center><img src=\"/img/frogfind.gif\" width=\"174\" height=\"80\" alt=\"FrogFind frog logo\"></center>\n"
    "<center><h3>Welcome Back to the Simple Internet</h3></center>\n<br>\n<center>\n"
    "<form action=\"/\" method=\"get\">\n<center>Web search: <input type=\"text\" size=\"30\" name=\"q\"></center>\n"
    "<center><input type=\"submit\" value=\"Ribbbit!\"></center>\n</form>\n</center>\n<br>\n"
    "<center><small><a href=\"/weather\">Weather</a> | <a href=\"/news\">News</a> | <a href=\"/about\">About</a> | "
    "<a href=\"/options\">Options</a></small></center>\n<br>\n"
    "<center><small>Built by <b><a href=\"https://youtube.com/ActionRetro\">Action Retro</a></b> | Powered by "
    "DuckDuckGo | v2.0</small></center>\n</body>\n</html>";

std::string frog_results()
{
    std::string html =
        "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n<html>\n<head>\n<title>FrogFind!</title>\n</head>\n"
        "<body class=\"column\">\n<form action=\"/\" method=\"get\">\n"
        "<a href=\"/\"><font size=\"6\" color=\"#008000\">Frog</font><font size=\"6\" color=\"#000000\">Find!</font></a> "
        "Leap again: <input type=\"text\" size=\"30\" name=\"q\" value=\"chinchilla\">\n"
        "<input type=\"submit\" value=\"Ribbbit!\"> <a href=\"/options\">Options</a>\n</form>\n<hr>\n<br>\n"
        "<center>Search Results for <b>chinchilla</b></center>\n<br>\n<hr><br>";
    const char *const results[][3] = {
        {"Chinchilla - An Encyclopedia", "https://encyclopedia.example/wiki/Chinchilla",
         "Chinchillas are two species of crepuscular rodents native to the Andes mountains in South America. They "
         "live in colonies called herds at high elevations, up to 4,270 m."},
        {"Caring for a chinchilla &amp; its teeth", "https://pets.example/chinchilla-care/",
         "A dust bath twice a week, hay every day and somewhere cool to sleep: what a chinchilla needs."},
        {"Chinchilla (animal) - la enciclopedia libre", "https://es.encyclopedia.example/wiki/Chinchilla_(animal)",
         "Art&#237;culo sobre el g&#233;nero de roedores Chinchilla, end&#233;mico de los Andes, con dos especies "
         "salvajes y una dom&#233;stica."},
        {"Vizcachas and chinchillas compared", "https://andes.example/vizcacha",
         "The vizcacha is the chinchilla's larger cousin, with the ears of a rabbit and the tail of a squirrel."},
    };
    for (const auto &r : results) {
        html += std::string("<a href=\"/read?a=") + "x&amp;k=Q9FS\"><font size=\"4\" color=\"#008000\"><b>" + r[0] +
                "</b></font></a><br><a href=\"/read?a=x&amp;k=Q9FS\"><font size=\"2\" color=\"#262626\">" + r[1] +
                "</font></a><br>" + r[2] + "<br><br><br>";
    }
    html += "\n<hr>\n<center>Page 1 | <a href=\"/?q=chinchilla&p=2\">Next Page &gt;</a></center>\n<br>\n</body>\n</html>";
    return html;
}

const char *const kArticle =
    "<html><head><title>The vizcacha</title></head><body bgcolor=\"#FFFFCC\" text=\"#000000\" link=\"#800000\">"
    "<h1>The vizcacha</h1><p>The <b>vizcacha</b> is a rodent of the family <i>Chinchillidae</i>, the "
    "<u>chinchilla's</u> larger cousin. <s>It is a rabbit.</s> It is not a rabbit, whatever its ears say.</p>"
    "<h2>Where it lives</h2><ul><li>The plains vizcacha, on the pampas<li>The mountain vizcachas, in the Andes"
    "<ol><li>northern<li>southern</ol></ul>"
    "<blockquote>\"It sits in the sun on a rock for most of the morning.\"</blockquote>"
    "<pre>species      weight\nplains       up to 9 kg\nmountain     up to 3 kg</pre>"
    "<dl><dt>Crepuscular<dd>Awake at dawn and at dusk.</dl><hr>"
    "<table><tr><th>Name</th><th>Home</th></tr><tr><td>Lagostomus</td><td>plains</td></tr></table>"
    "<p align=\"right\"><small>A page written for the tests, 2026.</small></p>"
    "<form action=\"/vote\"><input type=checkbox name=a checked> rabbits <input type=radio name=b value=1 checked> yes "
    "<input type=radio name=b value=2> no <select name=c><option>plains<option>mountain</select> "
    "<input type=submit value=\"Vote\"><input type=reset></form>"
    "<p><a href=\"https://secure.example/more\">More, on a secure site</a> | <a href=\"missing.html\">A dead link</a> | "
    "<a href=\"http://nowhere.example/\">A site that is not there</a></p>"
    "</body></html>";

void scenes()
{
    shell::open_browser(nullptr);
    wait(50);
    CHECK(strcmp(title(), "Home - Vizcacha") == 0);
    save("60-browser-home");

    // An address typed, as people type them. The site has moved once.
    key(wm::key::A + ('d' - 'a'), true); // Alt+D: the address
    desktop.text("frogfind.com");
    key(wm::key::Enter);
    wait(300);
    CHECK(strcmp(title(), "FrogFind! - Vizcacha") == 0);
    // The page, and then its picture: a GIF, see-through around the frog.
    CHECK(web.asked.size() == 3 && web.asked[0] == "frogfind.com/" && web.asked[1] == "www.frogfind.com/" &&
          web.asked[2] == "www.frogfind.com/img/frogfind.gif");
    CHECK(web.agent.find("Mozilla/4.0 (compatible;") == 0);
    CHECK(web.in_use() == 0 && os::tcp::open_connections() == 0);
    save("61-browser-frogfind");

    // A search: a click in the page's field, the words, Enter.
    click(330, 348);
    desktop.text("chinchilla");
    save("62-browser-typing");
    key(wm::key::Enter);
    wait(300);
    CHECK(web.asked.back() == "www.frogfind.com/?q=chinchilla");
    save("63-browser-results");

    // Down the page with the keys, then a result's link.
    key(wm::key::PageDown);
    save("64-browser-scrolled");
    key(wm::key::Home);
    click(120, 258);
    wait(300);
    CHECK(web.asked.back() == "www.frogfind.com/read?a=x&k=Q9FS");
    CHECK(strcmp(title(), "The vizcacha - Vizcacha") == 0);
    save("65-browser-article");
    key(wm::key::End);
    save("66-browser-article-end");

    // Back to the results, where they were left; forward again.
    key(wm::key::Left, true);
    wait(300);
    CHECK(strcmp(title(), "FrogFind! - Vizcacha") == 0);
    key(wm::key::Right, true);
    wait(300);
    CHECK(strcmp(title(), "The vizcacha - Vizcacha") == 0);

    // What cannot be opened says why.
    key(wm::key::A + ('d' - 'a'), true);
    desktop.text("https://secure.example/more");
    key(wm::key::Enter);
    wait(100);
    CHECK(strcmp(title(), "Page not opened - Vizcacha") == 0);
    save("67-browser-secure");
    key(wm::key::A + ('d' - 'a'), true);
    desktop.text("nowhere.example");
    key(wm::key::Enter);
    wait(300);
    CHECK(strcmp(title(), "Page not opened - Vizcacha") == 0);
    save("68-browser-not-found");

    // A page on the drive.
    key(wm::key::A + ('d' - 'a'), true);
    desktop.text("C:\\Documents\\page.htm");
    key(wm::key::Enter);
    wait(100);
    CHECK(strcmp(title(), "A page on the drive - Vizcacha") == 0);
    save("69-browser-file");

    // Pictures: one at the size the page gives, inside a link; one wider
    // than the page, made to fit; one that is not there; one from a folder
    // beside the page.
    key(wm::key::A + ('d' - 'a'), true);
    desktop.text("andes.example/album.html");
    key(wm::key::Enter);
    wait(3000);
    CHECK(strcmp(title(), "An album - Vizcacha") == 0);
    CHECK(web.asked[web.asked.size() - 4] == "andes.example/small.bmp" &&
          web.asked[web.asked.size() - 3] == "andes.example/wide.bmp" &&
          web.asked[web.asked.size() - 2] == "andes.example/gone.bmp" &&
          web.asked.back() == "andes.example/more/hills.bmp");
    CHECK(web.in_use() == 0 && os::tcp::open_connections() == 0);
    save("70-browser-pictures");
    key(wm::key::End);
    save("71-browser-pictures-end");

    // A picture opened by itself: the link around the first one.
    key(wm::key::Home);
    click(190, 180);
    wait(2000);
    CHECK(web.asked.back() == "andes.example/wide.bmp");
    save("72-browser-picture-alone");

    // A page on the drive with a picture beside it.
    key(wm::key::A + ('d' - 'a'), true);
    desktop.text("C:\\Documents\\album.htm");
    key(wm::key::Enter);
    wait(100);
    save("73-browser-file-picture");

    // Closed: nothing of it stays.
    key(wm::key::F4, true);
    wait(100);
    CHECK(desktop.active() == nullptr);
    CHECK(web.in_use() == 0 && os::tcp::open_connections() == 0);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: render_browser_scenes <folder>\n");
        return 2;
    }
    folder = argv[1];

    static host::HostMemory memory;
    static fs::Storage storage;
    static fs::MemoryVolume ram_disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    web.pages["frogfind.com/"] = "HTTP/1.1 301 Moved Permanently\r\nLocation: http://www.frogfind.com/\r\n\r\n";
    web.pages["www.frogfind.com/"] = page(kFrogHome);
    web.pages["www.frogfind.com/img/frogfind.gif"] = answer("image/gif", gif(174, 80));
    web.pages["www.frogfind.com/?q=chinchilla"] = page(frog_results());
    web.pages["www.frogfind.com/read?a=x&k=Q9FS"] = page(kArticle);
    web.pages["andes.example/album.html"] = page(
        "<html><head><title>An album</title></head><body><h2>An album</h2>"
        "<p>The valley, small: <a href=\"wide.bmp\"><img src=\"small.bmp\" width=96 height=60 alt=\"The valley\"></a> "
        "and the text goes on beside it.</p>"
        "<p><img src=\"/wide.bmp\" alt=\"The valley, wide\"></p>"
        "<p>One that is gone: <img src=\"gone.bmp\" alt=\"A lost picture\"> and the hills, half as wide as they are: "
        "<img src=\"more/hills.bmp\" width=120></p><p>The end of the album.</p></body></html>");
    web.pages["andes.example/small.bmp"] = answer("image/bmp", bmp(192, 120));
    web.pages["andes.example/wide.bmp"] = answer("image/bmp", bmp(660, 100));
    web.pages["andes.example/more/hills.bmp"] = answer("image/bmp", bmp(240, 160));
    os::install_dialer(&web);

    static init::Init first(proc::table()); // PID 1, as on the board; the desktop is its child
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_animations(false);
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    wait(40);
    storage.make_folder("C:\\Documents");
    const char *file = "<title>A page on the drive</title><h2>Saved pages open too</h2>"
                       "<p>This one is <tt>C:\\Documents\\page.htm</tt>. <a href=\"other.htm\">Its neighbour</a>.";
    CHECK(storage.write_file("C:\\Documents\\page.htm", file, strlen(file)) == ports::FsError::Ok);
    const char *album = "<title>An album on the drive</title><p>A picture beside this page: <img src=\"hills.bmp\"></p>";
    CHECK(storage.write_file("C:\\Documents\\album.htm", album, strlen(album)) == ports::FsError::Ok);
    const std::string hills = bmp(160, 100);
    CHECK(storage.write_file("C:\\Documents\\hills.bmp", hills.data(), hills.size()) == ports::FsError::Ok);
    const size_t before = memory.bytes_in_use();
    scenes();
    CHECK(memory.bytes_in_use() == before); // the browser gave back all it took
    return failures == 0 ? 0 : 1;
}
