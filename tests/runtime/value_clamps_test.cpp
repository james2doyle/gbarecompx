#include "value_clamps.h"

#include <cassert>
#include <cstdio>

int main() {
    using gbarecomp::clamp_fullscreen_mode;
    using gbarecomp::clamp_window_scale;
    using gbarecomp::clamp_volume_percent;

    assert(clamp_fullscreen_mode(-5) == 0);
    assert(clamp_fullscreen_mode(0) == 0);
    assert(clamp_fullscreen_mode(2) == 2);
    assert(clamp_fullscreen_mode(9) == 2);

    assert(clamp_window_scale(0) == 1);
    assert(clamp_window_scale(1) == 1);
    assert(clamp_window_scale(8) == 8);
    assert(clamp_window_scale(100) == 8);

    assert(clamp_volume_percent(-1) == 0);
    assert(clamp_volume_percent(50) == 50);
    assert(clamp_volume_percent(100) == 100);
    assert(clamp_volume_percent(150) == 100);

    std::printf("value_clamps tests: PASS\n");
    return 0;
}