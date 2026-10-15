#include "interp_pacing.h"

#include <cassert>
#include <cstdio>

using namespace gbarecomp;

int main() {
    const std::uint64_t now = 1000;
    const std::uint64_t deadline = 5000;

    // Deadline clamping never oversleeps a future deadline.
    assert(interp_clamp_wake_ns(900, now, deadline) == 0);       // already past
    assert(interp_clamp_wake_ns(2000, now, deadline) == 2000);    // before deadline
    assert(interp_clamp_wake_ns(9000, now, deadline) == deadline); // clamped
    assert(interp_clamp_wake_ns(9000, now, now) == 9000);         // no future deadline

    // A vsync present that would exceed the deadline is held.
    assert(interp_should_hold_for_deadline(true, 100, now, deadline));
    assert(!interp_should_hold_for_deadline(false, 100, now, deadline));
    assert(!interp_should_hold_for_deadline(true, 0, now, deadline));
    assert(!interp_should_hold_for_deadline(true, 100, now, now + 50));

    // Sleep kind selection.
    assert(interp_sleep_kind(true, 100, now + 50, now, deadline) == InterpSleep::kNone);
    assert(interp_sleep_kind(false, 0, now, now, deadline) == InterpSleep::kFixed);
    assert(interp_sleep_kind(false, 100, now + 50, now, deadline) == InterpSleep::kBoundary);
    assert(interp_sleep_kind(false, 100, now, now, deadline) == InterpSleep::kNone);

    // Boundary advance re-anchors for vsync/unknown and steps forward otherwise.
    assert(interp_advance_tick_ns(true, 100, now, now + 20) == now + 20);
    assert(interp_advance_tick_ns(false, 0, now, now + 20) == now + 20);
    assert(interp_advance_tick_ns(false, 100, now, now + 20) == now + 100);
    assert(interp_advance_tick_ns(false, 100, now + 500, now + 20) == now + 600);

    std::printf("interp_pacing tests: PASS\n");
    return 0;
}
