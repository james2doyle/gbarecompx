#pragma once

namespace gbarecomp {

// Clamp a haptic strength request into the driver's usable range.
// Mirrors std::clamp(x, 0.0f, 1.0f) but is dependency-free and testable
// without pulling in <algorithm>.
inline float haptic_clamp_strength(float strength) {
    if (!(strength > 0.0f)) return 0.0f;  // also catches NaN
    if (strength > 1.0f) return 1.0f;
    return strength;
}

// A zero/negative-duration request must not reach the driver; it can tear down
// or wedge a rumble device on some platforms. Host treats it as a no-op.
inline bool haptic_should_play(int duration_ms) {
    return duration_ms > 0;
}

}  // namespace gbarecomp
