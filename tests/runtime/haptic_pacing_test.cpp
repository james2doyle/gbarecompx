#include "haptic_pacing.h"

#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    using gbarecomp::haptic_clamp_strength;
    using gbarecomp::haptic_should_play;

    assert(haptic_clamp_strength(0.0f) == 0.0f);
    assert(haptic_clamp_strength(-5.0f) == 0.0f);
    assert(haptic_clamp_strength(0.5f) == 0.5f);
    assert(haptic_clamp_strength(1.0f) == 1.0f);
    assert(haptic_clamp_strength(2.0f) == 1.0f);
    // NaN must never leak through as a garbage rumble amplitude.
    assert(haptic_clamp_strength(std::numeric_limits<float>::quiet_NaN()) == 0.0f);

    assert(!haptic_should_play(0));
    assert(!haptic_should_play(-1));
    assert(haptic_should_play(1));
    assert(haptic_should_play(250));

    std::printf("haptic_pacing tests: PASS\n");
    return 0;
}
