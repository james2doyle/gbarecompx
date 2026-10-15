#pragma once

#include <cstdint>

namespace gbarecomp {

// Host-side volume scaling applied to pushed PCM before dispatch. 100 = copy;
// out-of-range values are clamped by callers. Pure arithmetic so the audio
// unit can be validated without any windowing/audio backend.
inline std::int16_t apply_volume_gain(std::int16_t sample, int volume) {
    return static_cast<std::int16_t>(
        (static_cast<std::int32_t>(sample) * volume) / 100);
}

}  // namespace gbarecomp