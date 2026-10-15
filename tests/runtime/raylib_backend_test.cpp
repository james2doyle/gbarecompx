// Skeleton guard for RaylibBackend.
//
// Two things are checked, and the second is the interesting one:
//
//  1. The class compiles and is constructible, and the pure-state methods we
//     claim to have implemented actually behave (shared policy: resolve_open_params,
//     volume clamping, scale clamping, sharp-wins-over-linear).
//
//  2. RaylibBackend is reachable through a plain WindowBackend&, so it is a drop-in
//     for the seam. Combined with the `override` keyword on every method in
//     raylib_backend.h, this file fails to COMPILE if the interface grows a
//     method the raylib backend has not accounted for -- which is the whole point
//     of "guarded interface".
//
// This target deliberately builds with neither SDL nor raylib on the include
// path. If it ever needs one, the seam has regressed.

#include "raylib_backend.h"

#include "gamepad_map.h"

#include <cassert>
#include <cstdio>

using gbarecomp::RaylibBackend;
using gbarecomp::WindowBackend;

int main() {
    // Not open until open() succeeds.
    RaylibBackend impl;
    WindowBackend& b = impl;
    assert(!b.is_open());

    // Pure-state setters/getters round-trip through the seam.
    b.set_volume(55);
    assert(b.volume_percent() == 55);
    b.set_volume(-10);
    assert(b.volume_percent() == 0);
    b.set_volume(999);
    assert(b.volume_percent() == 100);

    b.adjust_scale(2);
    assert(b.window_scale() == 5);
    b.adjust_scale(-99);
    assert(b.window_scale() == 1);   // clamped low
    b.adjust_scale(999);
    assert(b.window_scale() == 8);   // clamped high

    b.set_linear_filter(true);
    assert(b.linear_filter_enabled());
    b.set_linear_filter(false);
    assert(!b.linear_filter_enabled());

    b.set_fps_readout(true);
    assert(b.fps_readout_value());
    b.set_fps_readout(false);
    assert(!b.fps_readout_value());

    b.set_frame_interp(true);
    assert(b.frame_interp());
    b.set_frame_blend(true);
    assert(b.frame_blend_value());
    b.reset_frame_blend();          // must not crash / must stay consistent

    // LCD tunables round-trip through the seam (stub build: recorded, never
    // live — the shader needs a GL context this config never creates).
    b.set_lcd_shader(true);
    assert(b.lcd_shader_enabled());
    b.set_lcd_shader(false);
    assert(!b.lcd_shader_enabled());
    b.set_lcd_boost(2.0f);
    assert(b.lcd_boost_value() == 2.0f);
    b.set_lcd_boost(99.0f);
    assert(b.lcd_boost_value() == 3.0f);   // clamped high
    b.set_lcd_boost(-1.0f);
    assert(b.lcd_boost_value() == 1.0f);   // clamped low
    b.set_lcd_dither(0.0f);
    assert(b.lcd_dither_value() == 0.0f);  // 0 disables the pass
    b.set_lcd_dither(9.0f);
    assert(b.lcd_dither_value() == 2.0f);  // capped
    b.set_lcd_deband(6.0f);
    assert(b.lcd_deband_value() == 6.0f);
    b.set_lcd_deband(0.0f);
    assert(b.lcd_deband_value() == 0.0f);  // 0 disables the pass
    b.set_lcd_deband(99.0f);
    assert(b.lcd_deband_value() == 16.0f);  // capped

    b.set_audio_enabled(false);
    assert(!b.audio_enabled());
    b.set_audio_enabled(true);
    assert(b.audio_enabled());

    b.set_fullscreen(2);
    assert(b.fullscreen_mode() == 2);
    b.set_fullscreen(99);           // clamped to 0..2
    assert(b.fullscreen_mode() == 2);
    b.set_fullscreen(-5);
    assert(b.fullscreen_mode() == 0);

    b.set_presentation_anchor_top(true);
    b.set_view_margins(1, 2, 3, 4);
    b.set_touch_pad_visible(true);
    assert(b.touch_pad_visible_value());

    b.set_title("raylib skeleton");

    // Assist helpers are part of the same seam.
    assert(b.assist_tools_enabled());
    assert(b.fast_forward_multiplier() >= 2);

    // Geometry policy is shared with the SDL backend via window_open_params.h.
    assert(b.set_surface_size(320, 240));
    assert(b.set_surface_size(0, 0) == false);   // rejects degenerate sizes
    assert(b.set_surface_size(320, 240));        // idempotent re-request

    // Stubs must return the honest WindowBackend defaults, not garbage.
    int w = -1, h = -1;
    (void)b.drawable_size(&w, &h);              // stubbed: safe to call
    WindowBackend::Events ev;
    b.pump_events(ev);
    assert(!ev.quit);
    assert(ev.keyinput == 0x03FF);               // nothing pressed by default
    const WindowBackend::Events pumped = b.pump();
    assert(!pumped.quit);
    assert(pumped.keyinput == 0x03FF);
    assert(b.wait_for_foreground() == false);    // documented stub
    b.present(nullptr, true);                    // must not crash
    b.push_audio_samples(nullptr, 0);            // must not crash
    b.interp_display_tick(nullptr, 0);           // must not crash
    std::vector<std::uint8_t> frame;
    int fw = 0, fh = 0;
    assert(!b.copy_presented_frame(frame, fw, fh));

    // Gamepad capability is honest while unimplemented: claim nothing.
    assert(!b.gamepad_connected());
    assert(!b.gamepad_supports_rumble());
    b.gamepad_rumble(1.0f, 1.0f, 500);          // silent no-op
    b.haptic_pulse(10, 0.5f);                    // silent no-op

    // open()/close() through the flat overload, and idempotence.
    assert(b.open(3, 240, 160, "raylib skeleton", nullptr, false, false,
                  false, false));
    assert(b.is_open());
    assert(b.open(3, 240, 160, "again", nullptr, false, false, false, false));
    b.close();
    assert(!b.is_open());

    // The params-struct overload and the flat overload must agree. They are two
    // entry points to the same setup; if normalization ever moves to one and not
    // the other, callers get silently different windows.
    {
        RaylibBackend flat, structured;
        WindowBackend& f = flat;
        WindowBackend& st = structured;

        assert(f.open(0, 0, -3, "t", nullptr, true, true, false, false));
        gbarecomp::WindowBackendOpenParams p;
        p.scale = 0;
        p.base_w = 0;
        p.base_h = -3;
        p.title = "t";
        p.linear_filter = true;
        p.sharp_filter = true;
        assert(st.open(p));

        // Same normalization => same observable state.
        assert(f.window_scale() == st.window_scale());
        assert(f.linear_filter_enabled() == st.linear_filter_enabled());

        // Defaults in the struct must match what the flat form would clamp to,
        // so `open({})` behaves like `open(3, 240, 160, ...)`.
        assert(gbarecomp::WindowBackendOpenParams{}.scale == 3);
        assert(gbarecomp::WindowBackendOpenParams{}.base_w == 240);
        assert(gbarecomp::WindowBackendOpenParams{}.base_h == 160);

        f.close();
        st.close();
    }

    // Pin the Events contract. This struct was MOVED from host_window.h into
    // window_backend.h when pump() was converted; the runtime reads every one of
    // these fields, so a silent rename or default change is a real regression.
    {
        WindowBackend::Events e;
        assert(!e.quit);
        assert(e.keyinput == 0x03FF);      // active-low: nothing pressed
        assert(e.save_slot == 0 && e.load_slot == 0);
        assert(!e.solar_brighter && !e.solar_dimmer && !e.solar_live);
        assert(!e.fast_forward && !e.rewind);
        assert(!e.toggle_fullscreen && !e.toggle_pause);
        assert(!e.window_bigger && !e.window_smaller);
        assert(!e.volume_up && !e.volume_down && !e.toggle_fps);
        assert(e.gyro_delta_x == 0 && !e.mouse_gyro_active);
        assert(e.gyro_rate_z == 0.0f);
        assert(!e.enter_background && !e.terminating);
    }

    // Human input is deliberately NOT sanitized: pressing A+B+Start+Select on a
    // pad or keyboard should soft-reset, exactly like real GBA hardware.
    // KeySynth::sanitize() applies only to synthesized touch taps. Pin this so a
    // future "cleanup" does not silently break authentic soft-resets.
    {
        gbarecomp::GamepadSnapshot pad;
        pad.buttons = gbarecomp::kGbaKeyA | gbarecomp::kGbaKeyB |
                      gbarecomp::kGbaKeyStart | gbarecomp::kGbaKeySelect;
        const std::uint16_t keyinput = gbarecomp::gamepad_to_keyinput(pad);
        // All four bits clear => all four read as PRESSED to the guest.
        assert((keyinput &
                (gbarecomp::kGbaKeyA | gbarecomp::kGbaKeyB |
                 gbarecomp::kGbaKeyStart | gbarecomp::kGbaKeySelect)) == 0);
    }

    std::printf("raylib_backend tests: PASS\n");
    return 0;
}