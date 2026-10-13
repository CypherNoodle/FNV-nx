#include "../src/render_mode_guard.h"
#include <cassert>
#include <cstdio>
int main() {
    assert(FnvCanOverrideRenderMode(0x8230F3F0,1280,720,1280,720,960,540));
    assert(FnvCanOverrideRenderMode(0x8230F3F0,1280,720,1280,720,1280,720));
    assert(!FnvCanOverrideRenderMode(0x8230F3F4,1280,720,1280,720,960,540));
    assert(!FnvCanOverrideRenderMode(0x8230F3F0,1280,720,640,720,960,540));
    assert(!FnvCanOverrideRenderMode(0x8230F3F0,640,480,640,480,960,540));
    assert(!FnvCanOverrideRenderMode(0x8230F3F0,1280,720,1280,720,960,720));
    assert(!FnvCanOverrideRenderMode(0x8230F3F0,1280,720,1280,720,UINT32_MAX,540));
    assert(!FnvCanOverrideRenderMode(0x8230F3F0,1280,720,1280,720,992,558));
    std::puts("PASS: exact callsite, consistent globals, native input, aspect/alignment/bounds");
}
