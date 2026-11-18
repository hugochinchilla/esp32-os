// A page of the web as the browser shows it (core/www): HTML of the years
// around HTML 3.2 read into a flat list of things to show, and that list
// laid out for a width. No scripts, no style sheets.
//
// There is no tree. A page is read from start to end into items (a word, a
// line break, a rule, a list marker, a form field, a picture's place), each
// carrying how it looks (bold, size, colour, the link it belongs to, how
// far in it sits and how its line is aligned). layout() then gives every
// item its place for a width; the browser paints the items in view. Reading
// is forgiving: tags never closed, closed twice or unknown do no harm.
//
// The page is read twice: once to count, once to fill memory asked for in
// one piece from the Memory port (external RAM on the board), so a page
// costs what it holds and nothing is reallocated. The HTML itself is not
// kept. Text is kept as UTF-8 holding only what the system's font has
// (Latin-1); other characters become their nearest plain form or '?'.
//
// What it understands: headings, p, br, hr, div, center, b strong, i em
// cite, u, s strike, tt code, font size and color, big, small, a, ul ol li,
// dl dt dd, blockquote, pre, address, img (its place and its alt text),
// form, input (text, password, checkbox, radio, submit, reset, hidden),
// select, textarea (one line), body colours, base, title, frames (as links
// to them), and tables as rows of text.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/gfx/canvas.hpp"
#include "core/ports.hpp"

namespace www {

enum class Kind : uint8_t {
    Word,   // a piece of text with no space in it (a whole line inside <pre>)
    Break,  // <br>: the line ends
    Block,  // a paragraph or other block starts or ends: the line ends, `extra` pixels are left
    Rule,   // <hr>
    Marker, // in front of a list entry: `extra` is its number, 0 for a bullet
    Field,  // a form field: `extra` is its number
    Image,  // a picture's place: `extra` is its number
};

enum Style : uint8_t {
    kBold = 1,
    kItalic = 2,
    kUnderline = 4,
    kStrike = 8,
    kSpace = 16, // a space comes before it
    kPre = 32,   // inside <pre>: never wrapped
};

enum class Align : uint8_t { Left, Centre, Right };

// The system's font is 6 by 12; a size is that so many times over.
constexpr int kCharWidth = 6, kCharHeight = 12;
constexpr int kIndentUnit = 6; // pixels per unit of Item::indent

struct Item {
    int32_t y;  // the top of its line; set by layout()
    int16_t x;
    uint16_t w;
    uint16_t h;
    uint16_t line; // the height of its line
    Kind kind;
    uint8_t style; // Style bits
    uint8_t scale; // 1 to 3: how many times the font's size
    Align align;
    uint8_t indent; // how far in its line starts, in kIndentUnit
    gfx::Colour colour;
    uint16_t link;   // 0: none; otherwise the link's number
    uint16_t extra;
    uint32_t text;   // where its text is kept
};

enum class FieldType : uint8_t { Text, Password, CheckBox, Radio, Submit, Reset, Hidden, Select };

struct Field {
    FieldType type;
    bool checked;
    bool was_checked; // as the page had it, for Reset
    uint8_t size;     // of a text field, in characters
    uint16_t form;    // 0: none
    uint32_t name;
    uint32_t value;   // as the page had it; a button's label
    char *typed;      // what a text field holds now (kTypedBytes), nullptr for the others
    uint16_t first_option, option_count, chosen, was_chosen; // a Select's
};

struct Option {
    uint32_t text, value;
};

struct Form {
    uint32_t action;
    bool post;
};

struct Picture {
    uint32_t source, alt;
    uint16_t width, height; // as the page gives them; 0 if it does not
    uint16_t shown_w, shown_h; // of the picture once it has been fetched; 0 until then
};

class Document {
public:
    static constexpr size_t kTypedBytes = 200;
    static constexpr int kMargin = 8; // around the page

    explicit Document(ports::Memory &memory) : memory_(memory) {}
    ~Document() { clear(); }
    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;

    // Reads a page. `plain`: it is plain text, shown as it is. False: there
    // was no memory for it, and the document is empty.
    bool load(const uint8_t *bytes, size_t size, bool plain = false);
    void clear();

    // Places every item for a page `width` pixels wide.
    void layout(int width);
    int width() const { return width_; }
    int height() const { return height_; }

    const char *title() const { return title_; }
    const char *base() const { return base_; } // <base href>, "" if none
    gfx::Colour background() const { return background_; }
    gfx::Colour text_colour() const { return text_colour_; }
    gfx::Colour link_colour() const { return link_colour_; }

    int item_count() const { return item_count_; }
    const Item &item(int index) const { return items_[index]; }
    const char *text(const Item &item) const { return pool_ + item.text; }
    const char *text(uint32_t where) const { return pool_ + where; }
    // The first item whose line reaches `y` or below; item_count() if none.
    int first_at(int y) const;
    // The item at a point of the page, -1 if none.
    int item_at(int x, int y) const;
    // The address a link points to, as the page wrote it.
    const char *link(int number) const { return pool_ + links_[number - 1]; }
    int link_count() const { return link_count_; }

    int field_count() const { return field_count_; }
    Field &field(int number) { return fields_[number]; }
    const Field &field(int number) const { return fields_[number]; }
    const Option &option(int number) const { return options_[number]; }
    const Picture &picture(int number) const { return pictures_[number]; }
    int picture_count() const { return picture_count_; }
    // The picture has come: its place takes this size at the next layout().
    void set_picture_size(int number, int width, int height);
    const Form *form(int number) const { return number >= 1 && number <= form_count_ ? &forms_[number - 1] : nullptr; }
    // What a field shows: what was typed, a button's label, a select's choice.
    const char *shown(const Field &field) const;
    // A radio button chosen: the others of its name in its form let go.
    void choose_radio(int number);
    // Every field of the form as the page had it.
    void reset(int form);
    // The address a form asks for when sent with field `pressed` (a submit
    // button, or a text field Enter was pressed in), against the page's
    // address. False: it does not fit, or the form posts (not done yet;
    // `posts` says so).
    bool submit(int pressed, const char *page_url, char *out, size_t capacity, bool &posts) const;

    // The memory the page holds.
    size_t bytes() const { return arena_bytes_; }

private:
    friend class Reader;

    ports::Memory &memory_;
    void *arena_ = nullptr;
    size_t arena_bytes_ = 0;
    Item *items_ = nullptr;
    char *pool_ = nullptr;
    uint32_t *links_ = nullptr;
    Field *fields_ = nullptr;
    Option *options_ = nullptr;
    Form *forms_ = nullptr;
    Picture *pictures_ = nullptr;
    char *typed_ = nullptr; // what the text fields hold
    int item_count_ = 0, link_count_ = 0, field_count_ = 0, option_count_ = 0, form_count_ = 0, picture_count_ = 0;
    char title_[96] = "";
    char base_[256] = "";
    gfx::Colour background_ = gfx::kWhite;
    gfx::Colour text_colour_ = gfx::kBlack;
    gfx::Colour link_colour_ = gfx::rgb(0, 0, 3);
    int width_ = 0, height_ = 0;
};

// A colour as pages write them ("#008000", "#080", "green"). False if it is
// neither.
bool parse_colour(const char *text, gfx::Rgb &out);

} // namespace www
