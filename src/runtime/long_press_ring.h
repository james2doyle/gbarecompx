// long_press_ring.h — the engine long-press cue (plan §3.4 / Phase 3d): the
// pure progress computation and drawing for the settings-menu trigger ring,
// over HostOverlay primitives (the touch_pad_draw.h pattern).
//
// The semantic source is the SDL backend's draw_engine_long_press: rings for
// held, unmoved pointers that have not fired yet; hidden for the first 150 ms
// (a tap must not flash a ring); progress = elapsed / long_press_ms; drawn as
// a 7 mm arc, 1.2 mm stroke, light blue, sweeping clockwise from 12 o'clock,
// in drawable space. Shared by the raylib path and the tests.
#pragma once

#include <algorithm>
#include <cstdint>

#include "host_overlay.h"
#include "touch_input.h"

namespace gbarecomp {

// One ring to draw this frame, in drawable pixels.
struct LongPressRing {
    float x = 0.0f, y = 0.0f;
    float progress = 0.0f;  // 0..1
};

// Collect the rings for this frame. Returns the number written (<= max_out).
inline int collect_long_press_rings(const TouchPointSnapshot* points, int count,
                                    std::uint32_t now_ms,
                                    std::uint32_t long_press_ms,
                                    LongPressRing* out, int max_out) {
    if (!out || max_out <= 0 || long_press_ms == 0) return 0;
    int n = 0;
    for (int i = 0; i < count && n < max_out; ++i) {
        const TouchPointSnapshot& pt = points[i];
        if (pt.moved || pt.long_pressed) continue;
        // Same wrap-safe arithmetic as the hub's own long-press check.
        const std::uint32_t elapsed = now_ms - pt.down_ms;
        if (elapsed < 150) continue;
        LongPressRing ring;
        ring.x = pt.drawable_x;
        ring.y = pt.drawable_y;
        ring.progress = std::min(1.0f, static_cast<float>(elapsed) /
                                           static_cast<float>(long_press_ms));
        out[n++] = ring;
    }
    return n;
}

// Draw one ring exactly as the SDL backend does (geometry in mm * px_per_mm).
inline void draw_long_press_ring(HostOverlay& ov, const LongPressRing& ring,
                                 float px_per_mm) {
    ov.arc(ring.x, ring.y, 7.0f * px_per_mm, 1.2f * px_per_mm, 0.0f,
           ring.progress, {80, 210, 255, 200}, OverlaySpace::Drawable);
}

}  // namespace gbarecomp
