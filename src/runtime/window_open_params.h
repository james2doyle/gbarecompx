#pragma once

#include "window_backend.h"

namespace gbarecomp {

// Pure normalization of the requested window geometry, lifted out of the SDL
// backend's open() so it can be validated without a windowing system.
//
// Rules (matching the historical behavior exactly):
//   * scale  < 1      -> 1
//   * base_w < 1      -> 240 (native GBA width)
//   * base_h < 1      -> 160 (native GBA height)
//   * expanded_view   -> any non-native logical size
//   * linear_filter   -> requested linear AND not sharp; sharp wins, because
//                        the sharp path is a superset of the linear path and
//                        the two scalers are mutually exclusive.
struct ResolvedOpenParams {
    int scale = 1;
    int base_w = 240;
    int base_h = 160;
    bool expanded_view = false;
    bool linear_filter = false;
    bool sharp_filter = false;
};

inline ResolvedOpenParams resolve_open_params(int scale, int base_w, int base_h,
                                              bool linear_filter,
                                              bool sharp_filter) {
    ResolvedOpenParams p;
    p.scale = scale < 1 ? 1 : scale;
    p.base_w = base_w < 1 ? 240 : base_w;
    p.base_h = base_h < 1 ? 160 : base_h;
    p.expanded_view = (p.base_w != 240) || (p.base_h != 160);
    p.sharp_filter = sharp_filter;
    p.linear_filter = linear_filter && !sharp_filter;
    return p;
}

}  // namespace gbarecomp