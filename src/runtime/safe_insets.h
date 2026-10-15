#pragma once

#include <cstdio>
#include <algorithm>

namespace gbarecomp {

struct SafeInsets {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

// Parses "l,t,r,b" drawable-pixel inset strings. Returns true only when all four
// fields parsed; out values are clamped to >= 0.
inline bool parse_safe_insets(const char* text, SafeInsets& out) {
    if (!text) return false;
    int l = 0, t = 0, r = 0, b = 0;
    if (std::sscanf(text, "%d,%d,%d,%d", &l, &t, &r, &b) != 4) return false;
    out.left = std::max(0, l);
    out.top = std::max(0, t);
    out.right = std::max(0, r);
    out.bottom = std::max(0, b);
    return true;
}

} // namespace gbarecomp