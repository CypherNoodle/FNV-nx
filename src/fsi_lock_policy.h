#pragma once
namespace fnv {
inline bool LateFsiEligible(bool enabled, bool fsi, bool pixel_present,
                            bool kills, bool writes_depth, bool pixel_memexport,
                            bool vertex_memexport, bool stencil, bool alpha_coverage) {
    return enabled && fsi && pixel_present && !kills && !writes_depth &&
           !pixel_memexport && !vertex_memexport && !stencil && !alpha_coverage;
}
}
