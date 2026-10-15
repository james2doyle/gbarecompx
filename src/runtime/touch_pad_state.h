#pragma once

#include <cstdint>

namespace gbarecomp {

// Pure helpers for the on-screen touch pad visibility state. Keeping these
// arithmetic/decision rules SDL-free makes them reusable on Android/raylib
// backends, which only have to supply the current monotonic tick in ms.

// Should the pad hide itself after idle? Times are unsigned ms; the
// subtraction already handles clock wraparound the same way SDL_TICKS_PASSED.
inline bool touch_pad_should_idle_hide(bool idle_enabled,
                                       bool touch_controls,
                                       bool pad_visible,
                                       bool already_hidden,
                                       bool held,
                                       std::uint32_t now_ms,
                                       std::uint32_t last_touch_ms,
                                       int idle_seconds) {
    if (!idle_enabled || !touch_controls || !pad_visible || already_hidden)
        return false;
    if (held) return false;
    if (idle_seconds <= 0) return false;
    const std::uint32_t deadline =
        last_touch_ms + static_cast<std::uint32_t>(idle_seconds) * 1000u;
    return (now_ms - last_touch_ms) >= (deadline - last_touch_ms);
}

// Initial visibility when the player has no saved choice. pad_default < 0 means
// "platform default"; otherwise nonzero means visible.
inline bool touch_pad_initial_visible(bool touch_controls,
                                     int pad_default,
                                     bool platform_default) {
    if (!touch_controls) return false;
    const bool want = (pad_default < 0) ? platform_default : (pad_default != 0);
    return want;
}

}  // namespace gbarecomp