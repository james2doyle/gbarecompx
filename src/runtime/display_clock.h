#pragma once

#include <cstdint>

namespace gbarecomp {

// Display-clock period (nanoseconds) for an integer refresh rate. Returns 0 for
// unusable rates so callers preserve their previous period rather than divide
// by zero. Pure arithmetic; the host backend only supplies the Hz.
inline std::uint64_t display_refresh_period_ns(int refresh_hz) {
    if (refresh_hz <= 0) return 0;
    return 1000000000ULL / static_cast<std::uint64_t>(refresh_hz);
}

// Convenience: should the caller re-anchor boundaries? Useful for adaptive-rate panels.
inline bool display_refresh_changed(int refresh_hz, std::uint64_t current_period_ns) {
    return display_refresh_period_ns(refresh_hz) != current_period_ns;
}

}  // namespace gbarecomp