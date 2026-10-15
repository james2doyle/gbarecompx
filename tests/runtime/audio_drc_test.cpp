#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#define RECOMP_AUDIO_DRC_IMPL
#include "recomp_audio_drc.h"

int main() {
    rab_config cfg;
    rab_config_defaults(&cfg);
    cfg.channels = 1;
    cfg.source_rate = 48000.0;
    cfg.host_rate = 48000.0;
    cfg.target_ms = 10.0;
    cfg.ring_ms = 100.0;
    cfg.preroll_ms = 10.0;
    cfg.stretch_limit_ms = 20.0;

    rab_bridge bridge{};
    if (rab_init(&bridge, &cfg) != 0) {
        std::fprintf(stderr, "rab_init failed\n");
        return 1;
    }

    std::vector<int16_t> input(2400);
    for (std::size_t i = 0; i < input.size(); ++i) {
        constexpr double kPi = 3.14159265358979323846;
        input[i] = static_cast<int16_t>(
            std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) /
                     cfg.source_rate) *
            12000.0);
    }
    rab_push(&bridge, input.data(), static_cast<int>(input.size()));

    std::vector<int16_t> output(6000);
    rab_pull(&bridge, output.data(), static_cast<int>(output.size()));

    rab_stats stats{};
    rab_get_stats(&bridge, &stats);
    const uint64_t limit_frames =
        static_cast<uint64_t>(cfg.stretch_limit_ms * cfg.host_rate / 1000.0);
    if (stats.stretch_frames == 0 || stats.stretch_frames > limit_frames) {
        std::fprintf(stderr,
                     "stall concealment exceeded limit: stretch=%llu limit=%llu\n",
                     static_cast<unsigned long long>(stats.stretch_frames),
                     static_cast<unsigned long long>(limit_frames));
        rab_free(&bridge);
        return 2;
    }

    const auto tail_begin = output.end() - 256;
    const int16_t tail_peak = *std::max_element(
        tail_begin, output.end(),
        [](int16_t a, int16_t b) { return std::abs(a) < std::abs(b); });
    if (std::abs(static_cast<int>(tail_peak)) > 1) {
        std::fprintf(stderr, "stalled output did not fade to silence: peak=%d\n",
                     static_cast<int>(tail_peak));
        rab_free(&bridge);
        return 3;
    }
    rab_free(&bridge);

    // Ring-index wraparound: a DC level lapped several times past capacity
    // must come back at the same level. Indexes fold with a mask over the
    // power-of-two capacity (not integer division); a wrong fold reads stale
    // slots and the level breaks. DC is robust to the controller's timing
    // wobble (unit-DC-gain filter passes it exactly at any phase).
    {
        rab_config wcfg;
        rab_config_defaults(&wcfg);
        wcfg.channels = 1;
        wcfg.source_rate = 48000.0;
        wcfg.host_rate = 48000.0;
        wcfg.target_ms = 20.0;
        wcfg.ring_ms = 30.0;
        wcfg.preroll_ms = 5.0;
        rab_bridge wbridge{};
        if (rab_init(&wbridge, &wcfg) != 0) {
            std::fprintf(stderr, "wrap: rab_init failed\n");
            return 4;
        }
        constexpr int16_t kDc = 8000;
        std::vector<int16_t> chunk(512, kDc), got(512);
        // >4 laps of the ~4k-frame ring, steady push/pull (never starves).
        int64_t post_warmup_abs_sum = 0;
        int64_t post_warmup_n = 0;
        int worst_dev = 0;
        for (int i = 0; i < 40; ++i) {
            rab_push(&wbridge, chunk.data(), 512);
            rab_pull(&wbridge, got.data(), 512);
            if (i < 8) continue;  // prime + fade + filter history settle
            for (int16_t s : got) {
                const int dev = std::abs(static_cast<int>(s) - kDc);
                if (dev > worst_dev) worst_dev = dev;
                post_warmup_abs_sum += dev;
                ++post_warmup_n;
            }
        }
        rab_stats wstats{};
        rab_get_stats(&wbridge, &wstats);
        rab_free(&wbridge);
        const double mean_dev =
            static_cast<double>(post_warmup_abs_sum) / post_warmup_n;
        if (wstats.underrun_events != 0 || worst_dev > 1500 ||
            mean_dev > 400.0) {
            std::fprintf(stderr,
                         "wrap: DC broke across laps: underruns=%llu "
                         "worst_dev=%d mean_dev=%.1f\n",
                         static_cast<unsigned long long>(
                             wstats.underrun_events),
                         worst_dev, mean_dev);
            return 5;
        }
    }

    return 0;
}
