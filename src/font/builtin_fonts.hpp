#pragma once

// Bitsy 8.15's built-in fonts as packed bitmaps. builtin_fonts.cpp is written
// by scripts/gen_builtin_fonts.py; font.cpp turns an entry into a BitsyFont.

#include <cstdint>
#include <span>
#include <string_view>

namespace citsy {

struct BuiltinGlyph {
    std::uint32_t codepoint;
    std::uint8_t  width;
    std::uint8_t  height;
    std::int8_t   offset_x;
    std::int8_t   offset_y;
    std::int8_t   spacing;
    std::uint32_t bit_offset;  ///< first pixel in the font's bit array
};

struct BuiltinFont {
    std::string_view name;
    int width;
    int height;
    std::span<const BuiltinGlyph> glyphs;
    const std::uint8_t* bits;  ///< row-major pixels, most significant bit first
};

[[nodiscard]] std::span<const BuiltinFont> builtin_font_table();

} // namespace citsy
