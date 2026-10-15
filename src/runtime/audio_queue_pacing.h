#pragma once

#include <cstddef>
#include <cstdint>

namespace gbarecomp {

// Preroll cushion for the SDL direct-queue audio path. The device starts
// paused; we let a cushion accumulate before unpausing so the first audible
// block does not underrun. 16384 bytes = 125 ms at 65536 Hz mono S16.
inline constexpr std::uint32_t kAudioDirectPrerollBytes = 16384;

// A long producer stall (reset, loading hitch, codegen pause) can drain the
// queue completely. When that happens on an already-started device, restarting
// on the next tiny block is what produces audible crackle — instead drop back
// to paused and rebuild a full cushion.
inline bool audio_direct_underran(bool started, std::uint32_t queued_bytes) {
    return started && queued_bytes == 0;
}

// Has the cushion reached the preroll threshold? Only a device that has never
// been started is eligible; a started device is already playing.
inline bool audio_direct_preroll_ready(bool started, std::uint32_t queued_bytes) {
    return !started && queued_bytes >= kAudioDirectPrerollBytes;
}

}  // namespace gbarecomp