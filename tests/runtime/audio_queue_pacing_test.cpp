#include "audio_queue_pacing.h"
#include "audio_gain.h"

#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    using gbarecomp::audio_direct_underran;
    using gbarecomp::audio_direct_preroll_ready;
    using gbarecomp::kAudioDirectPrerollBytes;
    using gbarecomp::apply_volume_gain;

    assert(kAudioDirectPrerollBytes == 16384);

    // Underrun: only a started device with a fully drained queue.
    assert(!audio_direct_underran(false, 0));   // never started
    assert(!audio_direct_underran(true, 512));  // started, still queued
    assert(!audio_direct_underran(false, 512));
    assert(audio_direct_underran(true, 0));

    // Preroll: only an unstarted device crossing the threshold.
    assert(!audio_direct_preroll_ready(true, 999999));   // already playing
    assert(!audio_direct_preroll_ready(false, 0));
    assert(!audio_direct_preroll_ready(false, kAudioDirectPrerollBytes - 1));
    assert(audio_direct_preroll_ready(false, kAudioDirectPrerollBytes));
    assert(audio_direct_preroll_ready(false, kAudioDirectPrerollBytes + 4096));

    // Volume gain is passthrough at 100 and clamps out-of-range values.
    assert(apply_volume_gain(1234, 100) == 1234);
    assert(apply_volume_gain(0, 50) == 0);
    assert(apply_volume_gain(1000, 50) == 500);
    assert(apply_volume_gain(-1000, 50) == -500);
    // Full-scale input at half volume must not overflow int16.
    const int16_t peak = std::numeric_limits<int16_t>::max();
    assert(apply_volume_gain(peak, 50) == static_cast<int16_t>(peak / 2));
    assert(apply_volume_gain(-peak, 50) == static_cast<int16_t>(-peak / 2));

    std::printf("audio_queue_pacing tests: PASS\n");
    return 0;
}