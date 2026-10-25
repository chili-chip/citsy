#include "src/font/font.hpp"
#include "src/font/builtin_fonts.hpp"

#include <cmath>
#include <algorithm>
#include <span>

namespace citsy {
namespace {

void make_missing(BitsyFont& font) {
    font.missing.width = font.width;
    font.missing.height = font.height;
    font.missing.spacing = font.width;
    font.missing.data.assign(
        static_cast<std::size_t>(font.width * font.height), 0);
    for (int y = 0; y < font.height - 1; ++y) {
        for (int x = 0; x < font.width - 1; ++x) {
            font.missing.data[static_cast<std::size_t>(y * font.width + x)] = 1;
        }
    }
}

std::string_view trim_line(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

int to_int(std::string_view s) {
    int n = 0;
    bool neg = false;
    if (!s.empty() && s.front() == '-') { neg = true; s.remove_prefix(1); }
    for (char c : s) {
        if (c < '0' || c > '9') break;
        n = n * 10 + (c - '0');
    }
    return neg ? -n : n;
}

} // namespace

char32_t next_codepoint(std::string_view& s) {
    if (s.empty()) return 0;
    const auto b0 = static_cast<unsigned char>(s[0]);
    if (b0 < 0x80) {
        s.remove_prefix(1);
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && s.size() >= 2) {
        char32_t cp = ((b0 & 0x1F) << 6) |
                      (static_cast<unsigned char>(s[1]) & 0x3F);
        s.remove_prefix(2);
        return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && s.size() >= 3) {
        char32_t cp = ((b0 & 0x0F) << 12) |
                      ((static_cast<unsigned char>(s[1]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(s[2]) & 0x3F);
        s.remove_prefix(3);
        return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && s.size() >= 4) {
        char32_t cp = ((b0 & 0x07) << 18) |
                      ((static_cast<unsigned char>(s[1]) & 0x3F) << 12) |
                      ((static_cast<unsigned char>(s[2]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(s[3]) & 0x3F);
        s.remove_prefix(4);
        return cp;
    }
    s.remove_prefix(1);
    return b0;
}

BitsyFont default_font() {
    return *builtin_font("ascii_small");
}

BitsyFont parse_bitsyfont(std::string_view data) {
    if (data.empty()) return default_font();

    BitsyFont font;
    bool in_char = false;
    bool in_props = false;
    char32_t cur_cp = 0;
    int cur_row = 0;
    int char_h = 8;
    int char_w = 6;

    std::size_t line_start = 0;
    auto process = [&](std::string_view line) {
        line = trim_line(line);
        if (line.empty() && !in_char) return;
        if (!line.empty() && line[0] == '#') return;

        if (!in_char) {
            if (line.starts_with("FONT ")) {
                font.name = std::string(line.substr(5));
            } else if (line.starts_with("SIZE ")) {
                auto rest = line.substr(5);
                auto sp = rest.find(' ');
                if (sp != std::string_view::npos) {
                    font.width = to_int(rest.substr(0, sp));
                    font.height = to_int(rest.substr(sp + 1));
                }
            } else if (line.starts_with("CHAR ")) {
                in_char = true;
                in_props = true;
                cur_row = 0;
                cur_cp = static_cast<char32_t>(to_int(line.substr(5)));
                FontGlyph g;
                g.width = font.width;
                g.height = font.height;
                g.spacing = font.width;
                font.glyphs[cur_cp] = std::move(g);
                char_w = font.width;
                char_h = font.height;
            }
            return;
        }

        auto& g = font.glyphs[cur_cp];
        if (in_props) {
            if (line.starts_with("CHAR_SIZE ")) {
                auto rest = line.substr(10);
                auto sp = rest.find(' ');
                if (sp != std::string_view::npos) {
                    g.width = to_int(rest.substr(0, sp));
                    g.height = to_int(rest.substr(sp + 1));
                    g.spacing = g.width;
                    char_w = g.width;
                    char_h = g.height;
                }
                return;
            }
            if (line.starts_with("CHAR_OFFSET ")) {
                auto rest = line.substr(12);
                auto sp = rest.find(' ');
                if (sp != std::string_view::npos) {
                    g.offset_x = to_int(rest.substr(0, sp));
                    g.offset_y = to_int(rest.substr(sp + 1));
                }
                return;
            }
            if (line.starts_with("CHAR_SPACING ")) {
                g.spacing = to_int(line.substr(13));
                return;
            }
            in_props = false;
        }

        const int w = g.width;
        for (int x = 0; x < w; ++x) {
            std::uint8_t bit = 0;
            if (x < static_cast<int>(line.size()) && line[static_cast<std::size_t>(x)] == '1')
                bit = 1;
            g.data.push_back(bit);
        }
        ++cur_row;
        if (cur_row >= g.height) {
            in_char = false;
        }
        (void)char_h;
        (void)char_w;
    };

    while (line_start <= data.size()) {
        auto nl = data.find('\n', line_start);
        if (nl == std::string_view::npos) {
            process(data.substr(line_start));
            break;
        }
        process(data.substr(line_start, nl - line_start));
        line_start = nl + 1;
    }

    make_missing(font);
    return font;
}

std::optional<BitsyFont> builtin_font(std::string_view name) {
    for (const BuiltinFont& src : builtin_font_table()) {
        if (src.name != name) continue;
        BitsyFont font;
        font.name = std::string(src.name);
        font.width = src.width;
        font.height = src.height;
        for (const BuiltinGlyph& bg : src.glyphs) {
            FontGlyph g;
            g.width = bg.width;
            g.height = bg.height;
            g.offset_x = bg.offset_x;
            g.offset_y = bg.offset_y;
            g.spacing = bg.spacing;
            const std::size_t n = static_cast<std::size_t>(bg.width) * bg.height;
            g.data.resize(n);
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t bit = bg.bit_offset + i;
                g.data[i] = (src.bits[bit / 8] >> (7 - bit % 8)) & 1u;
            }
            font.glyphs[static_cast<char32_t>(bg.codepoint)] = std::move(g);
        }
        make_missing(font);
        return font;
    }
    return std::nullopt;
}

namespace {

// Bitsy dialog.js ArabicHandler.
constexpr char32_t kArabicFirst = 0x0621;
constexpr char32_t kArabicLast = 0x064E;

/// Isolated, final, initial and middle forms for U+0621..U+064E.
constexpr char32_t kArabicForms[kArabicLast - kArabicFirst + 1][4] = {
    {0xFE80, 0xFE80, 0xFE80, 0xFE80},  // hamza
    {0xFE81, 0xFE82, 0xFE81, 0xFE82},  // alef with madda above
    {0xFE83, 0xFE84, 0xFE83, 0xFE84},  // alef with hamza above
    {0xFE85, 0xFE86, 0xFE85, 0xFE86},  // waw with hamza above
    {0xFE87, 0xFE88, 0xFE87, 0xFE88},  // alef with hamza below
    {0xFE89, 0xFE8A, 0xFE8B, 0xFE8C},  // yeh with hamza above
    {0xFE8D, 0xFE8E, 0xFE8D, 0xFE8E},  // alef
    {0xFE8F, 0xFE90, 0xFE91, 0xFE92},  // beh
    {0xFE93, 0xFE94, 0xFE93, 0xFE94},  // teh marbuta
    {0xFE95, 0xFE96, 0xFE97, 0xFE98},  // teh
    {0xFE99, 0xFE9A, 0xFE9B, 0xFE9C},  // theh
    {0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0},  // jeem
    {0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4},  // hah
    {0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8},  // khah
    {0xFEA9, 0xFEAA, 0xFEA9, 0xFEAA},  // dal
    {0xFEAB, 0xFEAC, 0xFEAB, 0xFEAC},  // thal
    {0xFEAD, 0xFEAE, 0xFEAD, 0xFEAE},  // reh
    {0xFEAF, 0xFEB0, 0xFEAF, 0xFEB0},  // zain
    {0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4},  // seen
    {0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8},  // sheen
    {0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC},  // sad
    {0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0},  // dad
    {0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4},  // tah
    {0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8},  // zah
    {0xFEC9, 0xFECA, 0xFECB, 0xFECC},  // ain
    {0xFECD, 0xFECE, 0xFECF, 0xFED0},  // ghain
    {0, 0, 0, 0},                      // U+063B..U+063F are unassigned here
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0x0640, 0x0640, 0x0640, 0x0640},  // tatweel
    {0xFED1, 0xFED2, 0xFED3, 0xFED4},  // feh
    {0xFED5, 0xFED6, 0xFED7, 0xFED8},  // qaf
    {0xFED9, 0xFEDA, 0xFEDB, 0xFEDC},  // kaf
    {0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0},  // lam
    {0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4},  // meem
    {0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8},  // noon
    {0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC},  // heh
    {0xFEED, 0xFEEE, 0xFEED, 0xFEEE},  // waw
    {0xFEEF, 0xFEF0, 0xFBE8, 0xFBE9},  // alef maksura
    {0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4},  // yeh
    {0xFEF5, 0xFEF6, 0xFEF5, 0xFEF6},  // lam alef with madda (Bitsy's slot)
    {0xFEF7, 0xFEF8, 0xFEF7, 0xFEF8},  // lam alef with hamza above
    {0xFEF9, 0xFEFA, 0xFEF9, 0xFEFA},  // lam alef with hamza below
    {0xFEFB, 0xFEFC, 0xFEFB, 0xFEFC},  // lam alef
};

enum ArabicForm { kIsolated = 0, kFinal = 1, kInitial = 2, kMiddle = 3 };

bool is_arabic(char32_t c) { return c >= kArabicFirst && c <= kArabicLast; }

bool is_disconnected(char32_t c) {
    switch (c) {
    case 0x0621: case 0x0622: case 0x0623: case 0x0624: case 0x0625: case 0x0627:
    case 0x062F: case 0x0630: case 0x0631: case 0x0632: case 0x0648: case 0x0649:
    case 0x064B: case 0x064C: case 0x064D: case 0x064E:
        return true;
    default:
        return false;
    }
}

char32_t arabic_form(char32_t c, int form) {
    return kArabicForms[c - kArabicFirst][form];
}

void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

} // namespace

std::string shape_arabic(std::string_view utf8) {
    std::vector<char32_t> cps;
    bool any = false;
    for (std::string_view rest = utf8; !rest.empty();) {
        cps.push_back(next_codepoint(rest));
        any = any || is_arabic(cps.back());
    }
    if (!any) return std::string(utf8);

    std::string out;
    for (std::size_t i = 0; i < cps.size(); ++i) {
        const char32_t c = cps[i];
        if (!is_arabic(c)) {
            append_utf8(out, c);
            continue;
        }
        const bool to_prev = i > 0 && is_arabic(cps[i - 1]) && !is_disconnected(cps[i - 1]);
        const bool to_next = i + 1 < cps.size() && is_arabic(cps[i + 1]) && !is_disconnected(c);
        const int form = to_prev ? (to_next ? kMiddle : kFinal) : (to_next ? kInitial : kIsolated);

        if (c == 0x0644 && to_next) {
            char32_t ligature = 0;
            switch (cps[i + 1]) {
            case 0x0622: ligature = arabic_form(0x064B, form); break;
            case 0x0623: ligature = arabic_form(0x064C, form); break;
            case 0x0625: ligature = arabic_form(0x064D, form); break;
            case 0x0627: ligature = arabic_form(0x064E, form); break;
            default: break;
            }
            if (ligature) {
                append_utf8(out, ligature);
                ++i;
                continue;
            }
        }
        append_utf8(out, form == kIsolated ? c : arabic_form(c, form));
    }
    return out;
}

std::string spans_to_plain(const std::vector<TextSpan>& spans) {
    std::string out;
    for (const auto& sp : spans) {
        if (sp.is_drawing) out.push_back(' ');
        else out += sp.text;
    }
    return out;
}

namespace {

/// Spaces / newlines do not consume the typewriter budget.
bool is_typewriter_whitespace(char32_t cp) {
    return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r' || cp == 0xA0;
}

} // namespace

int count_printable_chars(const std::vector<TextSpan>& spans) {
    int n = 0;
    for (const auto& sp : spans) {
        if (sp.is_drawing) {
            ++n;
            continue;
        }
        std::string_view rest = sp.text;
        while (!rest.empty()) {
            const char32_t cp = next_codepoint(rest);
            if (cp != 0 && !is_typewriter_whitespace(cp)) ++n;
        }
    }
    return n;
}

bool is_page_complete(const std::vector<TextSpan>& spans, int visible_char_count) {
    if (visible_char_count < 0) return true;
    return visible_char_count >= count_printable_chars(spans);
}

namespace {

// Bitsy bitsy.js `rainbowColors`.
constexpr Color kRainbowColors[kTextboxRainbowCount] = {
    {255, 0, 0},   {255, 217, 0}, {78, 255, 0},  {0, 255, 125}, {0, 192, 255},
    {0, 18, 255},  {136, 0, 255}, {255, 0, 242}, {255, 0, 138}, {255, 0, 61},
};

struct Cursor {
    int cx = 0;
    int cy = 0;
    int line_h = 8;
    int page = 0;
    int col = 0;
    int row = 0;
};

int max_x_of(const TextboxLayout& layout) {
    return layout.width - layout.margin - 1;
}

int max_y_of(const TextboxLayout& layout) {
    return layout.height - layout.margin - 1;
}

/// Advance to the next line. Returns true if that overflowed onto a new page.
bool newline(Cursor& c, const TextboxLayout& layout, int font_h, bool paginate) {
    const int next = c.cy + c.line_h + 1;
    if (next + font_h > max_y_of(layout)) {
        if (paginate) {
            ++c.page;
            c.cx = layout.margin;
            c.cy = layout.margin;
            c.line_h = font_h;
            c.col = 0;
            c.row = 0;
            return true;
        }
    }
    c.cx = layout.margin;
    c.cy = next;
    c.line_h = font_h;
    c.col = 0;
    ++c.row;
    return false;
}

bool is_wrap_space(char c) {
    return c == ' ' || c == '\t';
}

int measure_word(const BitsyFont& font, std::string_view s) {
    int w = 0;
    while (!s.empty() && s.front() != '\n' && !is_wrap_space(s.front())) {
        w += font.glyph(next_codepoint(s)).spacing;
    }
    return w;
}

/// Wrap to the next row (and the next screen if the box is full) when @p need
/// does not fit on this row. Does not wrap at the start of a row, so a single
/// oversized word is not pulled apart until glyphs are placed one by one.
void wrap_if_needed(Cursor& c, int need, const TextboxLayout& layout, int font_h,
                    bool paginate, bool* new_page = nullptr) {
    if (c.cx + need > max_x_of(layout) && c.cx > layout.margin) {
        const bool np = newline(c, layout, font_h, paginate);
        if (new_page) *new_page = np;
    }
}

void append_run(std::vector<TextSpan>& page, const TextSpan& proto, std::string ch) {
    if (ch.empty()) return;
    if (!page.empty() && !page.back().is_drawing &&
        page.back().effects == proto.effects && page.back().color == proto.color &&
        ch != "\n" && page.back().text != "\n" &&
        (page.back().text.empty() || page.back().text.back() != '\n')) {
        page.back().text += ch;
        return;
    }
    TextSpan sp;
    sp.text = std::move(ch);
    sp.effects = proto.effects;
    sp.color = proto.color;
    page.push_back(std::move(sp));
}

bool is_sentence_end(char32_t cp) {
    return cp == '.' || cp == '!' || cp == '?';
}

bool has_printable(const std::vector<TextSpan>& page) {
    return count_printable_chars(page) > 0;
}

/// Drop spaces and row breaks at the end of @p page, so a page cut before a
/// sentence does not end in blank space.
void trim_trailing_whitespace(std::vector<TextSpan>& page) {
    while (!page.empty() && !page.back().is_drawing) {
        std::string& t = page.back().text;
        while (!t.empty() && (t.back() == '\n' || is_wrap_space(t.back()))) t.pop_back();
        if (!t.empty()) return;
        page.pop_back();
    }
}

} // namespace

void install_textbox_colors(std::span<Color> pal) {
    if (pal.size() <= kTextboxBlack) return;
    pal[kTextboxBlack] = Color{0, 0, 0};
    pal[kTextboxWhite] = Color{255, 255, 255};
    const std::size_t rainbow_end =
        static_cast<std::size_t>(kTextboxRainbow0 + kTextboxRainbowCount);
    if (pal.size() < rainbow_end) return;
    for (int i = 0; i < kTextboxRainbowCount; ++i) {
        pal[static_cast<std::size_t>(kTextboxRainbow0 + i)] = kRainbowColors[i];
    }
}

void install_textbox_colors(std::vector<Color>& pal) {
    if (pal.size() < 256) pal.resize(256, Color{0, 0, 0});
    install_textbox_colors(std::span<Color>(pal));
}

std::uint8_t rainbow_index(int col, double time_ms) {
    // Bitsy RainbowEffect: floor(positiveModulo(time / 100 - col * 0.5, count)).
    const double count = static_cast<double>(kTextboxRainbowCount);
    double step = time_ms / kTextboxRainbowTimeMs - static_cast<double>(col) * 0.5;
    step = step - count * std::floor(step / count);
    int idx = static_cast<int>(std::floor(step));
    if (idx < 0) idx = 0;
    if (idx >= kTextboxRainbowCount) idx = kTextboxRainbowCount - 1;
    return static_cast<std::uint8_t>(kTextboxRainbow0 + idx);
}

std::vector<std::vector<TextSpan>> paginate_spans(
    const BitsyFont& font,
    const std::vector<TextSpan>& spans,
    const TextboxLayout& layout)
{
    std::vector<std::vector<TextSpan>> pages;
    std::vector<TextSpan> cur;
    Cursor c;
    c.cx = layout.margin;
    c.cy = layout.margin;
    c.line_h = font.height;

    auto flush_page = [&] {
        if (!cur.empty()) pages.push_back(std::move(cur));
        cur = {};
    };

    std::size_t si = 0;   // span being laid out
    std::size_t pos = 0;  // byte offset into its text

    // Sentence tracking for layout.keep_sentences (citsy#14).
    bool after_end = false;      // last glyph was . ! ? (or a quote after one)
    bool at_boundary = false;    // whitespace followed a sentence end
    struct SentenceStart {
        bool valid = false;
        std::size_t si = 0, pos = 0, page = 0;
        std::vector<TextSpan> before;  // the page as it was before the sentence
    } start;

    auto note_glyph = [&](char32_t cp) {
        const bool closer = cp == '"' || cp == '\'' || cp == ')' ||
                            cp == 0x201D || cp == 0x2019;  // ” ’
        after_end = is_sentence_end(cp) || (after_end && closer);
    };
    auto note_space = [&] {
        if (after_end) at_boundary = true;
        after_end = false;
    };
    // Called where a new sentence's first glyph or drawing is about to go.
    auto mark_sentence_start = [&] {
        if (!at_boundary) return;
        at_boundary = false;
        start = {true, si, pos, pages.size(), cur};
    };
    // The page is full. Either move the unfinished sentence to the next page
    // (returns true; layout resumes at its first glyph) or keep paging on fit.
    auto next_page = [&]() -> bool {
        if (layout.keep_sentences && start.valid && !at_boundary &&
            start.page == pages.size() && has_printable(start.before)) {
            cur = std::move(start.before);
            trim_trailing_whitespace(cur);
            flush_page();
            c.cx = layout.margin;
            c.cy = layout.margin;
            c.line_h = font.height;
            c.col = 0;
            si = start.si;
            pos = start.pos;
            start.valid = false;
            after_end = false;
            return true;
        }
        flush_page();
        return false;
    };

    while (si < spans.size()) {
        const TextSpan& sp = spans[si];
        if (sp.is_drawing) {
            mark_sentence_start();
            bool np = false;
            wrap_if_needed(c, kTileSize, layout, font.height, true, &np);
            if (np && next_page()) continue;
            cur.push_back(sp);
            c.cx += kTileSize + 1;
            c.line_h = std::max(c.line_h, kTileSize);
            after_end = false;
            ++si;
            pos = 0;
            continue;
        }
        if (pos >= sp.text.size()) {
            ++si;
            pos = 0;
            continue;
        }

        const std::string_view text = sp.text;
        std::string_view rest = text.substr(pos);
        if (rest.front() == '\n') {
            ++pos;
            note_space();
            if (!newline(c, layout, font.height, true)) append_run(cur, sp, "\n");
            else next_page();
            continue;
        }
        if (is_wrap_space(rest.front())) {
            const char32_t cp = next_codepoint(rest);
            const std::size_t len = text.size() - pos - rest.size();
            note_space();
            const FontGlyph& g = font.glyph(cp);
            if (c.cx + g.spacing > max_x_of(layout) && c.cx > layout.margin) {
                // The space does not fit: break the row here. Keep the break
                // as a "\n" so render_textbox does not see the words on
                // either side as one word (citsy#13).
                if (!newline(c, layout, font.height, true)) append_run(cur, sp, "\n");
                else if (next_page()) continue;
                pos += len;
                while (pos < text.size() && is_wrap_space(text[pos])) ++pos;
                continue;
            }
            append_run(cur, sp, std::string(text.substr(pos, len)));
            pos += len;
            c.cx += g.spacing;
            c.line_h = std::max(c.line_h, g.height);
            continue;
        }

        if (pos == 0 || text[pos - 1] == '\n' || is_wrap_space(text[pos - 1])) {
            // First glyph of a word in this span: wrap the whole word.
            mark_sentence_start();
            bool np = false;
            wrap_if_needed(c, measure_word(font, rest), layout, font.height, true, &np);
            if (np && next_page()) continue;
        }
        const char32_t cp = next_codepoint(rest);
        const std::size_t len = text.size() - pos - rest.size();
        const FontGlyph& g = font.glyph(cp);
        bool np = false;
        wrap_if_needed(c, g.spacing, layout, font.height, true, &np);
        if (np && next_page()) continue;
        append_run(cur, sp, std::string(text.substr(pos, len)));
        pos += len;
        note_glyph(cp);
        c.cx += g.spacing;
        c.line_h = std::max(c.line_h, g.height);
    }
    flush_page();
    return pages;
}

std::vector<std::uint8_t> render_textbox(
    const BitsyFont& font,
    const std::vector<TextSpan>& spans,
    const TextboxLayout& layout)
{
    const int w = layout.width;
    const int h = layout.height;
    std::vector<std::uint8_t> buf(
        static_cast<std::size_t>(std::max(0, w * h)), kTextboxBlack);
    if (w < 4 || h < 4) return buf;

    auto put = [&](int x, int y, std::uint8_t col) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        buf[static_cast<std::size_t>(y * w + x)] = col;
    };

    struct Placed {
        int x, y, gw, gh;
        const std::uint8_t* data;
        int color;
        std::uint8_t effects;
        int index;
        int col;
        int row;
        int offx, offy;
        int printable_i;  ///< sequential printable index; -1 = whitespace
    };
    std::vector<Placed> placed;
    Cursor c;
    c.cx = layout.margin;
    c.cy = layout.margin;
    c.line_h = font.height;
    int index = 0;
    int printable = 0;

    auto place = [&](Placed p, bool counts) {
        p.printable_i = counts ? printable++ : -1;
        placed.push_back(p);
    };

    for (const auto& sp : spans) {
        if (sp.is_drawing) {
            wrap_if_needed(c, kTileSize, layout, font.height, false);
            Placed p;
            p.x = c.cx;
            p.y = c.cy;
            p.gw = kTileSize;
            p.gh = kTileSize;
            p.data = sp.drawing.data();
            p.color = sp.drawing_color;
            p.effects = GlyphFx::None;
            p.index = index++;
            p.col = c.col++;
            p.row = c.row;
            p.offx = 0;
            p.offy = 0;
            place(std::move(p), true);
            c.cx += kTileSize + 1;
            c.line_h = std::max(c.line_h, kTileSize);
            continue;
        }

        std::string_view rest = sp.text;
        while (!rest.empty()) {
            if (rest.front() == '\n') {
                rest.remove_prefix(1);
                newline(c, layout, font.height, false);
                continue;
            }
            if (is_wrap_space(rest.front())) {
                const char32_t cp = next_codepoint(rest);
                const FontGlyph& g = font.glyph(cp);
                if (c.cx + g.spacing > max_x_of(layout) && c.cx > layout.margin) {
                    // The space does not fit: break the row here, so a short next
                    // word can't land right after the previous one (citsy#13).
                    newline(c, layout, font.height, false);
                    while (!rest.empty() && is_wrap_space(rest.front())) rest.remove_prefix(1);
                    continue;
                }
                Placed p;
                p.x = c.cx;
                p.y = c.cy;
                p.gw = g.width;
                p.gh = g.height;
                p.data = g.data.data();
                p.color = sp.color;
                p.effects = sp.effects;
                p.index = index++;
                p.col = c.col++;
                p.row = c.row;
                p.offx = g.offset_x;
                p.offy = g.offset_y;
                place(std::move(p), !is_typewriter_whitespace(cp));
                c.cx += g.spacing;
                c.line_h = std::max(c.line_h, g.height);
                continue;
            }
            wrap_if_needed(c, measure_word(font, rest), layout, font.height, false);
            while (!rest.empty() && rest.front() != '\n' && !is_wrap_space(rest.front())) {
                const char32_t cp = next_codepoint(rest);
                const FontGlyph& g = font.glyph(cp);
                wrap_if_needed(c, g.spacing, layout, font.height, false);
                Placed p;
                p.x = c.cx;
                p.y = c.cy;
                p.gw = g.width;
                p.gh = g.height;
                p.data = g.data.data();
                p.color = sp.color;
                p.effects = sp.effects;
                p.index = index++;
                p.col = c.col++;
                p.row = c.row;
                p.offx = g.offset_x;
                p.offy = g.offset_y;
                place(std::move(p), !is_typewriter_whitespace(cp));
                c.cx += g.spacing;
                c.line_h = std::max(c.line_h, g.height);
            }
        }
    }

    if (layout.rtl) {
        int line_start = 0;
        while (line_start < static_cast<int>(placed.size())) {
            const int ly = placed[static_cast<std::size_t>(line_start)].y;
            int line_end = line_start + 1;
            while (line_end < static_cast<int>(placed.size()) &&
                   placed[static_cast<std::size_t>(line_end)].y == ly) {
                ++line_end;
            }
            int min_x = w, max_r = 0;
            for (int i = line_start; i < line_end; ++i) {
                min_x = std::min(min_x, placed[static_cast<std::size_t>(i)].x);
                max_r = std::max(max_r,
                    placed[static_cast<std::size_t>(i)].x +
                    placed[static_cast<std::size_t>(i)].gw);
            }
            const int span = max_r - min_x;
            const int origin = w - layout.margin - span;
            for (int i = line_start; i < line_end; ++i) {
                auto& p = placed[static_cast<std::size_t>(i)];
                p.x = origin + (p.x - min_x);
            }
            line_start = line_end;
        }
    }

    const double t = layout.time_ms;
    for (const auto& p : placed) {
        if (layout.visible_char_count >= 0 && p.printable_i >= 0 &&
            p.printable_i >= layout.visible_char_count) {
            continue;
        }
        // Bitsy dialog.js WavyEffect and ShakyEffect add to the offset, which
        // is rounded down once when the glyph is drawn.
        double ox = 0, oy = 0;
        std::uint8_t color = p.color < 0
            ? kTextboxWhite
            : static_cast<std::uint8_t>(p.color);
        const double col = p.col;
        const double row = p.row;
        if (p.effects & GlyphFx::Shaky) {
            oy += 1.5 * std::sin(t * 0.1 - col * 0.5) * std::cos(t * 0.3 - col * 0.2)
                * std::sin(t * 2.0 - row * 1.0);
            ox += 1.5 * std::cos(t * 0.1 - row * 1.0) * std::sin(t * 3.0 - col * 0.7)
                * std::cos(t * 0.2 - col * 0.3);
        }
        if (p.effects & GlyphFx::Wavy) {
            oy += std::sin(t / 250.0 - col / 2.0) * 2.0;
        }
        const int dx = static_cast<int>(std::floor(ox));
        const int dy = static_cast<int>(std::floor(oy));
        if (p.effects & GlyphFx::Rainbow) {
            color = rainbow_index(p.col, t);
        }
        if (!p.data) continue;
        for (int yy = 0; yy < p.gh; ++yy) {
            for (int xx = 0; xx < p.gw; ++xx) {
                if (!p.data[static_cast<std::size_t>(yy * p.gw + xx)]) continue;
                put(p.x + p.offx + xx + dx, p.y + p.offy + yy + dy, color);
            }
        }
    }

    if (layout.show_arrow) {
        // Bitsy dialog.js DrawNextArrow: a 5x3 arrow, on the left for RTL text.
        static constexpr std::uint8_t kArrow[3][5] = {
            {1, 1, 1, 1, 1}, {0, 1, 1, 1, 0}, {0, 0, 1, 0, 0}};
        const int left = layout.rtl ? 4 : w - (5 + 4);
        const int top = h - 5;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 5; ++x) {
                if (kArrow[y][x]) put(left + x, top + y, kTextboxWhite);
            }
        }
    }

    return buf;
}

} // namespace citsy
