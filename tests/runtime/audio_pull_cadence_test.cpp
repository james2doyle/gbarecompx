// Audio topology under the REAL production cadence.
//
// audio_drc_test covers RAB's stall concealment at a 1:1 rate with one large
// push and one large pull. Neither backend runs that way:
//
//   * the ratio is 65536 (GBA) -> 48000 (device), a real 1.365x resample
//   * the emulation thread pushes one emulated frame's worth of samples
//     (~1097 frames) and the audio thread pulls a stream-buffer chunk
//     (4096) at a time, continuously
//
// That producer/consumer relationship is exactly where a mis-sized buffer shows
// up, and it is the topology the raylib backend inherits from SDL. This test
// drives it with no audio device and no window: RAB is pure C++ with no SDL
// dependency at all.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#define RECOMP_AUDIO_DRC_IMPL
#include "recomp_audio_drc.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// Production values, kept in sync with both backends' open().
constexpr double kSourceRate = 65536.0;  // GBA standardized mixer rate
constexpr double kHostRate = 48000.0;    // raylib stream rate
constexpr double kEmuFps = 59.7275;
constexpr int kEmuFrameSourceFrames = 1097;  // round(65536 / 59.7275)

}  // namespace

int main() {
    rab_config cfg;
    rab_config_defaults(&cfg);
    cfg.channels = 1;
    cfg.source_rate = kSourceRate;
    cfg.host_rate = kHostRate;
    cfg.target_ms = 60.0;    // steady cushion (both backends)
    cfg.preroll_ms = 250.0;  // hide the cold-start recomp warm-up hitch

    rab_bridge bridge{};
    if (rab_init(&bridge, &cfg) != 0) {
        std::fprintf(stderr, "rab_init failed\n");
        return 1;
    }

    // A 440 Hz tone at a modest amplitude, generated per emulated frame.
    std::vector<int16_t> block(kEmuFrameSourceFrames);
    std::vector<int16_t> out(4096);

    int phase = 0;
    const int iterations = 600;  // ~10 s of emulated time
    std::uint64_t pulled_total = 0;

    for (int iter = 0; iter < iterations; ++iter) {
        for (int i = 0; i < kEmuFrameSourceFrames; ++i, ++phase) {
            block[i] = static_cast<int16_t>(
                std::sin(2.0 * kPi * 440.0 * static_cast<double>(phase) /
                         kSourceRate) *
                8000.0);
        }
        rab_push(&bridge, block.data(), kEmuFrameSourceFrames);

        // What the audio thread drains per servicing, in DEVICE frames. This is
        // the resampled equivalent of what we just pushed.
        const int pull_frames = static_cast<int>(
            static_cast<double>(kEmuFrameSourceFrames) * kHostRate / kSourceRate);
        rab_pull(&bridge, out.data(), pull_frames);
        pulled_total += static_cast<std::uint64_t>(pull_frames);
    }

    rab_stats stats{};
    rab_get_stats(&bridge, &stats);

    // The whole point: a correctly-sized cushion absorbs the push/pull cadence
    // with no stall concealment and no overflow.
    if (stats.underrun_events != 0) {
        std::fprintf(stderr,
                     "underruns at production cadence: %llu\n",
                     static_cast<unsigned long long>(stats.underrun_events));
        rab_free(&bridge);
        return 2;
    }
    if (stats.overflow_drops != 0) {
        std::fprintf(stderr,
                     "overflow drops at production cadence: %llu\n",
                     static_cast<unsigned long long>(stats.overflow_drops));
        rab_free(&bridge);
        return 3;
    }

    // Stretch must stay inside the configured concealment limit.
    const std::uint64_t stretch_limit =
        static_cast<std::uint64_t>(cfg.stretch_limit_ms * kHostRate / 1000.0);
    if (stats.stretch_frames > stretch_limit) {
        std::fprintf(stderr,
                     "stretch exceeded limit: %llu > %llu\n",
                     static_cast<unsigned long long>(stats.stretch_frames),
                     static_cast<unsigned long long>(stretch_limit));
        rab_free(&bridge);
        return 4;
    }

    // Sustained operation should hold a real cushion, not sit at zero. If the
    // buffer were mis-sized the fill would collapse over time.
    const double fill_ms = rab_fill_ms(&bridge);
    if (!(fill_ms > cfg.target_ms * 0.5)) {
        std::fprintf(stderr,
                     "cushion collapsed to %.1f ms (target %.1f ms)\n",
                     fill_ms, cfg.target_ms);
        rab_free(&bridge);
        return 5;
    }

    // The pulled stream must actually carry the tone, not silence.
    const int16_t peak = *std::max_element(
        out.begin(), out.end(),
        [](int16_t a, int16_t b) { return std::abs(a) < std::abs(b); });
    if (std::abs(static_cast<int>(peak)) < 1000) {
        std::fprintf(stderr, "output is effectively silent: peak=%d\n", peak);
        rab_free(&bridge);
        return 6;
    }

    rab_free(&bridge);
    std::printf("audio_pull_cadence tests: PASS (%llu frames pulled, "
                "underruns=0 overflow=0)\n",
                static_cast<unsigned long long>(pulled_total));
    return 0;
}