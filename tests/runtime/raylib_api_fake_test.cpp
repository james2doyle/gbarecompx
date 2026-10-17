// raylib_api_fake_test.cpp — Phase 5.1, §5.2 item 4 (red-first).
//
// Failure injection through a fake RaylibApi: InitWindow fails ->
// Err::Window; audio device unavailable -> claim() refuses; shader load
// fails -> Fx values still stored (capability rule); monitor/refresh edge
// -> 60 Hz fallback; queue-only Delete press -> Menu edge fires once.
//
// Runs against the real RaylibBackend with the injected api, no display.
// Stub config (GBARECOMP_USE_RAYLIB off): RaylibApi does not exist — PASS
// without doing anything.
//
// RED STATE: includes support/fake_raylib_api.h, which does not exist yet.
// The build must fail until the fake lands.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Included unconditionally so the RED state (missing header) fails in every
// config, not just raylib-ON: the stub branch below never uses the fake.
#include "support/fake_raylib_api.h"

// Variadic so a call containing commas inside parentheses (the grid compares
// below) still arrives as one expression.
#define CHECK(...)                                                           \
    do {                                                                     \
        if (!(__VA_ARGS__)) {                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                          __LINE__, #__VA_ARGS__);                            \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

#if !defined(GBARECOMP_USE_RAYLIB)

int main() {
    std::printf("raylib_api_fake tests: PASS (stub build, nothing to inject)\n");
    return 0;
}

#else

#include "raylib_api.h"
#include "raylib_backend.h"
#include "window_backend.h"

using gbarecomp::Caps;
using gbarecomp::Err;
using gbarecomp::FxSettings;
using gbarecomp::InputSnapshot;
using gbarecomp::OpenParams;
using gbarecomp::RaylibBackend;
using gbarecomp::testing::FakeRaylibApi;

// 1. InitWindow fails -> open() reports Err::Window.
void init_window_failure_is_a_window_error() {
    FakeRaylibApi fake;
    fake.window_ready = false;  // InitWindow never brings the window up
    RaylibBackend b(&fake, nullptr);
    const auto r = b.open(OpenParams{});
    CHECK(!r.ok() && r.code == Err::Window && r.detail != nullptr);
    CHECK(!b.is_open());
}

// 2. Audio device unavailable -> claim() refuses.
void audio_unavailable_refuses_claim() {
    FakeRaylibApi fake;  // audio_ready defaults to false: no device
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    const auto r = b.audio().claim();
    CHECK(!r.ok() && r.code == Err::AudioDevice);
    CHECK(!b.caps().audio);
    b.close();
}

// 3. Shader load fails -> Fx values are still stored (capability rule:
// values are stored and reported even when unsupported).
void shader_failure_still_stores_fx_values() {
    FakeRaylibApi fake;
    fake.shader_valid = false;
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    FxSettings s = b.fx().settings();
    s.lcd = true;
    s.lcd_boost = 1.25f;
    b.fx().apply(s);
    CHECK(b.fx().settings().lcd);
    CHECK(b.fx().settings().lcd_boost == 1.25f);
    b.close();
}

// 4. Monitor/refresh edge (no monitor, 0 Hz) -> 60 Hz fallback.
void monitor_edge_falls_back_to_60hz() {
    FakeRaylibApi fake;
    fake.monitor_count = 0;
    fake.refresh_hz = 0;
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    const float hz = b.video().refresh_hz();
    CHECK(hz > 59.9f && hz < 60.1f);
    CHECK(!b.caps().vsync_report);
    b.close();
}

// 5. Queue-only Delete press (down==false, queue holds Delete) forms the
// Menu edge exactly once — the Slice 0 queue-drain regression guard.
void queue_only_delete_press_fires_menu_edge_once() {
    FakeRaylibApi fake;
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    fake.key_down_held = false;
    fake.key_pressed_held = false;
    fake.press_queue_once = 261;  // KEY_DELETE, no level anywhere
    auto ev1 = b.pump();
    CHECK(ev1.toggle_menu);
    auto ev2 = b.pump();
    CHECK(!ev2.toggle_menu);
    b.close();
}

// 6. Window scale resizes the OS window (SDL parity): adjust_scale(+1)
// from the default 3x240x160 opens at 720x480, so +1 must request 960x640;
// while fullscreen the request is meaningless and must not resize.
void window_scale_resizes_the_window() {
    FakeRaylibApi fake;
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    CHECK(b.video().scale() == 3);
    b.video().adjust_scale(1);
    CHECK(b.video().scale() == 4);
    CHECK(fake.set_window_size_calls == 1);
    CHECK(fake.last_window_w == 240 * 4 && fake.last_window_h == 160 * 4);
    b.video().set_fullscreen(1);
    b.video().adjust_scale(-1);
    CHECK(b.video().scale() == 4);  // unchanged while fullscreen
    CHECK(fake.set_window_size_calls == 1);
    b.close();
}

// 7. Fullscreen drives the window state (not just the stored mode).
void fullscreen_drives_window_state() {
    FakeRaylibApi fake;
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    b.video().set_fullscreen(2);
    CHECK(b.video().fullscreen() == 2);
    CHECK(fake.set_window_state_calls == 1);
    b.video().set_fullscreen(0);
    CHECK(fake.clear_window_state_calls == 1);
    b.close();
}

// 8. Linear filter reaches the texture (not just the stored flag).
void linear_filter_reaches_texture() {
    FakeRaylibApi fake;
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    const int calls_before = fake.set_texture_filter_calls;
    b.video().set_linear_filter(true);
    CHECK(b.video().linear_filter());
    CHECK(fake.set_texture_filter_calls == calls_before + 1);
    CHECK(fake.last_texture_filter == TEXTURE_FILTER_BILINEAR);
    b.video().set_linear_filter(false);
    CHECK(fake.last_texture_filter == TEXTURE_FILTER_POINT);
    b.close();
}

// 9. The game-grid uniforms the scale-aware passes depend on are uploaded
// with the CURRENT layout's origin and scale, and recomputed on resize.
//
// This is the wiring half of the LCD-grid change: lcd_shader_test proves the
// mask is right given a grid, and this proves the backend hands the shader the
// right grid. A fake that returned -1 for every lookup (its old behaviour)
// could not observe it at all, so the uniform recording is the enabler.
//
// The negative controls matter: a stale uGrid (from the previous layout) must
// fail, or the test would pass on a resize bug that shipped as "the grid slides
// when you resize the window".
void grid_uniforms_track_the_layout() {
    FakeRaylibApi fake;
    fake.screen_w = 1920;
    fake.screen_h = 1280;  // exact 8x, image at the origin
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    FxSettings s = b.fx().settings();
    s.lcd = true;
    s.lcd_dither = 1.0f;
    b.fx().apply(s);
    CHECK(b.fx().settings().lcd);

    std::vector<unsigned char> frame(240 * 160 * 3u, 128u);
    b.video().present(frame.data(), false);

    float grid[4] = {0, 0, 0, 0};
    CHECK(fake.uniform_value("uGrid", grid, 4),
          "the grid program resolved and uploaded uGrid");
    CHECK(grid[0] == 0.0f && grid[1] == 0.0f);
    CHECK(std::fabs(grid[2] - 8.0f) < 1e-4f &&
          std::fabs(grid[3] - 8.0f) < 1e-4f);  // 8 drawable px per game px
    float size[2] = {0, 0};
    CHECK(fake.uniform_value("uGameSize", size, 2));
    CHECK(size[0] == 240.0f && size[1] == 160.0f);  // logical framebuffer

    // Resize to 1080p: the image is 1620x1080 at x=150 -- offset AND
    // fractional. The upload must follow within the same drawable.
    fake.screen_w = 1920;
    fake.screen_h = 1080;
    b.video().present(frame.data(), false);
    CHECK(fake.uniform_value("uGrid", grid, 4));
    CHECK(grid[0] == 150.0f && grid[1] == 0.0f);  // letterbox offset
    CHECK(std::fabs(grid[2] - 6.75f) < 1e-4f &&
          std::fabs(grid[3] - 6.75f) < 1e-4f);   // fractional scale
    b.close();
}

// 10. Both scale-aware programs get the grid: the dither cell is indexed by
// game pixel too, so a grid on the LCD program alone would leave the dither
// still screen-space. Render targets must exist for the dither pass to run at
// all (id 0 silently degrades it), which is why the fake's default changed.
void dither_program_gets_the_grid_too() {
    FakeRaylibApi fake;
    fake.screen_w = 960;
    fake.screen_h = 640;  // 4x
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    FxSettings s = b.fx().settings();
    s.lcd_dither = 1.0f;
    b.fx().apply(s);
    std::vector<unsigned char> frame(240 * 160 * 3u, 128u);
    b.video().present(frame.data(), false);
    CHECK(fake.uniform_written("uGrid"));  // dither-only path
    float grid[4] = {0, 0, 0, 0};
    CHECK(fake.uniform_value("uGrid", grid, 4));
    CHECK(std::fabs(grid[2] - 4.0f) < 1e-4f);  // dither sees the same scale
    b.close();
}

// 11. A driver that refuses every uniform lookup must not crash or write
// garbage: the backend guards each upload on loc >= 0, and that is the whole
// reason the guards exist. Nothing should be recorded as named-written.
void unknown_uniform_locations_degrade_cleanly() {
    FakeRaylibApi fake;
    fake.unknown_shader_locations = true;
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    FxSettings s = b.fx().settings();
    s.lcd = true;
    b.fx().apply(s);
    std::vector<unsigned char> frame(240 * 160 * 3u, 128u);
    b.video().present(frame.data(), false);
    CHECK(!fake.uniform_written("uGrid"));  // lookups failed
    CHECK(b.fx().settings().lcd);  // request still stands (capability rule)
    b.close();
}

// 12. Every pass off -> no grid uploads at all. With nothing running the frame
// must stay byte-identical to the pre-shader path, and the uniforms are part
// of that contract: nothing is written when nothing runs.
//
// Note the dither is ON by default (kLcdDefaultDither = 1.0), so it has to be
// turned off explicitly here -- which is itself worth pinning: with the
// dither left at its default, uGrid IS written, because the dither pass now
// needs the grid too.
void all_passes_off_writes_no_grid() {
    FakeRaylibApi fake;
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    FxSettings s = b.fx().settings();
    s.lcd = false;
    s.lcd_dither = 0.0f;
    s.lcd_deband = 0.0f;
    b.fx().apply(s);
    std::vector<unsigned char> frame(240 * 160 * 3u, 128u);
    b.video().present(frame.data(), false);
    CHECK(!fake.uniform_written("uGrid"));
    CHECK(!fake.uniform_written("uGameSize"));
    CHECK(fake.uniform_writes == 0);  // no program is even current
    b.close();
}

// 13. The border pixel grid (LADXHD PixelGrid port) rides the LCD program:
// border-only runs the program with uMaskMode=1, grille+border is 2, and the
// opacity flows through uGridAlpha. The grid uniforms ride along (the border
// is anchored to the same PresentGrid).
void border_grid_uploads_mask_mode_and_alpha() {
    FakeRaylibApi fake;
    fake.screen_w = 960;
    fake.screen_h = 640;  // 4x, origin
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());

    // Border only: the LCD program runs, mode 1.
    FxSettings s = b.fx().settings();
    s.lcd = false;
    s.lcd_border = true;
    s.lcd_grid_alpha = 0.5f;
    s.lcd_dither = 0.0f;
    s.lcd_deband = 0.0f;
    b.fx().apply(s);
    CHECK(b.fx().settings().lcd_border);
    std::vector<unsigned char> frame(240 * 160 * 3u, 128u);
    b.video().present(frame.data(), false);
    int mode = 0;
    CHECK(fake.uniform_value_int("uMaskMode", &mode, 1) && mode == 1,
          "border-only uploads uMaskMode=1");
    float alpha = 0.0f;
    CHECK(fake.uniform_value("uGridAlpha", &alpha, 1) &&
              std::fabs(alpha - 0.5f) < 1e-4f,
          "border opacity reaches uGridAlpha");
    CHECK(fake.uniform_written("uGrid"),
          "the border is anchored to the game grid too");

    // Grille + border: mode 2.
    s.lcd = true;
    b.fx().apply(s);
    b.video().present(frame.data(), false);
    CHECK(fake.uniform_value_int("uMaskMode", &mode, 1) && mode == 2,
          "grille+border uploads uMaskMode=2");

    // Grille only (the pre-existing path): mode 0 — the regression pin for
    // the shared program's default.
    s.lcd_border = false;
    b.fx().apply(s);
    b.video().present(frame.data(), false);
    CHECK(fake.uniform_value_int("uMaskMode", &mode, 1) && mode == 0,
          "grille-only uploads uMaskMode=0");
    b.close();
}

// 14. Sharp scaling: at a fractional layout the prescale engages and the
// finish is BILINEAR (prescale+bilinear classic — the >3x uneven-pixel fix).
// The fake records the last filter set, and the prescale's target is the
// last texture filtered inside this present (no later call happens).
void sharp_prescale_finishes_bilinear() {
    FakeRaylibApi fake;
    fake.screen_w = 1920;
    fake.screen_h = 1080;  // scale 6.75 -> factor 6, remainder 1.125
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    // Linear first so the mutual exclusion has something to clear.
    b.video().set_linear_filter(true);
    FxSettings s = b.fx().settings();
    s.sharp = true;
    s.lcd_dither = 0.0f;
    s.lcd_deband = 0.0f;
    b.fx().apply(s);
    CHECK(b.fx().settings().sharp);
    CHECK(!b.video().linear_filter(),
          "enabling sharp cleared the linear filter");
    std::vector<unsigned char> frame(240 * 160 * 3u, 128u);
    b.video().present(frame.data(), false);
    CHECK(fake.load_render_texture_calls >= 1,
          "the sharp prescale allocated its render target");
    CHECK(fake.last_texture_filter == TEXTURE_FILTER_BILINEAR,
          "the sharp finish is bilinear, not POINT");
    b.close();
}

// 15. MMPX edge smoothing: at a fractional layout the 2x RT is allocated
// and the uSrc uniform carries the LOGICAL size (the shader's tap space);
// below 2x the pass is inert (no allocation).
void mmpx_pass_allocates_and_uploads_src() {
    FakeRaylibApi fake;
    fake.screen_w = 1920;
    fake.screen_h = 1080;  // scale 6.75 -> engages
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    FxSettings s = b.fx().settings();
    s.edge_smoothing = true;
    s.lcd_dither = 0.0f;
    s.lcd_deband = 0.0f;
    b.fx().apply(s);
    CHECK(b.fx().settings().edge_smoothing);
    std::vector<unsigned char> frame(240 * 160 * 3u, 128u);
    b.video().present(frame.data(), false);
    CHECK(fake.load_render_texture_calls >= 1,
          "the MMPX pass allocated its 2x render target");
    float src_size[2] = {0, 0};
    CHECK(fake.uniform_value("uSrc", src_size, 2),
          "the MMPX pass uploaded uSrc");
    CHECK(src_size[0] == 240.0f && src_size[1] == 160.0f,
          "uSrc is the LOGICAL size (tap space), not the 2x target");
    b.close();
}

// 16. MMPX below 2x display scale: inert (no target allocated).
void mmpx_inert_below_2x() {
    FakeRaylibApi fake;
    fake.screen_w = 360;
    fake.screen_h = 240;  // 1.5x
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    FxSettings s = b.fx().settings();
    s.edge_smoothing = true;
    s.lcd_dither = 0.0f;
    s.lcd_deband = 0.0f;
    b.fx().apply(s);
    std::vector<unsigned char> frame(240 * 160 * 3u, 128u);
    b.video().present(frame.data(), false);
    CHECK(fake.load_render_texture_calls == 0,
          "MMPX stays inert below a 2x display");
    b.close();
}

int main() {
    init_window_failure_is_a_window_error();
    audio_unavailable_refuses_claim();
    shader_failure_still_stores_fx_values();
    monitor_edge_falls_back_to_60hz();
    queue_only_delete_press_fires_menu_edge_once();
    window_scale_resizes_the_window();
    fullscreen_drives_window_state();
    linear_filter_reaches_texture();
    grid_uniforms_track_the_layout();
    dither_program_gets_the_grid_too();
    unknown_uniform_locations_degrade_cleanly();
    all_passes_off_writes_no_grid();
    border_grid_uploads_mask_mode_and_alpha();
    sharp_prescale_finishes_bilinear();
    mmpx_pass_allocates_and_uploads_src();
    mmpx_inert_below_2x();

    std::printf("raylib_api_fake tests: PASS\n");
    return 0;
}

#endif  // defined(GBARECOMP_USE_RAYLIB)
