// host_platform.h — Host-side things: file I/O, time, threads,
// window + audio device output.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gbarecomp {

// ── InterpShift (issue #9 3b) ────────────────────────────────────────
// True-midpoint display interpolation, pure arithmetic: no SDL, no
// window, no guest state — unit-tested in ffta_unit_tests without a
// display. runtime.cpp wires these to buffers; all policy lives here.
//
// Model: the camera step between two presented frames is measured from
// the BG scroll registers; a non-first display tick shows the current
// frame shifted BACK by half that step (the true halfway position).
// Measured, never predicted: steady motion midpoints smoothly, snaps
// halve, oscillation lands sub-pixel, still frames are bit-identical.
namespace interp_shift {

// Scroll snapshot: HOFS/VOFS per BG layer (GBA IO 0x10..0x1F).
inline constexpr std::size_t kScrollSnapshotBytes = 16;

// Wrap-aware 9-bit scroll delta, folded to [-256, 256].
inline int32_t fold_scroll_delta(int32_t d) {
    d %= 512;
    if (d > 256)
        d -= 512;
    else if (d < -256)
        d += 512;
    return d;
}

// Scroll step prev → cur (16-byte snapshots as above), consensus rule.
// A whole-frame midpoint shift is only valid when the guest's scroll
// motion is unambiguous: every layer that moved must report the SAME
// (dh, dv) pair — layers that stayed still are fine, their pixels are
// pinned by select_static. When moving layers disagree, the register
// delta is not a camera pan: FFTA rebases BG2/BG3 HOFS by large
// amounts mid-pan (observed 0 → 648 raw = +136 masked, visually a
// no-op — the rendered frames stay continuous) and animates overlay
// layers while the world pans; shifting the composed frame by those
// deltas fabricated a ~68 px displaced frame for one display tick —
// the "wild shifting" blip (issue #9, dense capture 2026-10-13).
// On disagreement this returns (0,0): the live frame shows on every
// tick, exactly the retail presentation for that instant. Affine
// layers hold stale HOFS slots in affine modes, so they read static
// and never vote — the safe default.
inline void dominant_step(const uint8_t prev[kScrollSnapshotBytes],
                          const uint8_t cur[kScrollSnapshotBytes],
                          int32_t* out_dh, int32_t* out_dv) {
    int32_t step_dh = 0, step_dv = 0;
    bool have_step = false;
    for (unsigned layer = 0; layer < 4; ++layer) {
        const unsigned off = layer * 4u;
        const int32_t h =
            static_cast<int32_t>(cur[off] | (cur[off + 1u] << 8)) & 0x1FF;
        const int32_t v =
            static_cast<int32_t>(cur[off + 2u] | (cur[off + 3u] << 8)) &
            0x1FF;
        const int32_t ph =
            static_cast<int32_t>(prev[off] | (prev[off + 1u] << 8)) & 0x1FF;
        const int32_t pv =
            static_cast<int32_t>(prev[off + 2u] | (prev[off + 3u] << 8)) &
            0x1FF;
        const int32_t dh = fold_scroll_delta(h - ph);
        const int32_t dv = fold_scroll_delta(v - pv);
        if (dh == 0 && dv == 0) continue;
        if (!have_step) {
            step_dh = dh;
            step_dv = dv;
            have_step = true;
        } else if (step_dh != dh || step_dv != dv) {
            step_dh = 0;
            step_dv = 0;
            break;
        }
    }
    *out_dh = step_dh;
    *out_dv = step_dv;
}

// Sampled mean |prev − cur| over an RGB frame (stride 4 in x and y,
// 0..255 per channel). The runtime vetoes the midpoint when this is
// ~0 while the register step is nonzero: a scroll rebase that leaves
// the render untouched (compensated on all layers) has no visual
// motion to interpolate, and shifting would fabricate a frame the
// game never displayed.
inline float frame_mean_abs_diff(const uint8_t* prev, const uint8_t* cur,
                                 uint32_t w, uint32_t h) {
    if (prev == nullptr || cur == nullptr || w == 0 || h == 0) return 0.0f;
    std::uint64_t total = 0;
    std::uint32_t samples = 0;
    for (uint32_t y = 0; y < h; y += 4) {
        const uint8_t* p = prev + static_cast<std::size_t>(y) * w * 3u;
        const uint8_t* c = cur + static_cast<std::size_t>(y) * w * 3u;
        for (uint32_t x = 0; x < w; x += 4) {
            const std::size_t o = static_cast<std::size_t>(x) * 3u;
            const int32_t dp = p[o] > c[o] ? p[o] - c[o] : c[o] - p[o];
            const int32_t dg = p[o + 1u] > c[o + 1u] ? p[o + 1u] - c[o + 1u]
                                                     : c[o + 1u] - p[o + 1u];
            const int32_t db = p[o + 2u] > c[o + 2u] ? p[o + 2u] - c[o + 2u]
                                                     : c[o + 2u] - p[o + 2u];
            total += static_cast<std::uint64_t>(dp + dg + db);
            ++samples;
        }
    }
    return samples == 0
               ? 0.0f
               : static_cast<float>(total) / static_cast<float>(samples * 3u);
}

// Half-pixel screen shift for the midpoint: dst[x,y] samples src at
// (x + sh_h/2, y + sh_v/2). Integer bilinear, edge-clamped (the
// uncovered 1-2 px strip replicates the edge — invisible under motion).
// A camera step of +d (content moved d px against the scroll direction)
// midpoints with sh = -d.
inline void shift_blit(const uint8_t* src, uint8_t* dst, uint32_t w,
                       uint32_t h, int32_t sh_h, int32_t sh_v) {
    for (uint32_t y = 0; y < h; ++y) {
        int32_t sy = static_cast<int32_t>(y) * 2 + sh_v;
        int32_t y0 = sy / 2;
        int32_t fy = sy % 2;
        if (fy < 0) {
            fy += 2;
            y0 -= 1;
        }
        if (y0 < 0) {
            y0 = 0;
            fy = 0;
        }
        if (y0 >= static_cast<int32_t>(h) - 1) {
            y0 = static_cast<int32_t>(h) - 1;
            fy = 0;
        }
        const int32_t y1 =
            y0 + 1 >= static_cast<int32_t>(h) ? y0 : y0 + 1;
        for (uint32_t x = 0; x < w; ++x) {
            int32_t sx = static_cast<int32_t>(x) * 2 + sh_h;
            int32_t x0 = sx / 2;
            int32_t fx = sx % 2;
            if (fx < 0) {
                fx += 2;
                x0 -= 1;
            }
            if (x0 < 0) {
                x0 = 0;
                fx = 0;
            }
            if (x0 >= static_cast<int32_t>(w) - 1) {
                x0 = static_cast<int32_t>(w) - 1;
                fx = 0;
            }
            const int32_t x1 =
                x0 + 1 >= static_cast<int32_t>(w) ? x0 : x0 + 1;
            const uint8_t* p00 =
                src + (static_cast<std::size_t>(y0) * w + x0) * 3u;
            const uint8_t* p10 =
                src + (static_cast<std::size_t>(y0) * w + x1) * 3u;
            const uint8_t* p01 =
                src + (static_cast<std::size_t>(y1) * w + x0) * 3u;
            const uint8_t* p11 =
                src + (static_cast<std::size_t>(y1) * w + x1) * 3u;
            uint8_t* d = dst + (static_cast<std::size_t>(y) * w + x) * 3u;
            const int32_t w00 = (2 - fx) * (2 - fy);
            const int32_t w10 = fx * (2 - fy);
            const int32_t w01 = (2 - fx) * fy;
            const int32_t w11 = fx * fy;
            for (int c = 0; c < 3; ++c) {
                const int32_t v =
                    w00 * p00[c] + w10 * p10[c] + w01 * p01[c] + w11 * p11[c];
                d[c] = static_cast<uint8_t>((v + 2) / 4);
            }
        }
    }
}

// HUD pin: pixels byte-identical between the previous and current guest
// frames are screen-locked chrome (menus, panels, HUD) — keep them exact
// instead of shifting them with the world. `mid` enters holding the
// shifted image and leaves with static pixels restored from `cur`
// (in-place safe). Flat world areas match either way, so the compare
// costs nothing visually where it fires spuriously.
inline void select_static(const uint8_t* prev, const uint8_t* cur,
                          uint8_t* mid, std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; i += 3u) {
        if (prev[i] == cur[i] && prev[i + 1u] == cur[i + 1u] &&
            prev[i + 2u] == cur[i + 2u]) {
            mid[i] = cur[i];
            mid[i + 1u] = cur[i + 1u];
            mid[i + 2u] = cur[i + 2u];
        }
    }
}

}  // namespace interp_shift

// Read an entire file into memory. Returns empty vector + sets `error`
// on failure. Used for ROM and BIOS loading.
std::vector<unsigned char> read_file(const std::string& path,
                                     std::string* error = nullptr);

// ── FramePacer ─────────────────────────────────────────────────────
// Wall-clock frame limiter. Emulation speed must be governed by the
// GBA's real frame rate (59.7275 Hz), NOT by the host monitor's refresh
// rate — relying on SDL's PRESENTVSYNC made the game run at monitor
// speed (e.g. 2.75x on a 164 Hz panel; see MinishCapRecomp MC-HP-004).
//
// Maintains a monotonic deadline advanced by exactly one GBA frame
// period each call. wait() blocks until that deadline using a hybrid
// sleep-then-spin (Windows' default ~15.6 ms timer granularity is too
// coarse for a 16.74 ms target, so the pacer raises timer resolution to
// 1 ms for its lifetime and spins the final sub-ms). If the host falls
// more than a frame behind, the deadline resyncs to now rather than
// trying to catch up (no death spiral). Uncapping enables fast-forward.
class FramePacer {
public:
    // GBA: 16'777'216 Hz / 280'896 cycles-per-frame = 59.7275 Hz.
    static constexpr double kGbaFrameHz = 16777216.0 / 280896.0;

    explicit FramePacer(double target_hz = kGbaFrameHz);
    ~FramePacer();

    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;

    // Block until the next frame's deadline. Call once per presented
    // frame. No-op while uncapped.
    //
    // `frames` is the banked-frame count for this present (normally 1).
    // A present can cover k>1 guest frames when stale-frame presents were
    // skipped (the present hook only presents on frame change): without
    // accounting, those banked frames escape throttling and the guest
    // wall-clock runs fast (measured +5% in battle, with audio-bridge
    // flood as the audible symptom). The deadline advances by the banked
    // count so one wait throttles all of them; frames==1 is exactly the
    // old behavior. Fast-forward presents one frame per wait already, so
    // it is unaffected. The behind-resync below still bounds real stalls.
    //
    // `tick`, when set, subdivides the wait into display-paced callbacks
    // (issue #9 3a: decoupled presentation clock). Each tick re-presents
    // the current frame and paces itself to the display (sleeping to its
    // display boundary, or riding the present's own vsync block where the
    // renderer already blocks), and receives the wait's deadline as
    // nanoseconds on the steady clock so
    // a slower panel can never hold the guest wait past it. The wait
    // returns exactly at the guest deadline, so guest time is throttled
    // identically with or without ticks — ticks only fill the would-be
    // idle span with extra presents. Null (default) is the historical
    // blocking wait, bit for bit. Uncapped and behind-schedule waits
    // never tick.
    using DisplayTickFn = std::function<void(uint64_t deadline_ns)>;
    void wait_for_next_frame(uint64_t frames = 1,
                             DisplayTickFn tick = nullptr);

    // Realign the deadline to "now". Call after any non-real-time jump
    // (startup, save-state load) so the pacer doesn't try to catch up.
    void reset();

    // Fast-forward toggle. While uncapped, wait_for_next_frame() returns
    // immediately and the deadline tracks now.
    void set_uncapped(bool on) { uncapped_ = on; }
    bool uncapped() const { return uncapped_; }

private:
    using clock = std::chrono::steady_clock;
    std::chrono::nanoseconds period_{};
    clock::time_point next_{};
    bool uncapped_ = false;
    bool raised_timer_res_ = false;
    // HP-002: Windows high-resolution waitable timer (HANDLE, void* here to
    // keep windows.h out of this header). sleep_until quantizes to the 64 Hz
    // system tick under load; the phase drift between that grid and the
    // 16.742 ms frame period produced a measured 234 ms beat of 20-24 ms
    // oversleeps (52/75 late frames in the frame-phase ring). The hi-res
    // timer waits on the exact deadline instead; null = pre-1803 fallback
    // to the old hybrid sleep.
    void* wait_timer_ = nullptr;
};

}  // namespace gbarecomp
