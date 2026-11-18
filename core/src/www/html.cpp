#include "core/www/html.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <strings.h>

#include "core/www/url.hpp"

namespace www {

namespace {

/* ---- Characters -------------------------------------------------------------------- */

// The names of the characters 160 to 255, in order.
const char *const kLatin1Names[96] = {
    "nbsp",   "iexcl",  "cent",   "pound",  "curren", "yen",    "brvbar", "sect",   "uml",    "copy",   "ordf",
    "laquo",  "not",    "shy",    "reg",    "macr",   "deg",    "plusmn", "sup2",   "sup3",   "acute",  "micro",
    "para",   "middot", "cedil",  "sup1",   "ordm",   "raquo",  "frac14", "frac12", "frac34", "iquest", "Agrave",
    "Aacute", "Acirc",  "Atilde", "Auml",   "Aring",  "AElig",  "Ccedil", "Egrave", "Eacute", "Ecirc",  "Euml",
    "Igrave", "Iacute", "Icirc",  "Iuml",   "ETH",    "Ntilde", "Ograve", "Oacute", "Ocirc",  "Otilde", "Ouml",
    "times",  "Oslash", "Ugrave", "Uacute", "Ucirc",  "Uuml",   "Yacute", "THORN",  "szlig",  "agrave", "aacute",
    "acirc",  "atilde", "auml",   "aring",  "aelig",  "ccedil", "egrave", "eacute", "ecirc",  "euml",   "igrave",
    "iacute", "icirc",  "iuml",   "eth",    "ntilde", "ograve", "oacute", "ocirc",  "otilde", "ouml",   "divide",
    "oslash", "ugrave", "uacute", "ucirc",  "uuml",   "yacute", "thorn",  "yuml",
};

struct Named {
    const char *name;
    uint32_t code;
};
const Named kOtherNames[] = {
    {"amp", '&'},      {"lt", '<'},       {"gt", '>'},       {"quot", '"'},     {"apos", '\''},
    {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
    {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bull", 0x2022},  {"euro", 0x20AC},  {"trade", 0x2122},
    {"larr", 0x2190},  {"rarr", 0x2192},  {"laquo", 0xAB},   {"raquo", 0xBB},
};

// Windows-1252 has printable characters where Latin-1 has none (128 to
// 159), and old pages use them, as bytes and as numbers.
uint32_t from_cp1252(uint32_t code)
{
    static const uint16_t kMap[32] = {
        0x20AC, '?',    0x201A, 'f',    0x201E, 0x2026, '+',    '+',    '^',    '%',    'S',
        '<',    'O',    '?',    'Z',    '?',    '?',    0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, '~',    0x2122, 's',    '>',    'o',    '?',    'z',    'Y',
    };
    return code >= 128 && code < 160 ? kMap[code - 128] : code;
}

// The next character of the source as a Unicode number, moving `p` past it.
// `entities`: "&amp;" and "&#233;" are read as what they stand for.
uint32_t next_code(const char *&p, const char *end, bool entities)
{
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '&' && entities && p + 1 < end) {
        const char *q = p + 1;
        if (*q == '#') {
            q++;
            uint32_t code = 0;
            int digits = 0;
            if (q < end && (*q == 'x' || *q == 'X')) {
                q++;
                while (q < end && isxdigit(static_cast<unsigned char>(*q)) && digits < 8) {
                    const char hex[2] = {*q, 0};
                    code = code * 16 + static_cast<uint32_t>(strtol(hex, nullptr, 16));
                    q++;
                    digits++;
                }
            } else {
                while (q < end && isdigit(static_cast<unsigned char>(*q)) && digits < 8) {
                    code = code * 10 + static_cast<uint32_t>(*q - '0');
                    q++;
                    digits++;
                }
            }
            if (digits > 0) {
                if (q < end && *q == ';') {
                    q++;
                }
                p = q;
                return code == 0 ? '?' : from_cp1252(code);
            }
        } else {
            char name[10];
            size_t n = 0;
            while (q + n < end && n < sizeof(name) - 1 && isalnum(static_cast<unsigned char>(q[n]))) {
                name[n] = q[n];
                n++;
            }
            name[n] = 0;
            uint32_t code = 0;
            for (const Named &named : kOtherNames) {
                if (strcmp(named.name, name) == 0) {
                    code = named.code;
                }
            }
            for (int i = 0; code == 0 && i < 96; i++) {
                if (strcmp(kLatin1Names[i], name) == 0) {
                    code = 160 + static_cast<uint32_t>(i);
                }
            }
            if (code != 0) {
                q += n;
                if (q < end && *q == ';') {
                    q++;
                }
                p = q;
                return code;
            }
        }
        p++;
        return '&';
    }
    p++;
    if (c < 0x80) {
        return c;
    }
    // UTF-8 if it is well formed; otherwise the byte is Latin-1 (or 1252).
    int more = 0;
    uint32_t code = 0;
    if ((c & 0xE0) == 0xC0) {
        more = 1;
        code = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        more = 2;
        code = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        more = 3;
        code = c & 0x07;
    }
    if (more > 0 && p + more <= end) {
        bool good = true;
        for (int i = 0; i < more; i++) {
            const unsigned char next = static_cast<unsigned char>(p[i]);
            if ((next & 0xC0) != 0x80) {
                good = false;
                break;
            }
            code = (code << 6) | (next & 0x3F);
        }
        if (good && code >= 0x80) {
            p += more;
            return code;
        }
    }
    return from_cp1252(c);
}

// A character as the font can show it, as UTF-8 into `out` (8 bytes are
// enough). Returns how many bytes.
size_t shown_as(uint32_t code, char *out)
{
    if (code < 0x80) {
        out[0] = static_cast<char>(code);
        return 1;
    }
    if (code >= 0xA0 && code <= 0xFF) {
        out[0] = static_cast<char>(0xC0 | (code >> 6));
        out[1] = static_cast<char>(0x80 | (code & 0x3F));
        return 2;
    }
    const char *plain = "?";
    switch (code) {
    case 0x2018:
    case 0x2019:
    case 0x201A:
    case 0x2032: plain = "'"; break;
    case 0x201C:
    case 0x201D:
    case 0x201E:
    case 0x2033: plain = "\""; break;
    case 0x2010:
    case 0x2011:
    case 0x2012:
    case 0x2013:
    case 0x2212: plain = "-"; break;
    case 0x2014:
    case 0x2015: plain = "--"; break;
    case 0x2026: plain = "..."; break;
    case 0x2022:
    case 0x25CF:
    case 0x25AA: plain = "\xC2\xB7"; break;
    case 0x20AC: plain = "EUR"; break;
    case 0x2122: plain = "(TM)"; break;
    case 0x2190: plain = "<-"; break;
    case 0x2192: plain = "->"; break;
    case 0x2039: plain = "<"; break;
    case 0x203A: plain = ">"; break;
    case 0x2002:
    case 0x2003:
    case 0x2009:
    case 0x202F: plain = " "; break;
    case 0x200B:
    case 0x200C:
    case 0x200D:
    case 0x200E:
    case 0x200F:
    case 0x2060:
    case 0xFEFF: plain = ""; break;
    default: break;
    }
    const size_t length = strlen(plain);
    memcpy(out, plain, length);
    return length;
}

// A character as UTF-8, whatever it is (for addresses).
size_t as_utf8(uint32_t code, char *out)
{
    if (code < 0x80) {
        out[0] = static_cast<char>(code);
        return 1;
    }
    if (code < 0x800) {
        out[0] = static_cast<char>(0xC0 | (code >> 6));
        out[1] = static_cast<char>(0x80 | (code & 0x3F));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (code >> 12));
        out[1] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (code & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | ((code >> 18) & 0x07));
    out[1] = static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (code & 0x3F));
    return 4;
}

int count_chars(const char *text)
{
    int count = 0;
    for (; *text != 0; text++) {
        if ((static_cast<unsigned char>(*text) & 0xC0) != 0x80) {
            count++;
        }
    }
    return count;
}

size_t round4(size_t bytes) { return (bytes + 3) & ~static_cast<size_t>(3); }
size_t round8(size_t bytes) { return (bytes + 7) & ~static_cast<size_t>(7); }

} // namespace

bool parse_colour(const char *text, gfx::Rgb &out)
{
    static const struct {
        const char *name;
        uint32_t rgb;
    } kNames[] = {
        {"black", 0x000000},  {"silver", 0xC0C0C0}, {"gray", 0x808080},   {"grey", 0x808080},
        {"white", 0xFFFFFF},  {"maroon", 0x800000}, {"red", 0xFF0000},    {"purple", 0x800080},
        {"fuchsia", 0xFF00FF}, {"green", 0x008000}, {"lime", 0x00FF00},   {"olive", 0x808000},
        {"yellow", 0xFFFF00}, {"navy", 0x000080},   {"blue", 0x0000FF},   {"teal", 0x008080},
        {"aqua", 0x00FFFF},   {"orange", 0xFFA500}, {"brown", 0xA52A2A},  {"pink", 0xFFC0CB},
    };
    while (*text == ' ') {
        text++;
    }
    uint32_t rgb = 0;
    bool found = false;
    for (const auto &named : kNames) {
        if (strcasecmp(named.name, text) == 0) {
            rgb = named.rgb;
            found = true;
        }
    }
    if (!found) {
        if (*text == '#') {
            text++;
        }
        size_t digits = 0;
        while (isxdigit(static_cast<unsigned char>(text[digits]))) {
            digits++;
        }
        if (text[digits] != 0 && text[digits] != ' ') {
            return false;
        }
        char hex[7];
        if (digits == 6) {
            memcpy(hex, text, 6);
        } else if (digits == 3) {
            for (int i = 0; i < 3; i++) {
                hex[2 * i] = hex[2 * i + 1] = text[i];
            }
        } else {
            return false;
        }
        hex[6] = 0;
        rgb = static_cast<uint32_t>(strtoul(hex, nullptr, 16));
    }
    out = {static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb)};
    return true;
}

/* ---- Reading a page ---------------------------------------------------------------- */

// Reads the HTML once. With `fill` false it only counts what the page
// needs; with it true it writes into the document's memory. Both passes
// take exactly the same steps.
class Reader {
public:
    Reader(Document &doc, const char *begin, const char *end, bool fill) : doc_(doc), p_(begin), end_(end), fill_(fill)
    {
        look_.colour = doc.text_colour_;
        keep("", 0); // text 0 is the empty text
    }

    void read_html();
    void read_plain();

    int items = 0, links = 0, fields = 0, options = 0, forms = 0, pictures = 0, typed = 0;
    size_t pool = 0;

private:
    struct Look {
        uint8_t style = 0;
        uint8_t size = 3; // as <font size>: 1 to 7
        Align align = Align::Left;
        uint8_t indent = 0;
        gfx::Colour colour = gfx::kBlack;
        uint16_t link = 0;
    };
    struct Open {
        char name[11];
        Look look;
        bool ordered;
        uint16_t counter;
    };
    enum class Into : uint8_t { Page, Title, Option, TextArea, Nowhere };

    static constexpr int kMaxOpen = 40;
    static constexpr int kChunk = 20;       // a long word is cut every so many characters, so it can wrap
    static constexpr int kPreChunk = 120;
    static constexpr int kListIndent = 4;   // in kIndentUnit

    /* What the page is made of. */
    uint32_t keep(const char *text, size_t length)
    {
        const uint32_t at = static_cast<uint32_t>(pool);
        if (fill_) {
            memcpy(doc_.pool_ + pool, text, length);
            doc_.pool_[pool + length] = 0;
        }
        pool += length + 1;
        return at;
    }
    Item &emit(Kind kind)
    {
        Item &item = fill_ ? doc_.items_[items] : scratch_;
        items++;
        item = Item{};
        item.kind = kind;
        item.style = look_.style;
        item.scale = look_.size >= 7 ? 3 : look_.size >= 5 ? 2 : 1;
        item.align = look_.align;
        item.indent = look_.indent;
        item.colour = look_.colour;
        item.link = look_.link;
        last_ = kind;
        return item;
    }
    void flush_word()
    {
        if (word_length_ == 0) {
            return;
        }
        Item &item = emit(Kind::Word);
        if (space_) {
            item.style |= kSpace;
        }
        item.text = keep(word_, word_length_);
        word_length_ = 0;
        word_chars_ = 0;
        space_ = false;
    }
    // A block starts or ends: the line ends and `gap` pixels are left.
    void block(int gap)
    {
        flush_word();
        space_ = false;
        if (last_ == Kind::Block) {
            if (gap > last_gap_) {
                last_gap_ = gap;
                if (fill_) {
                    doc_.items_[items - 1].extra = static_cast<uint16_t>(gap);
                }
            }
            return;
        }
        emit(Kind::Block).extra = static_cast<uint16_t>(gap);
        last_gap_ = gap;
    }
    void line_break()
    {
        flush_word();
        space_ = false;
        emit(Kind::Break);
    }

    /* Text. */
    void put(uint32_t code)
    {
        char bytes[8];
        switch (into_) {
        case Into::Nowhere: return;
        case Into::Title:
            append(doc_.title_, sizeof(doc_.title_), bytes, shown_as(code == '\n' || code == '\t' ? ' ' : code, bytes));
            return;
        case Into::Option:
        case Into::TextArea:
            append(buffer_, sizeof(buffer_), bytes, shown_as(code == '\n' || code == '\t' || code == '\r' ? ' ' : code, bytes));
            return;
        case Into::Page: break;
        }
        if (look_.style & kPre) {
            if (code == '\n') {
                line_break();
                return;
            }
            if (code == '\r') {
                return;
            }
            if (code == '\t') {
                do {
                    put(' ');
                } while (word_chars_ % 8 != 0);
                return;
            }
        } else if (code == ' ' || code == '\n' || code == '\t' || code == '\r' || code == '\f') {
            flush_word();
            space_ = true;
            return;
        }
        const size_t n = shown_as(code, bytes);
        if (n == 0) {
            return;
        }
        if (word_length_ + n > sizeof(word_) - 1 || word_chars_ >= ((look_.style & kPre) ? kPreChunk : kChunk)) {
            flush_word();
        }
        memcpy(word_ + word_length_, bytes, n);
        word_length_ += n;
        word_chars_ += count_chars_in(bytes, n);
    }
    static int count_chars_in(const char *bytes, size_t n)
    {
        int count = 0;
        for (size_t i = 0; i < n; i++) {
            if ((static_cast<unsigned char>(bytes[i]) & 0xC0) != 0x80) {
                count++;
            }
        }
        return count;
    }
    static void append(char *to, size_t capacity, const char *bytes, size_t n)
    {
        const size_t length = strlen(to);
        // Spaces run together, and none leads.
        if (n == 1 && bytes[0] == ' ' && (length == 0 || to[length - 1] == ' ')) {
            return;
        }
        if (length + n + 1 <= capacity) {
            memcpy(to + length, bytes, n);
            to[length + n] = 0;
        }
    }
    void words(const char *text)
    {
        for (; *text != 0; text++) {
            put(static_cast<unsigned char>(*text));
        }
    }

    /* Tags. */
    // The value of an attribute of the tag being read, with what it stands
    // for in place of "&amp;". `display`: only characters the font has.
    // False if the tag has no such attribute (out is then empty).
    bool attr(const char *name, char *out, size_t capacity, bool display = true) const
    {
        out[0] = 0;
        const size_t name_length = strlen(name);
        const char *p = attrs_;
        while (p < attrs_end_) {
            while (p < attrs_end_ && (isspace(static_cast<unsigned char>(*p)) || *p == '/')) {
                p++;
            }
            const char *key = p;
            while (p < attrs_end_ && !isspace(static_cast<unsigned char>(*p)) && *p != '=' && *p != '/') {
                p++;
            }
            const size_t key_length = static_cast<size_t>(p - key);
            while (p < attrs_end_ && isspace(static_cast<unsigned char>(*p))) {
                p++;
            }
            const char *value = p, *value_end = p;
            if (p < attrs_end_ && *p == '=') {
                p++;
                while (p < attrs_end_ && isspace(static_cast<unsigned char>(*p))) {
                    p++;
                }
                if (p < attrs_end_ && (*p == '"' || *p == '\'')) {
                    const char quote = *p++;
                    value = p;
                    while (p < attrs_end_ && *p != quote) {
                        p++;
                    }
                    value_end = p;
                    if (p < attrs_end_) {
                        p++;
                    }
                } else {
                    value = p;
                    while (p < attrs_end_ && !isspace(static_cast<unsigned char>(*p))) {
                        p++;
                    }
                    value_end = p;
                }
            }
            if (key_length == 0) {
                if (p < attrs_end_ && value_end == value) {
                    p++; // something odd: step over it
                }
                continue;
            }
            if (key_length == name_length && strncasecmp(key, name, name_length) == 0) {
                size_t length = 0;
                const char *q = value;
                while (q < value_end) {
                    char bytes[8];
                    const uint32_t code = next_code(q, value_end, true);
                    const size_t n = display ? shown_as(code, bytes) : as_utf8(code, bytes);
                    if (length + n + 1 > capacity) {
                        break;
                    }
                    memcpy(out + length, bytes, n);
                    length += n;
                }
                out[length] = 0;
                return true;
            }
        }
        return false;
    }
    bool has_attr(const char *name) const
    {
        char value[4];
        return attr(name, value, sizeof(value));
    }
    int number_attr(const char *name, int otherwise) const
    {
        char value[12];
        if (!attr(name, value, sizeof(value)) || !isdigit(static_cast<unsigned char>(value[0]))) {
            return otherwise;
        }
        return atoi(value);
    }

    void open(const char *name)
    {
        flush_word();
        if (depth_ < kMaxOpen) {
            Open &o = open_[depth_++];
            snprintf(o.name, sizeof(o.name), "%s", name);
            o.look = look_;
            o.ordered = false;
            o.counter = 0;
        }
    }
    // The innermost open tag of that name, -1 if none.
    int find_open(const char *name) const
    {
        for (int i = depth_ - 1; i >= 0; i--) {
            if (strcmp(open_[i].name, name) == 0) {
                return i;
            }
        }
        return -1;
    }
    bool close(const char *name)
    {
        flush_word();
        const int at = find_open(name);
        if (at < 0) {
            return false;
        }
        look_ = open_[at].look;
        depth_ = at;
        return true;
    }
    void align_from_attr()
    {
        char value[12];
        if (attr("align", value, sizeof(value))) {
            if (strcasecmp(value, "center") == 0 || strcasecmp(value, "middle") == 0) {
                look_.align = Align::Centre;
            } else if (strcasecmp(value, "right") == 0) {
                look_.align = Align::Right;
            } else if (strcasecmp(value, "left") == 0) {
                look_.align = Align::Left;
            }
        }
    }
    void indent_more()
    {
        if (look_.indent <= 60) {
            look_.indent = static_cast<uint8_t>(look_.indent + kListIndent);
        }
    }
    void colour_attr(const char *name, gfx::Colour &colour) const
    {
        char value[24];
        gfx::Rgb rgb;
        if (attr(name, value, sizeof(value)) && parse_colour(value, rgb)) {
            colour = gfx::nearest(rgb);
        }
    }

    void tag(const char *name, bool closing);
    void input();
    void end_option();
    void end_select();
    Field &new_field(FieldType type)
    {
        Field &field = fill_ ? doc_.fields_[fields] : scratch_field_;
        fields++;
        field = Field{};
        field.type = type;
        field.form = static_cast<uint16_t>(form_);
        return field;
    }
    void give_typed(Field &field, const char *initial)
    {
        if (fill_) {
            field.typed = doc_.typed_ + static_cast<size_t>(typed) * Document::kTypedBytes;
            snprintf(field.typed, Document::kTypedBytes, "%s", initial);
        }
        typed++;
    }
    void field_item(int number)
    {
        flush_word();
        Item &item = emit(Kind::Field);
        if (space_) {
            item.style |= kSpace;
        }
        space_ = false;
        item.extra = static_cast<uint16_t>(number);
        item.link = 0;
    }
    // Moves past the end tag of `name`, whatever lies before it.
    void skip_past(const char *name)
    {
        const size_t length = strlen(name);
        while (p_ < end_) {
            if (*p_ == '<' && p_ + 2 + length <= end_ && p_[1] == '/' && strncasecmp(p_ + 2, name, length) == 0) {
                while (p_ < end_ && *p_ != '>') {
                    p_++;
                }
                if (p_ < end_) {
                    p_++;
                }
                return;
            }
            p_++;
        }
    }

    Document &doc_;
    const char *p_;
    const char *end_;
    bool fill_;
    Item scratch_ = {};
    Field scratch_field_ = {};
    Option scratch_option_ = {};
    Picture scratch_picture_ = {};
    Look look_;
    Open open_[kMaxOpen];
    int depth_ = 0;
    Kind last_ = Kind::Block; // a block at the very start adds nothing
    int last_gap_ = 1000;
    Into into_ = Into::Page;
    char word_[kPreChunk * 2 + 8];
    size_t word_length_ = 0;
    int word_chars_ = 0;
    bool space_ = false;
    const char *attrs_ = nullptr;
    const char *attrs_end_ = nullptr;
    int form_ = 0;
    int select_ = -1;        // the field of the <select> being read
    bool in_option_ = false;
    bool option_has_value_ = false;
    char option_value_[Document::kTypedBytes];
    int textarea_ = -1;
    char buffer_[Document::kTypedBytes]; // an option's text, or a textarea's
    int cells_ = 0;          // cells so far in this table row
};

void Reader::read_plain()
{
    look_.style |= kPre;
    while (p_ < end_) {
        put(next_code(p_, end_, false));
    }
    flush_word();
}

void Reader::read_html()
{
    while (p_ < end_) {
        if (*p_ != '<') {
            put(next_code(p_, end_, true));
            continue;
        }
        if (p_ + 3 < end_ && p_[1] == '!' && p_[2] == '-' && p_[3] == '-') {
            // A comment, to its "-->"; one never closed runs to its '>'.
            const char *q = p_ + 4;
            const char *found = nullptr;
            for (; q + 2 < end_; q++) {
                if (q[0] == '-' && q[1] == '-' && q[2] == '>') {
                    found = q + 3;
                    break;
                }
            }
            if (found == nullptr) {
                q = p_;
                while (q < end_ && *q != '>') {
                    q++;
                }
                found = q < end_ ? q + 1 : end_;
            }
            p_ = found;
            continue;
        }
        const char *q = p_ + 1;
        const bool closing = q < end_ && *q == '/';
        if (closing) {
            q++;
        }
        if (q < end_ && (*q == '!' || *q == '?')) {
            while (p_ < end_ && *p_ != '>') {
                p_++;
            }
            if (p_ < end_) {
                p_++;
            }
            continue;
        }
        if (q >= end_ || !isalpha(static_cast<unsigned char>(*q))) {
            put('<'); // a bare '<' in the text
            p_++;
            continue;
        }
        char name[12];
        size_t n = 0;
        while (q < end_ && isalnum(static_cast<unsigned char>(*q))) {
            if (n < sizeof(name) - 1) {
                name[n++] = static_cast<char>(tolower(static_cast<unsigned char>(*q)));
            }
            q++;
        }
        name[n] = 0;
        attrs_ = q;
        // To the '>' that is not inside quotes.
        char quote = 0;
        while (q < end_ && (quote != 0 || *q != '>')) {
            if (quote != 0) {
                if (*q == quote) {
                    quote = 0;
                }
            } else if ((*q == '"' || *q == '\'') && q > attrs_ && q[-1] == '=') {
                quote = *q;
            }
            q++;
        }
        attrs_end_ = q;
        p_ = q < end_ ? q + 1 : end_;
        tag(name, closing);
    }
    end_select();
    flush_word();
}

void Reader::end_option()
{
    if (!in_option_) {
        return;
    }
    in_option_ = false;
    into_ = Into::Nowhere;
    size_t length = strlen(buffer_);
    while (length > 0 && buffer_[length - 1] == ' ') {
        buffer_[--length] = 0;
    }
    Option &option = fill_ ? doc_.options_[options] : scratch_option_;
    options++;
    const uint32_t text = keep(buffer_, length);
    const uint32_t value = option_has_value_ ? keep(option_value_, strlen(option_value_)) : text;
    if (fill_) {
        option.text = text;
        option.value = value;
        doc_.fields_[select_].option_count++;
    }
}

void Reader::end_select()
{
    if (select_ < 0) {
        return;
    }
    end_option();
    into_ = Into::Page;
    field_item(select_);
    select_ = -1;
}

void Reader::input()
{
    char type[12], name[64], value[Document::kTypedBytes];
    attr("type", type, sizeof(type));
    attr("name", name, sizeof(name));
    const bool has_value = attr("value", value, sizeof(value));
    FieldType kind;
    if (type[0] == 0 || strcasecmp(type, "text") == 0 || strcasecmp(type, "search") == 0 ||
        strcasecmp(type, "email") == 0 || strcasecmp(type, "url") == 0 || strcasecmp(type, "number") == 0 ||
        strcasecmp(type, "tel") == 0) {
        kind = FieldType::Text;
    } else if (strcasecmp(type, "password") == 0) {
        kind = FieldType::Password;
    } else if (strcasecmp(type, "checkbox") == 0) {
        kind = FieldType::CheckBox;
    } else if (strcasecmp(type, "radio") == 0) {
        kind = FieldType::Radio;
    } else if (strcasecmp(type, "submit") == 0 || strcasecmp(type, "image") == 0) {
        kind = FieldType::Submit;
    } else if (strcasecmp(type, "reset") == 0) {
        kind = FieldType::Reset;
    } else if (strcasecmp(type, "hidden") == 0) {
        kind = FieldType::Hidden;
    } else {
        return; // a file to send, a script's button: nothing to show
    }
    const int number = fields;
    Field &field = new_field(kind);
    const uint32_t name_at = keep(name, strlen(name));
    const char *label = value;
    if (!has_value) {
        label = kind == FieldType::Submit  ? "Submit"
                : kind == FieldType::Reset ? "Reset"
                : kind == FieldType::CheckBox || kind == FieldType::Radio ? "on" : "";
    }
    const uint32_t value_at = keep(label, strlen(label));
    int size = number_attr("size", 20);
    size = size < 2 ? 2 : size > 60 ? 60 : size;
    const bool checked = has_attr("checked");
    if (fill_) {
        field.name = name_at;
        field.value = value_at;
        field.size = static_cast<uint8_t>(size);
        field.checked = field.was_checked = checked;
    }
    if (kind == FieldType::Text || kind == FieldType::Password) {
        give_typed(field, label);
    }
    if (kind != FieldType::Hidden) {
        field_item(number);
    }
}

void Reader::tag(const char *name, bool closing)
{
    const auto is = [name](const char *other) { return strcmp(name, other) == 0; };

    /* Inside a select or a textarea only their own tags count. */
    if (textarea_ >= 0) {
        if (is("textarea") && closing) {
            size_t length = strlen(buffer_);
            while (length > 0 && buffer_[length - 1] == ' ') {
                buffer_[--length] = 0;
            }
            if (fill_) {
                snprintf(doc_.fields_[textarea_].typed, Document::kTypedBytes, "%s", buffer_);
            }
            const uint32_t at = keep(buffer_, length);
            if (fill_) {
                doc_.fields_[textarea_].value = at;
            }
            into_ = Into::Page;
            field_item(textarea_);
            textarea_ = -1;
        }
        return;
    }
    if (select_ >= 0) {
        if (is("option")) {
            end_option();
            if (!closing) {
                in_option_ = true;
                into_ = Into::Option;
                buffer_[0] = 0;
                option_has_value_ = attr("value", option_value_, sizeof(option_value_));
                if (has_attr("selected") && fill_) {
                    Field &field = doc_.fields_[select_];
                    field.chosen = field.was_chosen = static_cast<uint16_t>(options - field.first_option);
                }
            }
        } else if (is("select") && closing) {
            end_select();
        }
        return;
    }

    /* What is not shown. */
    if (is("script") || is("style")) {
        if (!closing) {
            skip_past(name);
        }
        return;
    }
    if (is("title")) {
        flush_word();
        into_ = closing ? Into::Page : Into::Title;
        return;
    }
    if (closing && (is("head") || is("html") || is("body"))) {
        into_ = Into::Page;
        return;
    }
    if (is("body")) {
        into_ = Into::Page;
        if (fill_) {
            colour_attr("bgcolor", doc_.background_);
            colour_attr("link", doc_.link_colour_);
            const gfx::Colour before = doc_.text_colour_;
            colour_attr("text", doc_.text_colour_);
            if (look_.colour == before) {
                look_.colour = doc_.text_colour_;
            }
        }
        return;
    }
    if (is("base")) {
        if (fill_) {
            attr("href", doc_.base_, sizeof(doc_.base_), false);
        }
        return;
    }

    /* Letters. */
    static const struct {
        const char *name;
        uint8_t style;
    } kLetters[] = {
        {"b", kBold},      {"strong", kBold}, {"i", kItalic},   {"em", kItalic},      {"cite", kItalic},
        {"var", kItalic},  {"dfn", kItalic},  {"u", kUnderline}, {"ins", kUnderline}, {"s", kStrike},
        {"strike", kStrike}, {"del", kStrike}, {"tt", 0},        {"code", 0},         {"kbd", 0},
        {"samp", 0},
    };
    for (const auto &letter : kLetters) {
        if (is(letter.name)) {
            if (closing) {
                close(name);
            } else {
                open(name);
                look_.style |= letter.style;
            }
            return;
        }
    }
    if (is("font")) {
        if (closing) {
            close(name);
            return;
        }
        open(name);
        char value[24];
        if (attr("size", value, sizeof(value)) && value[0] != 0) {
            int size = atoi(value[0] == '+' ? value + 1 : value);
            if (value[0] == '+' || value[0] == '-') {
                size += 3;
            }
            look_.size = static_cast<uint8_t>(size < 1 ? 1 : size > 7 ? 7 : size);
        }
        if (look_.link == 0 || has_attr("color")) {
            colour_attr("color", look_.colour);
        }
        return;
    }
    if (is("big") || is("small")) {
        if (closing) {
            close(name);
            return;
        }
        open(name);
        if (is("big") && look_.size < 7) {
            look_.size++;
        } else if (is("small") && look_.size > 1) {
            look_.size--;
        }
        return;
    }
    if (is("a")) {
        close("a"); // links do not nest
        if (closing) {
            return;
        }
        char href[kMaxUrl];
        if (attr("href", href, sizeof(href), false)) {
            open(name);
            const uint32_t at = keep(href, strlen(href));
            if (fill_) {
                doc_.links_[links] = at;
            }
            links++;
            look_.link = static_cast<uint16_t>(links);
            look_.style |= kUnderline;
            look_.colour = doc_.link_colour_;
        }
        return;
    }

    /* Blocks. */
    if (is("br")) {
        line_break();
        return;
    }
    if (is("p")) {
        close("p");
        block(10);
        if (!closing) {
            open(name);
            align_from_attr();
        }
        return;
    }
    if (is("div")) {
        if (closing) {
            close(name);
            block(0);
        } else {
            block(0);
            open(name);
            align_from_attr();
        }
        return;
    }
    if (is("center")) {
        if (closing) {
            close(name);
            block(0);
        } else {
            block(0);
            open(name);
            look_.align = Align::Centre;
        }
        return;
    }
    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == 0) {
        if (closing) {
            close(name);
            block(8);
        } else {
            block(name[1] <= '2' ? 14 : 10);
            open(name);
            look_.style |= kBold;
            // A size the page asked for with <font> outside the heading stays.
            const uint8_t size = name[1] == '1' ? 6 : name[1] == '2' ? 5 : 3;
            if (size > look_.size) {
                look_.size = size;
            }
            align_from_attr();
        }
        return;
    }
    if (is("hr")) {
        block(4);
        emit(Kind::Rule);
        block(4);
        return;
    }
    if (is("ul") || is("ol") || is("menu") || is("dir") || is("dl")) {
        const char *list = is("ol") ? "ol" : is("dl") ? "dl" : "ul";
        if (closing) {
            const bool nested = close(list) && (find_open("ul") >= 0 || find_open("ol") >= 0);
            block(nested ? 0 : 8);
        } else {
            block(find_open("ul") >= 0 || find_open("ol") >= 0 ? 0 : 8);
            open(list);
            open_[depth_ > 0 ? depth_ - 1 : 0].ordered = is("ol");
            const int start = number_attr("start", 1);
            open_[depth_ > 0 ? depth_ - 1 : 0].counter = static_cast<uint16_t>(start > 0 ? start - 1 : 0);
            if (!is("dl")) {
                indent_more();
            }
        }
        return;
    }
    if (is("li")) {
        if (closing) {
            return;
        }
        block(0);
        int number = 0;
        for (int i = depth_ - 1; i >= 0; i--) {
            if (strcmp(open_[i].name, "ul") == 0 || strcmp(open_[i].name, "ol") == 0) {
                // Whatever the entry left open is closed with it.
                flush_word();
                Look look = i + 1 < depth_ ? open_[i + 1].look : look_;
                look_ = look;
                depth_ = i + 1;
                if (open_[i].ordered) {
                    number = ++open_[i].counter;
                }
                break;
            }
        }
        Item &item = emit(Kind::Marker);
        item.extra = static_cast<uint16_t>(number);
        item.link = 0;
        item.style = 0;
        item.scale = 1;
        return;
    }
    if (is("dt") || is("dd")) {
        if (closing) {
            return;
        }
        close("dd");
        block(0);
        if (is("dd")) {
            open(name);
            indent_more();
        }
        return;
    }
    if (is("blockquote")) {
        if (closing) {
            close(name);
            block(10);
        } else {
            block(10);
            open(name);
            indent_more();
        }
        return;
    }
    if (is("pre") || is("xmp") || is("listing")) {
        if (closing) {
            close("pre");
            block(10);
        } else {
            block(10);
            open("pre");
            look_.style |= kPre;
            // The line end right after <pre> is not a line.
            if (p_ < end_ && *p_ == '\r') {
                p_++;
            }
            if (p_ < end_ && *p_ == '\n') {
                p_++;
            }
        }
        return;
    }
    if (is("address")) {
        if (closing) {
            close(name);
            block(0);
        } else {
            block(0);
            open(name);
            look_.style |= kItalic;
        }
        return;
    }

    /* Tables, as rows of text. */
    if (is("table")) {
        block(6);
        cells_ = 0;
        return;
    }
    if (is("tr") || is("caption")) {
        close("th");
        block(0);
        cells_ = 0;
        return;
    }
    if (is("td") || is("th")) {
        close("th");
        if (closing) {
            return;
        }
        flush_word();
        if (cells_++ > 0) {
            space_ = true;
            words("\xA0\xA0");
            flush_word();
            space_ = true;
        }
        if (is("th")) {
            open(name);
            look_.style |= kBold;
        }
        return;
    }

    /* Frames: a link to each. */
    if ((is("frame") || is("iframe")) && !closing) {
        char source[kMaxUrl], title[64];
        if (attr("src", source, sizeof(source), false)) {
            block(0);
            words("Frame: ");
            open("a");
            const uint32_t at = keep(source, strlen(source));
            if (fill_) {
                doc_.links_[links] = at;
            }
            links++;
            look_.link = static_cast<uint16_t>(links);
            look_.style |= kUnderline;
            look_.colour = doc_.link_colour_;
            if (!attr("name", title, sizeof(title)) || title[0] == 0) {
                snprintf(title, sizeof(title), "%.60s", source);
            }
            for (const char *t = title; *t != 0;) {
                put(next_code(t, title + strlen(title), false));
            }
            close("a");
            block(0);
        }
        return;
    }

    /* Pictures. */
    if (is("img")) {
        char source[kMaxUrl], alt[120];
        attr("src", source, sizeof(source), false);
        attr("alt", alt, sizeof(alt));
        const int width = number_attr("width", 0), height = number_attr("height", 0);
        if ((width > 0 && width <= 2) || (height > 0 && height <= 2)) {
            return; // a spacer
        }
        flush_word();
        Picture &picture = fill_ ? doc_.pictures_[pictures] : scratch_picture_;
        const uint32_t source_at = keep(source, strlen(source));
        const uint32_t alt_at = keep(alt, strlen(alt));
        if (fill_) {
            picture.source = source_at;
            picture.alt = alt_at;
            picture.width = static_cast<uint16_t>(width > 2000 ? 2000 : width);
            picture.height = static_cast<uint16_t>(height > 2000 ? 2000 : height);
        }
        Item &item = emit(Kind::Image);
        if (space_) {
            item.style |= kSpace;
        }
        space_ = false;
        item.extra = static_cast<uint16_t>(pictures);
        pictures++;
        return;
    }

    /* Forms. */
    if (is("form")) {
        block(4);
        if (closing) {
            form_ = 0;
            return;
        }
        char action[kMaxUrl], method[8];
        attr("action", action, sizeof(action), false);
        attr("method", method, sizeof(method));
        const uint32_t at = keep(action, strlen(action));
        if (fill_) {
            doc_.forms_[forms].action = at;
            doc_.forms_[forms].post = strcasecmp(method, "post") == 0;
        }
        forms++;
        form_ = forms;
        return;
    }
    if (is("input") && !closing) {
        input();
        return;
    }
    if (is("select") && !closing) {
        flush_word();
        char field_name[64];
        attr("name", field_name, sizeof(field_name));
        select_ = fields;
        Field &field = new_field(FieldType::Select);
        const uint32_t at = keep(field_name, strlen(field_name));
        if (fill_) {
            field.name = at;
            field.first_option = static_cast<uint16_t>(options);
        }
        into_ = Into::Nowhere;
        return;
    }
    if (is("textarea") && !closing) {
        flush_word();
        char field_name[64];
        attr("name", field_name, sizeof(field_name));
        int size = number_attr("cols", 30);
        size = size < 2 ? 2 : size > 60 ? 60 : size;
        textarea_ = fields;
        Field &field = new_field(FieldType::Text);
        const uint32_t at = keep(field_name, strlen(field_name));
        if (fill_) {
            field.name = at;
            field.size = static_cast<uint8_t>(size);
        }
        give_typed(field, "");
        buffer_[0] = 0;
        into_ = Into::TextArea;
        return;
    }
    // Anything else is a tag this browser does not know: what it holds is
    // shown as plain text.
}

/* ---- The document ------------------------------------------------------------------ */

void Document::clear()
{
    if (arena_ != nullptr) {
        memory_.release(arena_);
    }
    arena_ = nullptr;
    arena_bytes_ = 0;
    items_ = nullptr;
    pool_ = nullptr;
    links_ = nullptr;
    fields_ = nullptr;
    options_ = nullptr;
    forms_ = nullptr;
    pictures_ = nullptr;
    typed_ = nullptr;
    item_count_ = link_count_ = field_count_ = option_count_ = form_count_ = picture_count_ = 0;
    title_[0] = 0;
    base_[0] = 0;
    background_ = gfx::kWhite;
    text_colour_ = gfx::kBlack;
    link_colour_ = gfx::rgb(0, 0, 3);
    width_ = height_ = 0;
}

bool Document::load(const uint8_t *bytes, size_t size, bool plain)
{
    clear();
    const char *begin = reinterpret_cast<const char *>(bytes);
    // A byte-order mark is not text.
    if (size >= 3 && memcmp(begin, "\xEF\xBB\xBF", 3) == 0) {
        begin += 3;
        size -= 3;
    }
    // The reader is some 2 KB, too much for the stack of a program's task
    // twice over: it lives in the Memory port's memory, one at a time, and
    // only its counts are kept.
    struct Counts {
        int items, links, fields, options, forms, pictures, typed;
        size_t pool;
    };
    const auto read = [&](bool fill, Counts &out) {
        void *room = memory_.allocate(sizeof(Reader));
        if (room == nullptr) {
            return false;
        }
        Reader *reader = new (room) Reader(*this, begin, begin + size, fill);
        if (plain) {
            reader->read_plain();
        } else {
            reader->read_html();
        }
        out = {reader->items, reader->links,  reader->fields, reader->options,
               reader->forms, reader->pictures, reader->typed,  reader->pool};
        reader->~Reader();
        memory_.release(room);
        return true;
    };
    Counts count = {};
    if (!read(false, count)) {
        return false;
    }
    title_[0] = 0;

    const size_t fields_bytes = round8(static_cast<size_t>(count.fields) * sizeof(Field));
    const size_t items_bytes = round4(static_cast<size_t>(count.items) * sizeof(Item));
    const size_t links_bytes = static_cast<size_t>(count.links) * sizeof(uint32_t);
    const size_t options_bytes = round4(static_cast<size_t>(count.options) * sizeof(Option));
    const size_t forms_bytes = round4(static_cast<size_t>(count.forms) * sizeof(Form));
    const size_t pictures_bytes = round4(static_cast<size_t>(count.pictures) * sizeof(Picture));
    const size_t typed_bytes = static_cast<size_t>(count.typed) * kTypedBytes;
    const size_t total =
        fields_bytes + items_bytes + links_bytes + options_bytes + forms_bytes + pictures_bytes + typed_bytes + count.pool;
    arena_ = memory_.allocate(total);
    if (arena_ == nullptr) {
        return false;
    }
    arena_bytes_ = total;
    memset(arena_, 0, total);
    char *at = static_cast<char *>(arena_);
    fields_ = reinterpret_cast<Field *>(at);
    at += fields_bytes;
    items_ = reinterpret_cast<Item *>(at);
    at += items_bytes;
    links_ = reinterpret_cast<uint32_t *>(at);
    at += links_bytes;
    options_ = reinterpret_cast<Option *>(at);
    at += options_bytes;
    forms_ = reinterpret_cast<Form *>(at);
    at += forms_bytes;
    pictures_ = reinterpret_cast<Picture *>(at);
    at += pictures_bytes;
    typed_ = at;
    at += typed_bytes;
    pool_ = at;

    Counts fill = {};
    if (!read(true, fill)) {
        clear();
        return false;
    }
    item_count_ = fill.items;
    link_count_ = fill.links;
    field_count_ = fill.fields;
    option_count_ = fill.options;
    form_count_ = fill.forms;
    picture_count_ = fill.pictures;
    // The title without the space a line end left at its end.
    size_t length = strlen(title_);
    while (length > 0 && title_[length - 1] == ' ') {
        title_[--length] = 0;
    }
    return true;
}

const char *Document::shown(const Field &field) const
{
    switch (field.type) {
    case FieldType::Text:
    case FieldType::Password: return field.typed != nullptr ? field.typed : "";
    case FieldType::Select:
        return field.option_count > 0 ? pool_ + options_[field.first_option + field.chosen].text : "";
    default: return pool_ + field.value;
    }
}

void Document::choose_radio(int number)
{
    const Field &chosen = fields_[number];
    for (int i = 0; i < field_count_; i++) {
        Field &other = fields_[i];
        if (other.type == FieldType::Radio && other.form == chosen.form &&
            strcmp(pool_ + other.name, pool_ + chosen.name) == 0) {
            other.checked = i == number;
        }
    }
}

void Document::reset(int form)
{
    for (int i = 0; i < field_count_; i++) {
        Field &field = fields_[i];
        if (field.form != form) {
            continue;
        }
        field.checked = field.was_checked;
        field.chosen = field.was_chosen;
        if (field.typed != nullptr) {
            snprintf(field.typed, kTypedBytes, "%s", pool_ + field.value);
        }
    }
}

bool Document::submit(int pressed, const char *page_url, char *out, size_t capacity, bool &posts) const
{
    posts = false;
    const Field &from = fields_[pressed];
    const Form *f = form(from.form);
    if (f == nullptr) {
        return false;
    }
    if (f->post) {
        posts = true;
        return false;
    }
    const char *against = base_[0] != 0 ? base_ : page_url;
    char action[kMaxUrl];
    if (!resolve(against, pool_ + f->action, action, sizeof(action))) {
        return false;
    }
    // What the form sends replaces any query the action had.
    action[strcspn(action, "?")] = 0;
    size_t length = strlen(action);
    if (length + 2 > capacity) {
        return false;
    }
    memcpy(out, action, length + 1);
    bool first = true;
    for (int i = 0; i < field_count_; i++) {
        const Field &field = fields_[i];
        if (field.form != from.form || pool_[field.name] == 0) {
            continue;
        }
        const char *value = nullptr;
        switch (field.type) {
        case FieldType::Text:
        case FieldType::Password: value = field.typed; break;
        case FieldType::Hidden: value = pool_ + field.value; break;
        case FieldType::CheckBox:
        case FieldType::Radio: value = field.checked ? pool_ + field.value : nullptr; break;
        case FieldType::Submit: value = i == pressed ? pool_ + field.value : nullptr; break;
        case FieldType::Select:
            value = field.option_count > 0 ? pool_ + options_[field.first_option + field.chosen].value : nullptr;
            break;
        case FieldType::Reset: break;
        }
        if (value == nullptr) {
            continue;
        }
        if (length + 2 > capacity) {
            return false;
        }
        out[length++] = first ? '?' : '&';
        out[length] = 0;
        first = false;
        length = append_escaped(out, length, capacity, pool_ + field.name);
        if (length + 2 > capacity) {
            return false;
        }
        out[length++] = '=';
        out[length] = 0;
        length = append_escaped(out, length, capacity, value);
        if (length >= capacity) {
            return false;
        }
    }
    return true;
}

/* ---- Layout ------------------------------------------------------------------------ */

void Document::layout(int width)
{
    width_ = width;
    const int right_edge = width - kMargin;
    int y = kMargin;
    int gap = 0;
    bool top = true;

    const auto measure = [this, right_edge](Item &item) {
        switch (item.kind) {
        case Kind::Word:
            item.w = static_cast<uint16_t>(count_chars(pool_ + item.text) * (kCharWidth + ((item.style & kBold) ? 1 : 0)) *
                                           item.scale);
            item.h = static_cast<uint16_t>(kCharHeight * item.scale);
            break;
        case Kind::Marker:
            {
                int digits = 1;
                for (int n = item.extra; n >= 10; n /= 10) {
                    digits++;
                }
                item.w = static_cast<uint16_t>(item.extra == 0 ? kCharWidth : (digits + 1) * kCharWidth);
                item.h = kCharHeight;
            }
            break;
        case Kind::Field:
            {
                const Field &field = fields_[item.extra];
                switch (field.type) {
                case FieldType::Text:
                case FieldType::Password:
                    item.w = static_cast<uint16_t>(field.size * kCharWidth + 8);
                    item.h = 18;
                    break;
                case FieldType::CheckBox:
                case FieldType::Radio:
                    item.w = 13;
                    item.h = 13;
                    break;
                case FieldType::Select:
                    {
                        int longest = 4;
                        for (int i = 0; i < field.option_count; i++) {
                            const int length = count_chars(pool_ + options_[field.first_option + i].text);
                            longest = length > longest ? length : longest;
                        }
                        item.w = static_cast<uint16_t>(longest * kCharWidth + 8 + 16);
                        item.h = 18;
                    }
                    break;
                default:
                    item.w = static_cast<uint16_t>(count_chars(pool_ + field.value) * kCharWidth + 20);
                    item.h = 20;
                    break;
                }
            }
            break;
        case Kind::Image:
            {
                const Picture &picture = pictures_[item.extra];
                int w = picture.width, h = picture.height;
                if (w == 0 || h == 0) {
                    w = count_chars(pool_ + picture.alt) * kCharWidth + 8;
                    h = kCharHeight + 4;
                    if (pool_[picture.alt] == 0) {
                        w = h = 16;
                    }
                }
                const int most = right_edge - kMargin;
                item.w = static_cast<uint16_t>(w > most && most > 0 ? most : w);
                item.h = static_cast<uint16_t>(h);
            }
            break;
        default:
            item.w = item.h = 0;
            break;
        }
    };

    int i = 0;
    while (i < item_count_) {
        Item &first = items_[i];
        if (first.kind == Kind::Block) {
            gap = first.extra > gap ? first.extra : gap;
            first.y = y;
            first.x = 0;
            first.w = first.h = first.line = 0;
            i++;
            continue;
        }
        if (!top) {
            y += gap;
        }
        gap = 0;
        top = false;
        int left = kMargin + first.indent * kIndentUnit;
        if (left > width / 2) {
            left = width / 2;
        }
        if (first.kind == Kind::Break) {
            // A line with nothing on it.
            first.y = y;
            first.x = static_cast<int16_t>(left);
            first.w = first.h = 0;
            first.line = kCharHeight + 2;
            y += first.line;
            i++;
            continue;
        }
        if (first.kind == Kind::Rule) {
            first.y = y;
            first.x = static_cast<int16_t>(left);
            first.w = static_cast<uint16_t>(right_edge > left ? right_edge - left : 0);
            first.h = 2;
            first.line = 8;
            y += first.line;
            i++;
            continue;
        }

        // A line of words, fields and pictures, from item i.
        const bool pre = (first.style & kPre) != 0;
        int x = left;
        int line = 0;
        bool any = false;  // something is on the line
        int content = -1;  // the first item on it that takes room
        int group = i;     // where the piece that cannot be split starts
        int j = i;
        while (j < item_count_) {
            Item &item = items_[j];
            if (item.kind == Kind::Block || item.kind == Kind::Rule) {
                break;
            }
            if (item.kind == Kind::Break) {
                item.x = static_cast<int16_t>(x);
                item.w = item.h = 0;
                j++;
                break;
            }
            measure(item);
            if (item.kind == Kind::Marker) {
                item.x = static_cast<int16_t>(left - item.w - 4);
                line = item.h > line ? item.h : line;
                j++;
                group = j;
                continue;
            }
            const int space = any && (item.style & kSpace) ? kCharWidth * item.scale : 0;
            if ((item.style & kSpace) || !any) {
                group = j;
            }
            if (any && !pre && x + space + item.w > right_edge) {
                // The line is full. What is glued to this item goes down with it.
                if (group > content) {
                    j = group;
                }
                break;
            }
            if (!any) {
                content = j;
            }
            item.x = static_cast<int16_t>(x + space);
            x = item.x + item.w;
            any = true;
            j++;
        }
        // The line is items i to j - 1.
        line = 0;
        for (int k = i; k < j; k++) {
            line = items_[k].h > line ? items_[k].h : line;
        }
        if (line < kCharHeight) {
            line = kCharHeight;
        }
        line += 2;
        int shift = 0;
        int used = left;
        for (int k = i; k < j; k++) {
            if (items_[k].kind != Kind::Marker && items_[k].x + items_[k].w > used) {
                used = items_[k].x + items_[k].w;
            }
        }
        const int room = right_edge - used;
        // The first thing on the line that takes room says how it is aligned.
        const Align align = content >= 0 && content < j ? items_[content].align : first.align;
        if (room > 0) {
            shift = align == Align::Centre ? room / 2 : align == Align::Right ? room : 0;
        }
        for (int k = i; k < j; k++) {
            items_[k].y = y;
            items_[k].line = static_cast<uint16_t>(line);
            items_[k].x = static_cast<int16_t>(items_[k].x + shift);
        }
        y += line;
        i = j;
    }
    height_ = y + kMargin;
}

int Document::first_at(int y) const
{
    int low = 0, high = item_count_;
    while (low < high) {
        const int middle = (low + high) / 2;
        if (items_[middle].y + items_[middle].line > y) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    return low;
}

int Document::item_at(int x, int y) const
{
    for (int i = first_at(y); i < item_count_ && items_[i].y <= y; i++) {
        const Item &item = items_[i];
        if (item.w == 0 || item.kind == Kind::Marker || item.kind == Kind::Rule) {
            continue;
        }
        // A link is one piece to click: the space before a word is its too.
        const int before = (item.style & kSpace) && item.link != 0 ? kCharWidth * item.scale : 0;
        if (x >= item.x - before && x < item.x + item.w && y >= item.y && y < item.y + item.line) {
            return i;
        }
    }
    return -1;
}

} // namespace www
