#include "audio_gain.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    using gbarecomp::apply_volume_gain;

    assert(apply_volume_gain(0, 100) == 0);
    assert(apply_volume_gain(1000, 100) == 1000);
    assert(apply_volume_gain(1000, 50) == 500);
    assert(apply_volume_gain(-1000, 50) == -500);
    assert(apply_volume_gain(1000, 0) == 0);
    // truncation matches the original in-tree arithmetic.
    assert(apply_volume_gain(1, 33) == 0);
    assert(apply_volume_gain(3, 33) == 0);
    assert(apply_volume_gain(10, 33) == 3);

    std::printf("audio_gain tests: PASS\n");
    return 0;
}