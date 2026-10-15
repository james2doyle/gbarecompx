#pragma once

#include <cstdint>

namespace gbarecomp {

// Pure decisions for the frame-interpolation pacing loop (see
// HostWindow::interp_display_tick). The host supplies the clock and performs
// the sleep; everything here is integer arithmetic with no platform calls.
//
// The guest frame pacer supplies a `deadline_ns` that this loop must never hold
// past. Holding the last presented frame is correct; oversleeping hands the
// guest extra wall time and skews its measured rate. Both branches below clamp
// the wake time to `deadline_ns` whenever that is in the future.

enum class InterpSleep {
    kNone,      // no sleep; present immediately
    kFixed,     // fixed pacing budget (unknown display rate)
    kBoundary,  // wait for the next interpolated display boundary
};

// Clamp a proposed wake time so it never exceeds a future deadline.
// Returns 0 when the caller should not sleep at all.
inline std::uint64_t interp_clamp_wake_ns(std::uint64_t wake_ns,
                                          std::uint64_t now_ns,
                                          std::uint64_t deadline_ns) {
    if (wake_ns <= now_ns) return 0;
    if (deadline_ns > now_ns && wake_ns > deadline_ns) return deadline_ns;
    return wake_ns;
}

// A vsync-blocked present already consumes a full display period: presenting
// now would push the guest past its deadline. Hold and sleep out the remainder
// instead of emitting an extra frame early.
inline bool interp_should_hold_for_deadline(bool interp_vsync,
                                           std::uint64_t period_ns,
                                           std::uint64_t now_ns,
                                           std::uint64_t deadline_ns) {
    return interp_vsync && period_ns > 0 &&
           now_ns + period_ns > deadline_ns;
}

// Decide how long to sleep before the next interpolated present.
//  * unknown display rate -> kFixed (caller supplies its own small budget)
//  * vsync present       -> the present itself paces, so no pre-sleep
//  * non-vsync           -> sleep to the next boundary, clamped to the deadline
inline InterpSleep interp_sleep_kind(bool interp_vsync,
                                     std::uint64_t period_ns,
                                     std::uint64_t tick_ns,
                                     std::uint64_t now_ns,
                                     std::uint64_t deadline_ns) {
    if (interp_vsync) return InterpSleep::kNone;
    if (period_ns == 0) return InterpSleep::kFixed;
    if (tick_ns <= now_ns) return InterpSleep::kNone;
    (void)deadline_ns;  // clamping happens in interp_clamp_wake_ns
    return InterpSleep::kBoundary;
}

// Advance the boundary cursor after a present that completed at `after_ns`.
// Vsync-blocked and unknown-rate presents re-anchor (the boundary clock is no
// longer meaningful); otherwise the cursor advances by whole periods until it
// is strictly in the future.
inline std::uint64_t interp_advance_tick_ns(bool interp_vsync,
                                            std::uint64_t period_ns,
                                            std::uint64_t tick_ns,
                                            std::uint64_t after_ns) {
    if (interp_vsync || period_ns == 0) return after_ns;
    std::uint64_t next = tick_ns == 0 ? after_ns : tick_ns;
    // Bounded so a zero/lagging cursor cannot spin forever.
    for (int i = 0; i < 4096 && next <= after_ns; ++i) next += period_ns;
    return next > after_ns ? next : after_ns + period_ns;
}

}  // namespace gbarecomp
