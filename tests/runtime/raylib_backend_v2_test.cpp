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
#include "raylib_keymap.h"
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

using gbarecomp::Audio;
using gbarecomp::Caps;
using gbarecomp::Err;
using gbarecomp::Events;
using gbarecomp::Fx;
using gbarecomp::FxSettings;
using gbarecomp::Input;
using gbarecomp::InputSnapshot;
using gbarecomp::kRayKeyX;
using gbarecomp::OpenParams;
using gbarecomp::Overlay;
using gbarecomp::RaylibBackend;
using gbarecomp::Result;
using gbarecomp::TouchConfig;
using gbarecomp::Video;
using gbarecomp::WindowBackendV2;

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

    // ...and the other direction: the flat setters must be visible through V2
    // (a broken flat forwarder would otherwise pass every check above).
    b.set_linear_filter(false);
    CHECK(!v.linear_filter());
    b.adjust_scale(1);
    CHECK(v.scale() == 2);
    b.set_fullscreen(1);
    CHECK(v.fullscreen() == 1);
    b.set_fps_readout(false);
    CHECK(!v.fps_readout());
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

    // The flat side of the same paths (forwarders): same no-ops, same state.
    CHECK(b.px_per_mm_value() > 3.7f && b.px_per_mm_value() < 3.9f);
    int fw = 0, fh = 0;
    CHECK(!b.drawable_size(&fw, &fh));
    std::vector<std::uint8_t> frame2;
    CHECK(!b.copy_presented_frame(frame2, fw, fh));
    b.present(px.data(), false);
    b.interp_display_tick(px.data(), 2000u);
    b.set_title("flat title");
    b.set_view_margins(0, 0, 0, 0);
    b.set_resize_driven_view(false);
    b.set_presentation_anchor_top(false);

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

void audio_defaults_and_shared_state() {
    RaylibBackend b;
    Audio& a = b.audio();

    // Stable accessor + defaults mirroring the flat generation's.
    CHECK(&b.audio() == &a);
    CHECK(a.volume() == 100);
    CHECK(a.enabled());
    CHECK(b.volume_percent() == 100);
    CHECK(b.audio_enabled());

    // One state, two views, both directions.
    a.set_volume(55);
    CHECK(a.volume() == 55);
    CHECK(b.volume_percent() == 55);
    a.set_volume(150);
    CHECK(a.volume() == 100);  // clamped high
    a.set_volume(-5);
    CHECK(a.volume() == 0);  // clamped low
    b.set_volume(80);
    CHECK(a.volume() == 80);

    a.set_enabled(false);
    CHECK(!a.enabled());
    CHECK(!b.audio_enabled());
    b.set_audio_enabled(true);
    CHECK(a.enabled());
}

void audio_safe_without_a_device() {
    RaylibBackend b;
    Audio& a = b.audio();

    // Unopened: there is no device to claim — claim() refuses with a
    // static-lifetime detail (Caps::audio stays false until open succeeds).
    // The already-claimed refusal needs a real device (or the Phase 5 fake).
    const Result r = a.claim();
    CHECK(!r.ok() && r.code == Err::AudioDevice && r.detail != nullptr);
    a.release();  // safe no-op: nothing was claimed

    // push() is the one cross-thread entry point; unopened it must be a safe
    // no-op (null / zero-count / real inputs alike).
    a.push(nullptr, 8);
    std::vector<std::int16_t> samples(64, 0);
    a.push(samples.data(), 0);
    a.push(samples.data(), samples.size());
    b.push_audio_samples(samples.data(), samples.size());
}

void input_defaults_and_shared_state() {
    RaylibBackend b;
    Input& in = b.input();
    CHECK(&b.input() == &in);

    // Defaults mirror the flat generation's.
    CHECK(in.assist_tools_enabled());
    CHECK(b.assist_tools_enabled());
    CHECK(in.fast_forward_multiplier() == 4);
    CHECK(b.fast_forward_multiplier() == 4);
    CHECK(!in.gamepad_connected());
    CHECK(!b.gamepad_connected());

    // bindings() is the live table (stable address; the ctor's default seed:
    // index 0 = A = X, per raylib_keymap.h).
    CHECK(&in.bindings() == &in.bindings());
    CHECK(in.bindings().gba_key[0] == kRayKeyX);

    // load_config(nullptr, ...) reseeds the defaults (assist tools + clamped
    // multiplier) without touching a file.
    in.load_config(nullptr, false, 7);
    CHECK(!in.assist_tools_enabled());
    CHECK(!b.assist_tools_enabled());
    CHECK(in.fast_forward_multiplier() == 7);
    in.load_config(nullptr, true, 99);
    CHECK(in.assist_tools_enabled());
    CHECK(in.fast_forward_multiplier() == 10);  // clamped
}

void input_safe_without_a_window() {
    RaylibBackend b;
    Input& in = b.input();

    // read() unopened resets the snapshot to all-up levels.
    InputSnapshot s;
    s.mods = 0xFFFFu;
    s.close_requested = true;
    s.pad_connected = true;
    s.hold(kRayKeyX);
    in.read(s);
    CHECK(s.mods == 0u);
    CHECK(!s.close_requested);
    CHECK(!s.pad_connected);
    CHECK(s.held_key_count == 0);

    // The flat pump composes the same way and stays quiet unopened.
    const Events ev = b.pump();
    CHECK(!ev.fast_forward);
    CHECK(!ev.quit);
    CHECK(ev.save_slot == 0 && ev.load_slot == 0);
    CHECK(ev.keyinput == 0x03FF);

    // rumble/haptic are safe no-ops without a pad.
    in.rumble(0.5f, 0.5f, 100);
    in.haptic_pulse(50, 1.0f);
    b.gamepad_rumble(0.5f, 0.5f, 100);
    b.haptic_pulse(50, 1.0f);
}

void fx_defaults_roundtrip_and_shared_state() {
    RaylibBackend b;
    Fx& f = b.fx();
    CHECK(&b.fx() == &f);

    // Defaults mirror the flat generation's.
    const FxSettings d = f.settings();
    CHECK(!d.frame_interp);
    CHECK(d.frame_blend == 0.0f);
    CHECK(!d.lcd);
    CHECK(d.lcd_boost == 1.0f && d.lcd_dither == 1.0f && d.lcd_deband == 0.0f);

    // Flat setters -> settings() (V2), both directions.
    b.set_frame_interp(true);
    CHECK(f.settings().frame_interp);
    CHECK(b.frame_interp());
    b.set_frame_blend(true);
    CHECK(f.settings().frame_blend == 1.0f);
    CHECK(b.frame_blend_value());
    f.apply(FxSettings{});  // defaults: toggles both back off
    CHECK(!f.settings().frame_interp);
    CHECK(f.settings().frame_blend == 0.0f);

    // A full snapshot round-trips through settings(); the flat LCD getters
    // agree.
    FxSettings want;
    want.frame_interp = true;
    want.frame_blend = 1.0f;
    want.lcd = true;
    want.lcd_boost = 1.25f;
    want.lcd_dither = 0.5f;
    want.lcd_deband = 0.25f;
    f.apply(want);
    const FxSettings got = f.settings();
    CHECK(got.frame_interp == want.frame_interp);
    CHECK(got.frame_blend == want.frame_blend);
    CHECK(got.lcd == want.lcd);
    CHECK(got.lcd_boost == want.lcd_boost);
    CHECK(got.lcd_dither == want.lcd_dither);
    CHECK(got.lcd_deband == want.lcd_deband);
    CHECK(b.lcd_shader_enabled());
    CHECK(b.lcd_boost_value() == 1.25f);
    CHECK(b.lcd_dither_value() == 0.5f);
    CHECK(b.lcd_deband_value() == 0.25f);

    // Out-of-range values clamp like the flat setters (shared policy:
    // boost [1,3], dither [0,2], deband [0,16]).
    FxSettings wild;
    wild.lcd_boost = 99.0f;
    wild.lcd_dither = -5.0f;
    wild.lcd_deband = 99.0f;
    f.apply(wild);
    const FxSettings clamped = f.settings();
    CHECK(clamped.lcd_boost == 3.0f);
    CHECK(clamped.lcd_dither == 0.0f);
    CHECK(clamped.lcd_deband == 16.0f);

    // reset_frame_blend is a safe no-op unopened (no history yet), through
    // both views.
    f.reset_frame_blend();
    b.reset_frame_blend();
}

void overlay_defaults_and_shared_state() {
    RaylibBackend b;
    Overlay& o = b.overlay();
    CHECK(&b.overlay() == &o);

    // Fresh backend: no pad.
    CHECK(!o.touch_pad_visible());
    CHECK(!b.touch_pad_visible_value());

    // configure_touch stores the value struct: emulation turns the pad
    // capability on, and the unsaved pad choice follows pad_default.
    TouchConfig cfg;
    cfg.policy = true;
    cfg.claims = 1;
    cfg.pad_default = 2;  // non-zero -> visible
    cfg.emulate_touch = true;
    cfg.pad_idle_hide_seconds = 5;
    o.configure_touch(cfg);
    CHECK(o.touch_pad_visible());
    CHECK(b.touch_pad_visible_value());

    // A saved choice wins from then on (pad_default no longer applies).
    o.set_touch_pad_visible(false);
    CHECK(!o.touch_pad_visible());
    CHECK(!b.touch_pad_visible_value());
    b.configure_touch(true, 1, 2, true, 5);
    CHECK(!o.touch_pad_visible());  // saved: stays hidden
    b.set_touch_pad_visible(true);
    CHECK(o.touch_pad_visible());

    // Config dir + overlay hook: null is a safe no-op in both views.
    o.set_config_dir(nullptr);
    b.set_touch_config_dir(nullptr);
    o.set_host_overlay(nullptr);
    b.set_host_overlay(nullptr);
}

void facade_lifecycle_contract() {
    RaylibBackend b;
    WindowBackendV2& v2 = b;  // both generations on one object

    // Caps invariants that hold in every config.
    const Caps caps = v2.caps();
#if defined(GBARECOMP_USE_RAYLIB)
    CHECK(caps.menu);  // the presentation exists in the raylib build
#else
    CHECK(!caps.menu);  // state-only stub: no presentation
#endif
    CHECK(!caps.rumble && !caps.haptics);
    CHECK(caps.reopen);         // desktop
    CHECK(!caps.vsync_report);  // no monitor probe yet
    CHECK(!caps.audio);         // no device yet
    CHECK(!caps.headless);      // GLFW platform (PLATFORM_MEMORY not defined)
#if defined(GBARECOMP_USE_RAYLIB)
    CHECK(caps.window && caps.input && caps.touch);
#if defined(GRAPHICS_API_OPENGL_SOFTWARE)
    CHECK(!caps.shaders);  // rlsw compiles the shader entry points out
#else
    CHECK(caps.shaders);
#endif
#else
    CHECK(!caps.window && !caps.input && !caps.touch && !caps.shaders);
#endif

    // Headless requires the memory platform; refused here (both configs).
    OpenParams headless;
    headless.mode = OpenParams::Mode::Headless;
    const Result hr = v2.open(headless);
    CHECK(!hr.ok() && hr.code == Err::Unsupported && hr.detail != nullptr);
    CHECK(!v2.is_open());

    // close() is idempotent on a closed backend; lifecycle calls are safe.
    v2.close();
    CHECK(!v2.is_open());
    CHECK(v2.suspend() && v2.resume());
    CHECK(!v2.wait_for_foreground());  // closed -> false

    // A real open: succeeds in the state-only stub build and on a display; a
    // display-less GLFW build fails with Err::Window. Both are legal.
    const Result r = v2.open(OpenParams{});
    CHECK(r.ok() || r.code == Err::Window);
    if (r.ok()) {
        CHECK(v2.is_open());
        const Result again = v2.open(OpenParams{});
        CHECK(!again.ok() && again.code == Err::AlreadyOpen);
        CHECK(v2.wait_for_foreground());
        v2.close();
        CHECK(!v2.is_open());
        // Desktop supports close -> open (Caps::reopen).
        CHECK(v2.open(OpenParams{}).ok());
        v2.close();
    }

    // The V2 accessors return the same objects the flat generation uses.
    CHECK(&v2.video() == &b.video());
    CHECK(&v2.audio() == &b.audio());
    CHECK(&v2.input() == &b.input());
    CHECK(&v2.fx() == &b.fx());
    CHECK(&v2.overlay() == &b.overlay());
    CHECK(&v2.menu() == &b.menu());
    CHECK(v2.menu().available() == caps.menu);
}

}  // namespace

int main() {
    video_defaults_clamps_and_shared_state();
    video_safe_without_a_window();
    video_refresh_hz_estimate();
    audio_defaults_and_shared_state();
    audio_safe_without_a_device();
    input_defaults_and_shared_state();
    input_safe_without_a_window();
    fx_defaults_roundtrip_and_shared_state();
    overlay_defaults_and_shared_state();
    facade_lifecycle_contract();

    std::printf("raylib_backend_v2 tests: PASS\n");
    return 0;
}
