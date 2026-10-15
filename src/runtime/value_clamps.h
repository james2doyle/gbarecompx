#pragma once

#include <algorithm>

namespace gbarecomp {

inline int clamp_int(int value, int low, int high) {
    return std::max(low, std::min(high, value));
}
inline int clamp_fullscreen_mode(int mode) { return clamp_int(mode, 0, 2); }
inline int clamp_window_scale(int scale) { return clamp_int(scale, 1, 8); }
inline int clamp_volume_percent(int pct) { return clamp_int(pct, 0, 100); }

} // namespace gbarecomp