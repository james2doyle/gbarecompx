// Phase 2 — V2 concern contract: Video (plan §3.2 / §3.5, concern step 1).
//
// Drives the V2 `Video` interface through RaylibBackend::video() and checks:
//   * one state, two views — the flat accessors agree with the V2 ones
//     (window_scale() == video().scale(), etc.);
//   * display-free contract — defaults, clamps, and safe no-ops on an
//     unopened backend, in BOTH configs (raylib ON and stub).
//
// Window-backed behavior (present/readback pixels, fullscreen state changes)
// is exercised by the FFTA app tests + attract; this file must pass without a
// display.
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG; see BRINGUP
// "vacuous test" finding.
#include "raylib_backend.h"
#include "window_backend.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace {

using gbarecomp::RaylibBackend;
using gbarecomp::Video;

void video_defaults_clamps_and_shared_state() {
    RaylibBackend b;
    Video& v = b.video();

    // The accessor is stable: same object every call (state must not be
    // per-call).
    CHECK(&b.video() == &v);

    // Defaults mirror the flat generation's.
    CHECK(v.scale() == 3);
    CHECK(!v.linear_filter());
    CHECK(v.fullscreen() == 0);
    CHECK(!v.fps_readout());
    CHECK(v.px_per_mm() > 3.7f && v.px_per_mm() < 3.9f);  // 96/25.4 ≈ 3.7795

    // One state, two views: mutating through V2 is visible through the flat
    // generation and vice versa.
    v.adjust_scale(2);
    CHECK(v.scale() == 5);
    CHECK(b.window_scale() == 5);

    v.adjust_scale(100);
    CHECK(v.scale() == 8);  // clamped high
    v.adjust_scale(-100);
    CHECK(v.scale() == 1);  // clamped low

    v.set_linear_filter(true);
    CHECK(v.linear_filter());
    CHECK(b.linear_filter_enabled());

    v.set_fullscreen(9);
    CHECK(v.fullscreen() == 2);  // clamped high
    CHECK(b.fullscreen_mode() == 2);
    v.set_fullscreen(-3);
    CHECK(v.fullscreen() == 0);  // clamped low
    CHECK(b.fullscreen_mode() == 0);

    v.set_fps_readout(true);
    CHECK(v.fps_readout());
    CHECK(b.fps_readout_value());
}

void video_safe_without_a_window() {
    RaylibBackend b;
    Video& v = b.video();

    // Unopened: display-touching calls must be safe no-ops/false, never crash.
    CHECK(!v.drawable_size(nullptr, nullptr));
    int w = 7, h = 9;
    CHECK(!v.drawable_size(&w, &h));

    std::vector<std::uint8_t> frame;
    CHECK(!v.readback(frame, w, h));
    CHECK(frame.empty());

    // present/interp on an unopened backend: no-op, no crash, nothing
    // recorded for readback.
    std::vector<std::uint8_t> px(240 * 160 * 3, 0x11);
    v.present(px.data(), true);
    v.interp_display_tick(px.data(), 1000u);
    CHECK(!v.readback(frame, w, h));

    // State-only setters are safe unopened.
    v.set_title("v2 video test");
    v.set_resize_driven_view(true);
    v.set_view_margins(1, 2, 3, 4);
    v.set_presentation_anchor_top(true);

    // Surface size: rejects nonsense, accepts changes, idempotent on repeat.
    CHECK(!v.set_surface_size(0, 160));
    CHECK(!v.set_surface_size(240, -1));
    CHECK(v.set_surface_size(240, 160));  // same as default: true, no-op
    CHECK(v.set_surface_size(256, 192));  // valid change
    CHECK(v.set_surface_size(256, 192));  // idempotent
}

void video_refresh_hz_estimate() {
    RaylibBackend b;
    // Default period is the 60 Hz placeholder (interp_period_ns = 16666666
    // until the open-time probe); refresh_hz reports it. Its *meaningfulness*
    // after open is Caps::vsync_report's job.
    const float hz = b.video().refresh_hz();
    CHECK(hz > 59.9f && hz < 60.1f);
}

}  // namespace

int main() {
    video_defaults_clamps_and_shared_state();
    video_safe_without_a_window();
    video_refresh_hz_estimate();

    std::printf("raylib_backend_v2 tests: PASS\n");
    return 0;
}
