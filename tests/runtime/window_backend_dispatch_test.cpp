// window_backend_dispatch_test.cpp — the per-interface surface walk (Phase 4d).
//
// Ports the two deleted flat-generation dispatch tests to the single-
// generation seam: every method of every interface is called through ITS
// INTERFACE TYPE (`WindowBackend&` for the facade, `Video&`/`Audio&`/... from
// the accessors) and each call must land on the implementation behind the
// interface. The compile side of that contract is structural (pure-virtual
// interfaces + `override`), so this file's jobs are:
//
//   1. surface usage — a consumer can reach every method through the seam
//      types; a signature change breaks this file at compile time, and
//   2. dispatch — each call lands on the implementation the accessor returned
//      (a broken accessor, or a same-named non-virtual member, fails here).
//
// The walk drives the §5.2 fakes (no window, no display, no devices):
// FakeBackend composes RecordingVideo / ScriptedInput / TappingAudio / FakeFx /
// FakeOverlay / FakeMenu, each of which records what it saw.
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG; see BRINGUP
// "vacuous test" finding.

#include "window_backend.h"

#include "menu_model.h"
#include "support/fake_backend.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using gbarecomp::Audio;
using gbarecomp::Caps;
using gbarecomp::Fx;
using gbarecomp::FxSettings;
using gbarecomp::Input;
using gbarecomp::InputSnapshot;
using gbarecomp::Menu;
using gbarecomp::MenuModel;
using gbarecomp::OpenParams;
using gbarecomp::Overlay;
using gbarecomp::PointerState;
using gbarecomp::TouchConfig;
using gbarecomp::Video;
using gbarecomp::WindowBackend;
using gbarecomp::testing::FakeBackend;

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace {
void test_overlay(gbarecomp::HostOverlay*) {}
}  // namespace

int main() {
    FakeBackend fb;
    WindowBackend& seam = fb;

    // ── facade: lifecycle + capabilities through WindowBackend& ─────────────
    CHECK(!seam.is_open());
    CHECK(!seam.wait_for_foreground());  // closed -> false
    OpenParams params;
    params.title = "dispatch";
    CHECK(seam.open(params).ok());
    CHECK(fb.open_calls == 1);
    CHECK(seam.is_open());
    const Caps caps = seam.caps();
    CHECK(caps.window);  // the fake's desktop-shaped defaults
    CHECK(seam.wait_for_foreground());  // open -> true

    // Accessor identity: every call returns the SAME composed object, which is
    // what makes the per-interface walks below meaningful.
    CHECK(&seam.video() == &fb.recording_video);
    CHECK(&seam.audio() == &fb.tapped_audio);
    CHECK(&seam.input() == &fb.scripted_input);
    CHECK(&seam.fx() == &fb.fake_fx);
    CHECK(&seam.overlay() == &fb.fake_overlay);
    CHECK(&seam.menu() == &fb.fake_menu);

    // ── Video: all 19 methods through Video& ────────────────────────────────
    Video& v = seam.video();
    std::vector<std::uint8_t> frame(240u * 160u * 3u, 0x5A);
    v.present(frame.data(), /*fresh=*/true);
    CHECK(fb.recording_video.present_calls == 1);
    CHECK(fb.recording_video.last_fresh);
    CHECK(fb.recording_video.have_frame);
    v.interp_display_tick(frame.data(), 4242u);
    CHECK(fb.recording_video.interp_calls == 1);
    CHECK(fb.recording_video.last_deadline_ns == 4242u);
    std::vector<std::uint8_t> back;
    int rw = 0, rh = 0;
    CHECK(v.readback(back, rw, rh));
    CHECK(rw == 240 && rh == 160 && back == frame);
    CHECK(v.refresh_hz() == fb.recording_video.hz);
    int dw = 0, dh = 0;
    CHECK(v.drawable_size(&dw, &dh));
    CHECK(dw == fb.recording_video.drawable_w && dh == fb.recording_video.drawable_h);
    CHECK(v.set_surface_size(320, 240));
    CHECK(fb.recording_video.base_w == 320 && fb.recording_video.base_h == 240);
    v.set_title("dispatch-2");
    CHECK(fb.recording_video.title == "dispatch-2");
    CHECK(v.scale() == fb.recording_video.scale());
    v.adjust_scale(1);
    CHECK(v.scale() == 4);
    v.set_linear_filter(true);
    CHECK(v.linear_filter());
    v.set_resize_driven_view(true);
    CHECK(fb.recording_video.resize_driven);
    v.set_view_margins(1, 2, 3, 4);
    CHECK(fb.recording_video.margin_l == 1 && fb.recording_video.margin_r == 2 &&
          fb.recording_video.margin_t == 3 && fb.recording_video.margin_b == 4);
    v.set_fullscreen(9);
    CHECK(v.fullscreen() == 2);  // clamped high
    CHECK(v.px_per_mm() == 96.0f / 25.4f);
    v.set_presentation_anchor_top(true);
    CHECK(fb.recording_video.anchor_top);
    v.set_fps_readout(true);
    CHECK(v.fps_readout());

    // ── Audio: all 7 methods through Audio& ─────────────────────────────────
    Audio& a = seam.audio();
    CHECK(a.claim().ok());
    CHECK(fb.tapped_audio.claim_calls == 1);
    CHECK(fb.tapped_audio.claimed);
    a.set_volume(42);
    CHECK(a.volume() == 42);
    a.set_enabled(false);
    CHECK(!a.enabled());
    const std::int16_t samples[4] = {1, 2, 3, 4};
    a.push(samples, 4);
    CHECK(fb.tapped_audio.push_calls == 1);
    CHECK(fb.tapped_audio.pushed == 4);
    a.release();
    CHECK(fb.tapped_audio.release_calls == 1);
    CHECK(!fb.tapped_audio.claimed);

    // ── Input: all 8 methods through Input& ─────────────────────────────────
    Input& in = seam.input();
    InputSnapshot snap;
    in.read(snap);
    CHECK(fb.scripted_input.read_calls == 1);
    in.load_config("/tmp/dispatch", true, 4);
    CHECK(fb.scripted_input.load_config_calls == 1);
    CHECK(&in.bindings() == &in.bindings());  // stable reference
    CHECK(in.assist_tools_enabled() == fb.scripted_input.assist);
    CHECK(in.fast_forward_multiplier() == fb.scripted_input.ff);
    CHECK(in.gamepad_connected() == fb.scripted_input.pad_connected);
    in.rumble(0.5f, 0.25f, 120);
    CHECK(fb.scripted_input.rumble_calls == 1);
    CHECK(fb.scripted_input.last_low == 0.5f &&
          fb.scripted_input.last_high == 0.25f &&
          fb.scripted_input.last_duration_ms == 120);
    in.haptic_pulse(10, 0.5f);
    CHECK(fb.scripted_input.haptic_calls == 1);
    CHECK(fb.scripted_input.last_haptic_ms == 10 &&
          fb.scripted_input.last_haptic_strength == 0.5f);

    // ── Fx: all 4 methods through Fx& ───────────────────────────────────────
    Fx& fx = seam.fx();
    FxSettings settings = fx.settings();
    settings.frame_interp = true;
    settings.lcd = true;
    fx.apply(settings);
    CHECK(fb.fake_fx.apply_calls == 1);
    CHECK(fb.fake_fx.stored.frame_interp && fb.fake_fx.stored.lcd);
    fx.reset_frame_blend();
    CHECK(fb.fake_fx.reset_calls == 1);
    fx.load_config("/tmp/dispatch");
    CHECK(fb.fake_fx.load_config_calls == 1);
    CHECK(fb.fake_fx.config_dir == "/tmp/dispatch");

    // ── Overlay: all 5 methods through Overlay& ─────────────────────────────
    Overlay& ov = seam.overlay();
    ov.set_host_overlay(&test_overlay);
    CHECK(fb.fake_overlay.host_overlay == &test_overlay);
    TouchConfig touch;
    touch.policy = true;
    touch.claims = 0x3u;
    ov.configure_touch(touch);
    CHECK(fb.fake_overlay.touch.policy && fb.fake_overlay.touch.claims == 0x3u);
    ov.set_config_dir("/tmp/dispatch");
    CHECK(fb.fake_overlay.config_dir == "/tmp/dispatch");
    ov.set_touch_pad_visible(true);
    CHECK(ov.touch_pad_visible());

    // ── Menu: all 2 methods through Menu& ───────────────────────────────────
    Menu& menu = seam.menu();
    CHECK(menu.available() == caps.menu);
    MenuModel model;
    PointerState pointer;
    fb.fake_menu.next_result.changed_row = 3;
    const gbarecomp::MenuResult result = menu.present(model, pointer);
    CHECK(fb.fake_menu.present_calls == 1);
    CHECK(fb.fake_menu.last_model == &model);
    CHECK(result.changed_row == 3);

    // ── facade tail: pump / suspend / resume / close ────────────────────────
    const auto ev = seam.pump();
    CHECK(fb.pump_calls == 1);
    CHECK(ev.keyinput == 0x03FFu && !ev.quit);  // no keys scripted
    CHECK(seam.suspend());
    CHECK(fb.suspend_calls == 1);
    CHECK(seam.resume());
    CHECK(fb.resume_calls == 1);
    seam.close();
    CHECK(fb.close_calls == 1);
    CHECK(!seam.is_open());

    std::printf("window_backend dispatch tests: PASS\n");
    return 0;
}
