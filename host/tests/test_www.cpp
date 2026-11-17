// The browser's portable parts (core/www): addresses, reading HTML and
// laying a page out. `test_www --dump FILE [WIDTH]` prints a page as text,
// to look at what a real page turns into.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/www/html.hpp"
#include "core/www/http.hpp"
#include "core/www/url.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
// Unbuffered, so that what failed is seen even if a later line crashes.
static const int unbuffered = setvbuf(stdout, nullptr, _IONBF, 0);
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

std::string resolved(const char *base, const char *reference)
{
    char out[www::kMaxUrl];
    return www::resolve(base, reference, out, sizeof(out)) ? out : "(none)";
}

std::string typed(const char *text)
{
    char out[www::kMaxUrl];
    return www::from_typed(text, out, sizeof(out)) ? out : "(none)";
}

void test_urls()
{
    CHECK(www::scheme_of("http://a/") == www::Scheme::Http);
    CHECK(www::scheme_of("HTTPS://a/") == www::Scheme::Https);
    CHECK(www::scheme_of("about:home") == www::Scheme::About);
    CHECK(www::scheme_of("mailto:x@y") == www::Scheme::Other);
    CHECK(www::scheme_of("/a/b") == www::Scheme::None);
    CHECK(www::scheme_of("C:\\a") == www::Scheme::None);

    CHECK(typed("frogfind.com") == "http://frogfind.com/");
    CHECK(typed("  frogfind.com/about ") == "http://frogfind.com/about");
    CHECK(typed("http://frogfind.com") == "http://frogfind.com/");
    CHECK(typed("localhost:8080") == "http://localhost:8080/");
    CHECK(typed("C:\\docs\\a.htm") == "file:///C:/docs/a.htm");
    CHECK(typed("about:home") == "about:home");
    CHECK(typed("") == "(none)");

    const char *base = "http://host.example/a/b/page.html?x=1#top";
    CHECK(resolved(base, "other.html") == "http://host.example/a/b/other.html");
    CHECK(resolved(base, "/root.html") == "http://host.example/root.html");
    CHECK(resolved(base, "../up.html") == "http://host.example/a/up.html");
    CHECK(resolved(base, "../../../far.html") == "http://host.example/far.html");
    CHECK(resolved(base, "./here.html#part") == "http://host.example/a/b/here.html");
    CHECK(resolved(base, "?q=2") == "http://host.example/a/b/page.html?q=2");
    CHECK(resolved(base, "//other.example/x") == "http://other.example/x");
    CHECK(resolved(base, "http://third.example") == "http://third.example/");
    CHECK(resolved(base, "https://secure.example/") == "https://secure.example/");
    CHECK(resolved(base, "#top") == "http://host.example/a/b/page.html?x=1");
    CHECK(resolved(base, "a b.html") == "http://host.example/a/b/a%20b.html");
    CHECK(resolved("http://frogfind.com/", "/read?a=https%3A%2F%2Fx.org%2F&k=abc") ==
          "http://frogfind.com/read?a=https%3A%2F%2Fx.org%2F&k=abc");
    CHECK(resolved("http://frogfind.com", "/about") == "http://frogfind.com/about");
    CHECK(resolved("http://frogfind.com/?q=a/b", "more") == "http://frogfind.com/more");
    CHECK(resolved("file:///C:/docs/index.htm", "pics/a.htm") == "file:///C:/docs/pics/a.htm");
    CHECK(resolved("about:home", "x.html") == "(none)");

    char host[64];
    int port = 0;
    const char *path = nullptr;
    CHECK(www::split_http("http://frogfind.com/?q=x", host, sizeof(host), port, path));
    CHECK(strcmp(host, "frogfind.com") == 0 && port == 80 && strcmp(path, "/?q=x") == 0);
    CHECK(www::split_http("http://localhost:8080", host, sizeof(host), port, path));
    CHECK(strcmp(host, "localhost") == 0 && port == 8080 && path[0] == 0);
    CHECK(!www::split_http("https://frogfind.com/", host, sizeof(host), port, path));
    CHECK(!www::split_http("http://host:99999/", host, sizeof(host), port, path));

    char file[64];
    CHECK(www::file_path("file:///C:/docs/my%20page.htm", file, sizeof(file)));
    CHECK(strcmp(file, "C:\\docs\\my page.htm") == 0);

    char escaped[64] = "";
    const size_t length = www::append_escaped(escaped, 0, sizeof(escaped), "a b&c=d/\xC3\xA9");
    CHECK(strcmp(escaped, "a+b%26c%3Dd%2F%C3%A9") == 0 && length == strlen(escaped));
}

// The page's words, one line of text per laid-out line.
std::string as_text(const www::Document &doc)
{
    std::string out;
    int line_y = -1;
    for (int i = 0; i < doc.item_count(); i++) {
        const www::Item &item = doc.item(i);
        if (item.kind == www::Kind::Block) {
            continue;
        }
        if (item.y != line_y) {
            if (line_y >= 0) {
                out += '\n';
            }
            line_y = item.y;
        } else if (item.style & www::kSpace) {
            out += ' ';
        }
        switch (item.kind) {
        case www::Kind::Word: out += doc.text(item); break;
        case www::Kind::Rule: out += "----"; break;
        case www::Kind::Marker: out += item.extra == 0 ? "* " : std::to_string(item.extra) + ". "; break;
        case www::Kind::Field:
            {
                const www::Field &field = doc.field(item.extra);
                out += field.type == www::FieldType::CheckBox || field.type == www::FieldType::Radio
                           ? (field.checked ? "(x)" : "( )")
                           : std::string("[") + doc.shown(field) + "]";
            }
            break;
        case www::Kind::Image: out += std::string("{") + doc.text(doc.picture(item.extra).alt) + "}"; break;
        default: break;
        }
    }
    return out;
}

bool load(www::Document &doc, const char *html, int width = 600)
{
    if (!doc.load(reinterpret_cast<const uint8_t *>(html), strlen(html))) {
        return false;
    }
    doc.layout(width);
    return true;
}

const www::Item *word(const www::Document &doc, const char *text)
{
    for (int i = 0; i < doc.item_count(); i++) {
        if (doc.item(i).kind == www::Kind::Word && strcmp(doc.text(doc.item(i)), text) == 0) {
            return &doc.item(i);
        }
    }
    return nullptr;
}

void test_text()
{
    host::HostMemory memory;
    {
        www::Document doc(memory);
        CHECK(load(doc, "<html><head><title> A  page\n</title><style>b{}</style></head><body>Hello,   <b>bold</b>\n world"
                        "<script>if (a<b) document.write('<p>no')</script>!</body></html>"));
        CHECK(strcmp(doc.title(), "A page") == 0);
        CHECK(as_text(doc) == "Hello, bold world!");
        const www::Item *bold = word(doc, "bold");
        CHECK(bold != nullptr && (bold->style & www::kBold) && (bold->style & www::kSpace));
        const www::Item *world = word(doc, "world!");
        CHECK(world != nullptr && !(world->style & www::kBold));
        // Bold letters are a pixel wider.
        CHECK(bold->w == 4 * 7 && world->w == 6 * 6);
        CHECK(memory.bytes_in_use() > 0);
    }
    CHECK(memory.bytes_in_use() == 0); // the page gave its memory back
    {
        // What the font has is kept; the rest becomes plain.
        www::Document doc(memory);
        CHECK(load(doc, "caf&eacute; &amp; t&#233; &lt;b&gt; \xC3\xB1 \xF1 &copy;2026 \xE2\x80\x9Cquoted\xE2\x80\x9D "
                        "a&nbsp;b &unknown; AT&T &#8212; \x93x\x94 \xE6\x97\xA5"));
        CHECK(as_text(doc) == "caf\xC3\xA9 & t\xC3\xA9 <b> \xC3\xB1 \xC3\xB1 \xC2\xA9""2026 \"quoted\" a\xC2\xA0""b "
                              "&unknown; AT&T -- \"x\" ?");
    }
    {
        // Tags left open, closed twice or unknown do no harm.
        www::Document doc(memory);
        CHECK(load(doc, "<b>one <i>two</b> three</i></i></b> <blink>four</blink> <a href=x>five <a href=y>six</a> seven"
                        "<p>eight<p>nine</font></p></p> < ten <!-- no --> <!broken> eleven"));
        CHECK(as_text(doc) == "one two three four five six seven\neight\nnine\n< ten eleven");
        CHECK(word(doc, "three") != nullptr && word(doc, "three")->style == www::kSpace);
        CHECK(word(doc, "five")->link == 1 && word(doc, "six")->link == 2 && word(doc, "seven")->link == 0);
        CHECK(strcmp(doc.link(2), "y") == 0);
    }
    {
        www::Document doc(memory);
        CHECK(load(doc, "<h1>Title</h1>text<ul><li>one<li>two<ol><li>a</li><li>b</li></ol></ul><pre>\n  x  y\n\tz\n</pre>"
                        "<dl><dt>term<dd>meaning</dl><hr>end"));
        CHECK(as_text(doc) == "Title\ntext\n* one\n* two\n1. a\n2. b\n  x  y\n        z\nterm\nmeaning\n----\nend");
        CHECK(word(doc, "Title")->scale == 2 && (word(doc, "Title")->style & www::kBold));
        CHECK(word(doc, "a")->indent > word(doc, "one")->indent && word(doc, "one")->indent > word(doc, "text")->indent);
        CHECK(word(doc, "meaning")->x > word(doc, "term")->x);
        CHECK(word(doc, "  x  y")->style & www::kPre);
        CHECK(word(doc, "end")->y > word(doc, "Title")->y);
    }
    {
        www::Document doc(memory);
        CHECK(load(doc, "<body bgcolor=#000080 text=white link=\"#ffff00\"><font color=red size=7>R</font> w "
                        "<a href=/x>l</a> <font size=+1>p</font> <small>s</small> <center>mid</center>"
                        "<p align=right>right</p>"));
        CHECK(doc.background() == gfx::nearest({0, 0, 0x80}));
        CHECK(word(doc, "R")->colour == gfx::nearest({255, 0, 0}) && word(doc, "R")->scale == 3);
        CHECK(word(doc, "w")->colour == gfx::kWhite);
        CHECK(word(doc, "l")->colour == gfx::nearest({255, 255, 0}) && (word(doc, "l")->style & www::kUnderline));
        const www::Item *mid = word(doc, "mid");
        CHECK(mid->x == (600 - mid->w) / 2);
        const www::Item *right = word(doc, "right");
        CHECK(right->x + right->w == 600 - www::Document::kMargin);
    }
    {
        // Plain text is shown as it is.
        www::Document doc(memory);
        const char *text = "a <b> c\n  d &amp;";
        CHECK(doc.load(reinterpret_cast<const uint8_t *>(text), strlen(text), true));
        doc.layout(600);
        CHECK(as_text(doc) == "a <b> c\n  d &amp;");
    }
    {
        // An empty page, and one that is only a tag cut short.
        www::Document doc(memory);
        CHECK(load(doc, ""));
        CHECK(doc.item_count() == 0 && doc.height() == 2 * www::Document::kMargin);
        CHECK(load(doc, "<a href=\"never closed"));
        CHECK(load(doc, "text <"));
        CHECK(as_text(doc) == "text <");
    }
    CHECK(memory.bytes_in_use() == 0);
}

void test_layout()
{
    host::HostMemory memory;
    www::Document doc(memory);
    std::string html;
    for (int i = 0; i < 200; i++) {
        html += "word" + std::to_string(i) + " ";
    }
    html += "<br>http://a.very.long.address.example/that/does/not/have/any/space/in/it/and/is/wider/than/the/page/itself.html";
    CHECK(load(doc, html.c_str(), 300));
    // Nothing sticks out of the page, and the lines go down it.
    int last_y = -1;
    for (int i = 0; i < doc.item_count(); i++) {
        const www::Item &item = doc.item(i);
        CHECK(item.x >= www::Document::kMargin && item.x + item.w <= 300 - www::Document::kMargin);
        CHECK(item.y >= last_y);
        last_y = item.y;
    }
    CHECK(doc.height() > 300);
    const int tall = doc.height();
    doc.layout(600);
    CHECK(doc.height() < tall);
    // Finding what is at a place.
    const www::Item *w = word(doc, "word7");
    CHECK(w != nullptr);
    const int at = doc.item_at(w->x + 2, w->y + 3);
    CHECK(at >= 0 && &doc.item(at) == w);
    CHECK(doc.item_at(w->x + 2, doc.height() + 50) == -1);
    CHECK(doc.first_at(0) == 0 && doc.first_at(doc.height() + 1) == doc.item_count());

    // A word glued to a tag's end goes to the next line whole.
    CHECK(load(doc, "aaaaaaaaaa bbbbbbbbbb <b>cccc</b>dddd", 190));
    CHECK(word(doc, "cccc")->y == word(doc, "dddd")->y && word(doc, "cccc")->y > word(doc, "aaaaaaaaaa")->y);
}

// FrogFind's pages, as it wrote them on 2026-11-17 (the markup, with other
// words): the browser's benchmark.
const char *const kSearchPage =
    "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n<html>\n<head>\n"
    "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n<title>FrogFind!</title>\n</head>\n"
    "<body class=\"column\">\n<form action=\"/\" method=\"get\">\n"
    "<a href=\"/\"><font size=\"6\" color=\"#008000\">Frog</font><font size=\"6\" color=\"#000000\">Find!</font></a> "
    "Leap again: <input type=\"text\" size=\"30\" name=\"q\" value=\"chinchilla\">\n"
    "<input type=\"submit\" value=\"Ribbbit!\"> <a href=\"/options\">Options</a>\n</form>\n<hr>\n<br>\n"
    "<center>Search Results for <b>chinchilla</b></center>\n<br>\n"
    "<hr><br><a href=\"/read?a=https%3A%2F%2Fexample.org%2Fwiki%2FChinchilla&amp;k=Q9FS\"><font size=\"4\" "
    "color=\"#008000\"><b>Chinchilla - An Encyclopedia</b></font></a><br>"
    "<a href=\"/read?a=https%3A%2F%2Fexample.org%2Fwiki%2FChinchilla&amp;k=Q9FS\"><font size=\"2\" color=\"#262626\">"
    "https://example.org/wiki/Chinchilla</font></a><br>A rodent of the Andes &amp; its cousins.<br><br><br>\n"
    "<hr>\n<center>Page 1 | <a href=\"/?q=chinchilla&p=2\">Next Page &gt;</a></center>\n<br>\n"
    "<a href=\"/more\" title=\"ignore this link\" aria-label=\"ignore this link\"></a></body>\n</html>";

void test_frogfind()
{
    host::HostMemory memory;
    www::Document doc(memory);
    CHECK(load(doc, kSearchPage, 560));
    CHECK(strcmp(doc.title(), "FrogFind!") == 0);
    CHECK(as_text(doc) == "FrogFind! Leap again: [chinchilla] [Ribbbit!] Options\n----\n\nSearch Results for chinchilla\n\n"
                          "----\n\nChinchilla - An Encyclopedia\nhttps://example.org/wiki/Chinchilla\n"
                          "A rodent of the Andes & its cousins.\n\n\n----\nPage 1 | Next Page >\n");
    const www::Item *frog = word(doc, "Frog");
    CHECK(frog != nullptr && frog->scale == 2 && frog->colour == gfx::nearest({0, 0x80, 0}) && frog->link == 1);
    CHECK(strcmp(doc.link(frog->link), "/") == 0);
    const www::Item *result = word(doc, "Chinchilla");
    CHECK(result != nullptr && (result->style & www::kBold) && result->link != 0);
    CHECK(strcmp(doc.link(result->link), "/read?a=https%3A%2F%2Fexample.org%2Fwiki%2FChinchilla&k=Q9FS") == 0);
    CHECK(strcmp(doc.link(word(doc, "Next")->link), "/?q=chinchilla&p=2") == 0);

    // Searching again: what was typed goes into the address.
    CHECK(doc.field_count() == 2);
    CHECK(doc.field(0).type == www::FieldType::Text && doc.field(0).size == 30);
    snprintf(doc.field(0).typed, www::Document::kTypedBytes, "%s", "caf\xC3\xA9 & t\xC3\xA9");
    char url[www::kMaxUrl];
    bool posts = true;
    CHECK(doc.submit(1, "http://frogfind.com/?q=chinchilla", url, sizeof(url), posts) && !posts);
    CHECK(strcmp(url, "http://frogfind.com/?q=caf%C3%A9+%26+t%C3%A9") == 0);
}

void test_forms()
{
    host::HostMemory memory;
    www::Document doc(memory);
    CHECK(load(doc, "<form action=\"/options\" method=get><input type=hidden name=s value=1>"
                    "<input type=radio name=img value=1 checked> On<br><input type=radio name=img value=0> Off<br>"
                    "<input type=checkbox name=c> C <input type=checkbox name=d value=yes checked> D"
                    "<select name=t><option value=1>Normal<option value=2 selected>Large</option><option>Huge</select>"
                    "<textarea name=note cols=10>hello\nthere</textarea>"
                    "<input type=submit name=go value=Save><input type=reset><input type=file name=f></form>"
                    "<form action=post.cgi method=POST><input name=x><input type=submit></form>"));
    CHECK(as_text(doc) == "(x) On\n( ) Off\n( ) C (x) D[Large][hello there][Save][Reset]\n[][Submit]");
    char url[www::kMaxUrl];
    bool posts = false;
    const int save = 7;
    CHECK(doc.field(save).type == www::FieldType::Submit);
    CHECK(doc.submit(save, "http://frogfind.com/options?old=1", url, sizeof(url), posts));
    CHECK(strcmp(url, "http://frogfind.com/options?s=1&img=1&d=yes&t=2&note=hello+there&go=Save") == 0);
    // Another radio button, a tick, another choice; then Reset.
    doc.choose_radio(2);
    CHECK(!doc.field(1).checked && doc.field(2).checked);
    doc.field(3).checked = true;
    doc.field(5).chosen = 2;
    CHECK(doc.submit(save, "http://frogfind.com/options", url, sizeof(url), posts));
    CHECK(strcmp(url, "http://frogfind.com/options?s=1&img=0&c=on&d=yes&t=Huge&note=hello+there&go=Save") == 0);
    doc.reset(1);
    CHECK(doc.submit(6, "http://frogfind.com/options", url, sizeof(url), posts));
    CHECK(strcmp(url, "http://frogfind.com/options?s=1&img=1&d=yes&t=2&note=hello+there") == 0);
    // A form that posts is not sent, and says so.
    CHECK(!doc.submit(doc.field_count() - 1, "http://x/", url, sizeof(url), posts) && posts);
    // An address too long for the room is refused, not cut.
    CHECK(!doc.submit(save, "http://frogfind.com/options", url, 40, posts));
}

// A wire that plays a script: each connection gets the next answer, a few
// bytes at a time, and what was sent to it is kept.
class ScriptWire : public www::Wire {
public:
    struct Answer {
        std::string bytes;
        bool fails = false;
    };
    std::vector<Answer> answers;
    std::vector<std::string> hosts;
    std::vector<std::string> requests;
    size_t piece = 7;     // bytes handed over per read
    int slow = 2;         // polls before a connection opens
    bool silent = false;  // connects, and then says nothing
    int open = 0;         // connections not closed
    int write_limit = 5;

    int connect(const char *host, int port) override
    {
        if (static_cast<size_t>(next_) >= answers.size()) {
            return 0;
        }
        hosts.push_back(std::string(host) + ":" + std::to_string(port));
        requests.emplace_back();
        current_ = next_++;
        at_ = 0;
        waited_ = 0;
        open++;
        return current_ + 1;
    }
    State state(int) override
    {
        if (answers[current_].fails) {
            return State::Failed;
        }
        return ++waited_ > slow ? State::Open : State::Connecting;
    }
    const char *why(int) override { return "the site was not found"; }
    int read(int, uint8_t *out, size_t capacity) override
    {
        const std::string &bytes = answers[current_].bytes;
        if (silent) {
            return 0;
        }
        if (at_ >= bytes.size()) {
            return -1;
        }
        size_t n = bytes.size() - at_;
        n = n > piece ? piece : n;
        n = n > capacity ? capacity : n;
        memcpy(out, bytes.data() + at_, n);
        at_ += n;
        return static_cast<int>(n);
    }
    int write(int, const uint8_t *data, size_t size) override
    {
        const size_t n = size > static_cast<size_t>(write_limit) ? write_limit : size;
        requests.back().append(reinterpret_cast<const char *>(data), n);
        return static_cast<int>(n);
    }
    void close(int) override { open--; }

private:
    int next_ = 0, current_ = 0, waited_ = 0;
    size_t at_ = 0;
};

// Runs a fetch to its end. The clock moves 10 ms a poll.
www::Fetch::State run(www::Fetch &fetch, uint32_t &now, int polls = 100000)
{
    while (fetch.busy() && polls-- > 0) {
        now += 10000;
        fetch.poll(now);
    }
    return fetch.state();
}

std::string body_of(const www::Fetch &fetch)
{
    return std::string(reinterpret_cast<const char *>(fetch.body()), fetch.size());
}

void test_fetch()
{
    host::HostMemory memory;
    uint32_t now = 0xFFFF0000u; // the clock wraps during the tests
    {
        // A page with its length, and what was asked for.
        ScriptWire wire;
        wire.answers = {{"HTTP/1.1 200 OK\r\nServer: x\r\nContent-Type: text/html; charset=utf-8\r\n"
                         "Content-Length: 12\r\n\r\n<p>hello</p>and more that is not the page"}};
        www::Fetch fetch(wire, memory);
        CHECK(fetch.start("http://frogfind.com/?q=a b\xC3\xA9", now));
        CHECK(fetch.state() == www::Fetch::State::Connecting);
        CHECK(run(fetch, now) == www::Fetch::State::Done);
        CHECK(fetch.status() == 200 && body_of(fetch) == "<p>hello</p>" && !fetch.cut());
        CHECK(strcmp(fetch.content_type(), "text/html; charset=utf-8") == 0);
        CHECK(wire.hosts[0] == "frogfind.com:80" && wire.open == 0);
        CHECK(wire.requests[0] == std::string("GET /?q=a%20b%C3%A9 HTTP/1.0\r\nHost: frogfind.com\r\nUser-Agent: ") +
                                      www::Fetch::kAgent +
                                      "\r\nAccept: text/html, text/plain, */*\r\nConnection: close\r\n\r\n");
    }
    {
        // No length: the page ends when the server closes. A port in the address.
        ScriptWire wire;
        wire.answers = {{"HTTP/1.0 404 Not Found\nContent-Type: text/plain\n\nnot here"}};
        www::Fetch fetch(wire, memory);
        CHECK(fetch.start("http://localhost:8080", now));
        CHECK(run(fetch, now) == www::Fetch::State::Done);
        CHECK(fetch.status() == 404 && body_of(fetch) == "not here");
        CHECK(wire.hosts[0] == "localhost:8080");
        CHECK(wire.requests[0].find("GET / HTTP/1.0\r\nHost: localhost:8080\r\n") == 0);
    }
    {
        // In chunks, whatever the size of the pieces it arrives in.
        for (size_t piece : {1u, 3u, 1000u}) {
            ScriptWire wire;
            wire.piece = piece;
            wire.answers = {{"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n"
                             "b;ext=1\r\n, chunked w\r\n4\r\norld\r\n0\r\nX-Trailer: 1\r\n\r\nrubbish"}};
            www::Fetch fetch(wire, memory);
            CHECK(fetch.start("http://a.example/", now));
            CHECK(run(fetch, now) == www::Fetch::State::Done);
            CHECK(body_of(fetch) == "hello, chunked world");
        }
    }
    {
        // A site that has moved, twice; the address is where it ended up.
        ScriptWire wire;
        wire.answers = {{"HTTP/1.1 301 Moved\r\nLocation: /about\r\nContent-Length: 5\r\n\r\nmoved"},
                        {"HTTP/1.1 302 Found\r\nLocation: http://www.b.example/new/\r\n\r\n"},
                        {"HTTP/1.1 200 OK\r\n\r\nthere"}};
        www::Fetch fetch(wire, memory);
        CHECK(fetch.start("http://a.example/about.php", now));
        CHECK(run(fetch, now) == www::Fetch::State::Done);
        CHECK(body_of(fetch) == "there" && strcmp(fetch.url(), "http://www.b.example/new/") == 0);
        CHECK(wire.hosts.size() == 3 && wire.hosts[2] == "www.b.example:80" && wire.open == 0);
        CHECK(wire.requests[1].find("GET /about HTTP/1.0\r\nHost: a.example\r\n") == 0);
    }
    {
        // Moved to a secure address: said in words, since there is no TLS.
        ScriptWire wire;
        wire.answers = {{"HTTP/1.1 301 Moved\r\nLocation: https://a.example/\r\n\r\n"}};
        www::Fetch fetch(wire, memory);
        CHECK(fetch.start("http://a.example/", now));
        CHECK(run(fetch, now) == www::Fetch::State::Failed);
        CHECK(strstr(fetch.error(), "secure (https)") != nullptr && wire.open == 0);
        CHECK(strcmp(fetch.url(), "https://a.example/") == 0);
        CHECK(!fetch.start("https://a.example/", now) && strstr(fetch.error(), "secure") != nullptr);
        CHECK(!fetch.start("ftp://a.example/", now));
    }
    {
        // Round and round.
        ScriptWire wire;
        for (int i = 0; i < 10; i++) {
            wire.answers.push_back({"HTTP/1.1 302 Found\r\nLocation: /again\r\n\r\n"});
        }
        www::Fetch fetch(wire, memory);
        CHECK(fetch.start("http://a.example/", now));
        CHECK(run(fetch, now) == www::Fetch::State::Failed);
        CHECK(wire.hosts.size() == www::Fetch::kMaxMoves + 1 && wire.open == 0);
    }
    {
        // The site is not there; it says nothing; it hangs up at once.
        ScriptWire wire;
        wire.answers = {{"", true}, {""}, {""}};
        www::Fetch fetch(wire, memory);
        CHECK(fetch.start("http://nowhere.example/", now));
        CHECK(run(fetch, now) == www::Fetch::State::Failed);
        CHECK(strcmp(fetch.error(), "Could not reach nowhere.example: the site was not found.") == 0);
        wire.silent = true;
        CHECK(fetch.start("http://quiet.example/", now));
        const uint32_t before = now;
        CHECK(run(fetch, now) == www::Fetch::State::Failed);
        CHECK(now - before > www::Fetch::kQuietUs && now - before < www::Fetch::kQuietUs + 1000000);
        CHECK(strstr(fetch.error(), "did not answer") != nullptr);
        wire.silent = false;
        CHECK(fetch.start("http://rude.example/", now));
        CHECK(run(fetch, now) == www::Fetch::State::Failed);
        CHECK(strstr(fetch.error(), "without an answer") != nullptr && wire.open == 0);
        // No connection to be had.
        CHECK(!fetch.start("http://one.more.example/", now));
    }
    {
        // A page longer than is kept is cut, and the rest not read.
        ScriptWire wire;
        wire.piece = 1000;
        wire.answers = {{"HTTP/1.0 200 OK\r\n\r\n" + std::string(www::Fetch::kMaxBody + 5000, 'x')}};
        www::Fetch fetch(wire, memory);
        CHECK(fetch.start("http://big.example/", now));
        CHECK(run(fetch, now) == www::Fetch::State::Done);
        CHECK(fetch.cut() && fetch.size() == www::Fetch::kMaxBody && wire.open == 0);
        // Stopping gives the memory back, and so does starting again.
        fetch.stop();
        CHECK(memory.bytes_in_use() == 0 && fetch.state() == www::Fetch::State::Idle);
    }
    {
        // Stopped half way.
        ScriptWire wire;
        wire.answers = {{"HTTP/1.0 200 OK\r\n\r\n" + std::string(5000, 'x')}};
        www::Fetch fetch(wire, memory);
        CHECK(fetch.start("http://a.example/", now));
        run(fetch, now, 20);
        CHECK(fetch.busy() && wire.open == 1);
        fetch.stop();
        CHECK(wire.open == 0 && !fetch.busy());
    }
    {
        // An answer with no status line at all is all page.
        ScriptWire wire;
        wire.answers = {{"<html>\nold\n</html>"}};
        www::Fetch fetch(wire, memory);
        CHECK(fetch.start("http://old.example/", now));
        CHECK(run(fetch, now) == www::Fetch::State::Done);
        CHECK(fetch.status() == 200 && body_of(fetch) == "<html>\nold\n</html>");
    }
    CHECK(memory.bytes_in_use() == 0);
}

int dump(const char *path, int width)
{
    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        printf("cannot open %s\n", path);
        return 1;
    }
    std::vector<uint8_t> bytes;
    uint8_t buffer[4096];
    for (size_t got; (got = fread(buffer, 1, sizeof(buffer), f)) > 0;) {
        bytes.insert(bytes.end(), buffer, buffer + got);
    }
    fclose(f);
    host::HostMemory memory;
    www::Document doc(memory);
    if (!doc.load(bytes.data(), bytes.size())) {
        printf("no memory\n");
        return 1;
    }
    doc.layout(width);
    printf("%s\n", as_text(doc).c_str());
    printf("-- title \"%s\", %d items, %d links, %d fields, %d x %d pixels, %zu bytes for %zu of HTML\n", doc.title(),
           doc.item_count(), doc.link_count(), doc.field_count(), doc.width(), doc.height(), doc.bytes(), bytes.size());
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--dump") == 0) {
        return dump(argv[2], argc >= 4 ? atoi(argv[3]) : 600);
    }
    test_urls();
    test_text();
    test_layout();
    test_frogfind();
    test_forms();
    test_fetch();
    if (failures == 0) {
        printf("www: all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
