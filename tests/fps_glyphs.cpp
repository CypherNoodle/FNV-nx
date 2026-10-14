#include "fps_glyphs.h"
#include <cassert>
#include <iostream>
int main() {
    unsigned count = 0;
    fnv::BuildFpsGlyphs("FPS: 999.9", [&](const fnv::FpsRect& r) {
        assert(r.x >= 16 && r.x + r.width <= 16 + 12 * 10);
        assert(r.y >= 16 && r.y + r.height <= 30 && r.width && r.height == 2);
        ++count;
    });
    assert(count > 0 && count < 256);
    for (char c = '0'; c <= '9'; ++c) {
        unsigned bits = 0;
        for (auto row : fnv::FpsGlyph(c)) { assert(row < 32); bits |= row; }
        assert(bits);
    }
    std::cout << "PASS FPS glyph geometry: digits, bounds, capacity\n";
}
