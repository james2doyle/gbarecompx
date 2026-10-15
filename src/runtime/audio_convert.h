#pragma once

#include <cstdint>

namespace gbarecomp {

// RAB (recomp_audio_drc) produces mono signed 16-bit PCM at the GBA's
// standardized 65536 Hz. raylib's raw AudioStream takes **32-bit float** samples
// (`LoadAudioStream(rate, 32, channels)`), so every backend must convert at the
// audio thread. This is that conversion, kept pure and testable.
//
// Scale is 1/32768 rather than 1/32767: it maps INT16_MIN to exactly -1.0
// instead of slightly beyond it, so full-scale negative audio cannot wrap.

inline constexpr float kS16ToFloatScale = 1.0f / 32768.0f;

inline float s16_to_float(std::int16_t sample) {
    return static_cast<float>(sample) * kS16ToFloatScale;
}

}  // namespace gbarecomp
