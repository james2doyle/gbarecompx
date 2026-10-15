// long_press_ring_test.cpp — Phase 3d: the engine long-press cue (plan §3.4).
//
// Drives the pure ring collector + drawing over a RECORDING HostOverlay. The
// semantic source is the SDL backend's draw_engine_long_press: rings for held
// unmoved pointers, hidden for the first 150 ms, progress = elapsed/hold,
// drawn as a 7 mm arc sweeping clockwise from 12 o'clock in light blue.
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG; see BRINGUP
// "vacuous test" finding.
#include "long_press_ring.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace {

using gbarecomp::HostOverlay;
using gbarecomp::LongPressRing;
using gbarecomp::OverlayAlign;
using gbarecomp::OverlayColor;
using gbarecomp::OverlayPoint;
using gbarecomp::OverlaySpace;
using gbarecomp::TouchPointSnapshot;
using gbarecomp::PresentationLayout;

// A HostOverlay that records every call (the plan's recording-overlay
// pattern). Geometry: 720x480 drawable, 240x160 view.
class RecordingOverlay final : public HostOverlay {
public:
    int drawable_width() const override { return 720; }
    int drawable_height() const override { return 480; }
    PresentationLayout game_rect() const override {
        PresentationLayout l;
        l.x = 60;
        l.y = 0;
        l.width = 600;
        l.height = 480;
        return l;
    }
    int view_width() const override { return 240; }
    int view_height() const override { return 160; }
    float drawable_px_per_mm() const override { return 3.78f; }
    Insets safe_insets() const override { return Insets{}; }
    std::uint32_t host_ms() const override { return 0; }
    void to_drawable(float x, float y, OverlaySpace, float* dx,
                     float* dy) const override {
        *dx = x;
        *dy = y;
    }

    void line(float, float, float, float, float, OverlayColor,
              OverlaySpace) override {}
    void polyline(const OverlayPoint*, std::size_t, float, OverlayColor,
                  OverlaySpace) override {}
    void fill_rect(float, float, float, float, float, OverlayColor,
                   OverlaySpace) override {}
    void stroke_rect(float, float, float, float, float, float, OverlayColor,
                     OverlaySpace) override {}
    void fill_circle(float, float, float, OverlayColor, OverlaySpace) override {}
    void stroke_circle(float, float, float, float, OverlayColor,
                       OverlaySpace) override {}
    void arc(float cx, float cy, float radius, float thickness, float start,
             float end, OverlayColor color, OverlaySpace space) override {
        ++arc_calls;
        last_cx = cx;
        last_cy = cy;
        last_radius = radius;
        last_thickness = thickness;
        last_start = start;
        last_end = end;
        last_color = color;
        last_space = space;
    }
    bool text_supported() const override { return true; }
    void text(float, float, float, OverlayColor, const char*, OverlayAlign,
              OverlaySpace) override {}
    float text_width(const char*, float, OverlaySpace) const override {
        return 0.0f;
    }

    int arc_calls = 0;
    float last_cx = 0, last_cy = 0, last_radius = 0, last_thickness = 0;
    float last_start = 0, last_end = 0;
    OverlayColor last_color{};
    OverlaySpace last_space = OverlaySpace::View;
};

TouchPointSnapshot point(float x, float y, std::uint32_t down_ms) {
    TouchPointSnapshot p;
    p.drawable_x = x;
    p.drawable_y = y;
    p.down_ms = down_ms;
    return p;
}

void collect_visibility_rules() {
    LongPressRing out[4];

    // Nothing to collect from an empty frame.
    CHECK(gbarecomp::collect_long_press_rings(nullptr, 0, 1000, 650, out, 4) ==
          0);

    // A fresh press does not flash a ring: hidden for the first 150 ms.
    TouchPointSnapshot early = point(10, 20, 900);
    CHECK(gbarecomp::collect_long_press_rings(&early, 1, 1049, 650, out, 4) ==
          0);
    CHECK(gbarecomp::collect_long_press_rings(&early, 1, 1050, 650, out, 4) ==
          1);

    // A moved pointer is disqualified for good.
    TouchPointSnapshot moved = point(10, 20, 900);
    moved.moved = true;
    CHECK(gbarecomp::collect_long_press_rings(&moved, 1, 1400, 650, out, 4) ==
          0);

    // A fired long press stops ringing.
    TouchPointSnapshot fired = point(10, 20, 900);
    fired.long_pressed = true;
    CHECK(gbarecomp::collect_long_press_rings(&fired, 1, 1400, 650, out, 4) ==
          0);

    // Disabled timing (hold == 0) never rings.
    CHECK(gbarecomp::collect_long_press_rings(&early, 1, 1400, 0, out, 4) == 0);
}

void collect_progress_and_order() {
    LongPressRing out[4];
    TouchPointSnapshot pts[3] = {
        point(100, 200, 1000),  // 325 ms held -> 0.5 of 650
        point(300, 400, 0),     // 1000 ms held -> clamped to 1.0
        point(500, 600, 1000),  // also 0.5
    };
    CHECK(gbarecomp::collect_long_press_rings(pts, 3, 1325, 650, out, 4) == 3);
    CHECK(out[0].x == 100 && out[0].y == 200);
    CHECK(std::fabs(out[0].progress - 0.5f) < 1e-4f);
    CHECK(out[1].x == 300 && std::fabs(out[1].progress - 1.0f) < 1e-4f);
    CHECK(out[2].x == 500 && std::fabs(out[2].progress - 0.5f) < 1e-4f);

    // max_out is respected.
    CHECK(gbarecomp::collect_long_press_rings(pts, 3, 1325, 650, out, 2) == 2);

    // The elapsed window uses the same wrap-safe arithmetic as the hub:
    // a pointer that went down just before the 32-bit clock wrapped.
    LongPressRing one[1];
    TouchPointSnapshot wrapped = point(7, 8, 0xFFFFFF00u);
    CHECK(gbarecomp::collect_long_press_rings(&wrapped, 1, 0x00000100u, 650,
                                              one, 1) == 1);
    CHECK(std::fabs(one[0].progress - 512.0f / 650.0f) < 1e-4f);
}

void draw_geometry_matches_sdl() {
    RecordingOverlay ov;
    LongPressRing ring;
    ring.x = 123.5f;
    ring.y = 456.5f;
    ring.progress = 0.25f;
    gbarecomp::draw_long_press_ring(ov, ring, 4.0f);

    CHECK(ov.arc_calls == 1);
    CHECK(ov.last_cx == 123.5f && ov.last_cy == 456.5f);
    CHECK(std::fabs(ov.last_radius - 28.0f) < 1e-4f);     // 7 mm * 4
    CHECK(std::fabs(ov.last_thickness - 4.8f) < 1e-4f);   // 1.2 mm * 4
    CHECK(ov.last_start == 0.0f && std::fabs(ov.last_end - 0.25f) < 1e-4f);
    CHECK(ov.last_color.r == 80 && ov.last_color.g == 210 &&
          ov.last_color.b == 255 && ov.last_color.a == 200);
    CHECK(ov.last_space == OverlaySpace::Drawable);

    // Full progress draws a complete turn.
    RecordingOverlay full;
    ring.progress = 1.0f;
    gbarecomp::draw_long_press_ring(full, ring, 4.0f);
    CHECK(full.arc_calls == 1 && full.last_end == 1.0f);
}

}  // namespace

int main() {
    collect_visibility_rules();
    collect_progress_and_order();
    draw_geometry_matches_sdl();

    std::printf("long_press_ring tests: PASS\n");
    return 0;
}
