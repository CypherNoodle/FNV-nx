#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace fnv {
inline std::array<uint8_t, 7> FpsGlyph(char c) {
    switch (c) {
        case '0': return {14,17,19,21,25,17,14};
        case '1': return {4,12,4,4,4,4,14};
        case '2': return {14,17,1,2,4,8,31};
        case '3': return {30,1,1,14,1,1,30};
        case '4': return {2,6,10,18,31,2,2};
        case '5': return {31,16,16,30,1,1,30};
        case '6': return {14,16,16,30,17,17,14};
        case '7': return {31,1,2,4,8,8,8};
        case '8': return {14,17,17,14,17,17,14};
        case '9': return {14,17,17,15,1,1,14};
        case 'F': return {31,16,16,30,16,16,16};
        case 'P': return {30,17,17,30,16,16,16};
        case 'S': return {15,16,16,14,1,1,30};
        case ':': return {0,4,4,0,4,4,0};
        case '.': return {0,0,0,0,0,12,12};
        default: return {};
    }
}
struct FpsRect { uint32_t x, y, width, height; };
template <typename Emit> void BuildFpsGlyphs(std::string_view text, Emit emit) {
    uint32_t x = 16;
    for (const char c : text) {
        const auto glyph = FpsGlyph(c);
        for (unsigned row = 0; row < 7; ++row) {
            for (unsigned col = 0; col < 5;) {
                if (!(glyph[row] & (1u << (4 - col)))) { ++col; continue; }
                const auto first = col;
                while (col < 5 && (glyph[row] & (1u << (4 - col)))) ++col;
                emit(FpsRect{x + first * 2, 16 + row * 2, (col - first) * 2, 2});
            }
        }
        x += 12;
    }
}
}
