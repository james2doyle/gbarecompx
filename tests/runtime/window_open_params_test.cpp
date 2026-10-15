#include "window_open_params.h"

#include <cassert>
#include <cstdio>

using gbarecomp::resolve_open_params;

int main() {
    // Native default geometry is not "expanded".
    auto n = resolve_open_params(3, 240, 160, false, false);
    assert(n.scale == 3 && n.base_w == 240 && n.base_h == 160);
    assert(!n.expanded_view);

    // Degenerate inputs clamp to the native GBA surface / minimum scale.
    auto d = resolve_open_params(0, 0, -5, false, false);
    assert(d.scale == 1 && d.base_w == 240 && d.base_h == 160);
    assert(!d.expanded_view);

    // Any non-native size marks the expanded/widescreen path.
    assert(resolve_open_params(1, 320, 160, false, false).expanded_view);
    assert(resolve_open_params(1, 240, 240, false, false).expanded_view);
    assert(!resolve_open_params(1, 240, 160, false, false).expanded_view);

    // Negative scale clamps up, not down.
    assert(resolve_open_params(-4, 240, 160, false, false).scale == 1);

    // Sharp wins over linear: the two scalers are mutually exclusive.
    auto s = resolve_open_params(1, 240, 160, true, true);
    assert(s.sharp_filter && !s.linear_filter);
    assert(resolve_open_params(1, 240, 160, true, false).linear_filter);
    assert(!resolve_open_params(1, 240, 160, false, false).linear_filter);

    std::printf("window_open_params tests: PASS\n");
    return 0;
}
