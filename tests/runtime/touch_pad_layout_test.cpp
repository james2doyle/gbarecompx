// Touch pad geometry. Pure, no display: the layout must be identical on every
// backend, so it is verified here rather than through any window.

#include "touch_pad_layout.h"

#include <cstdio>

using namespace gbarecomp;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (ok) {
        std::printf("  ok    %s\n", what);
    } else {
        std::printf("  FAIL  %s\n", what);
        ++failures;
    }
}

// A landscape phone-ish drawable (960x640) with the game pillar-boxed in the
// middle at 2x (480x320). Rails on both sides for the pad.
PadLayoutInputs landscape() {
    PadLayoutInputs in;
    in.drawable_w = 960.0f;
    in.drawable_h = 640.0f;
    in.px_per_mm = 6.0f;
    in.game = {240, 0, 480, 320, 0};
    return in;
}

}  // namespace

int main() {
    // ---- landscape arrangement ----------------------------------------------
    {
        const PadLayout p = compute_pad_layout(landscape());
        check(p.dpad.x < 480.0f, "landscape dpad sits left of centre");
        check(p.a.x > 480.0f && p.b.x > 480.0f, "landscape A/B sit right");
        check(p.a.r > 0.0f && p.b.r > 0.0f, "buttons have positive radius");
        check(p.dpad.r > 0.0f && p.dpad_thick > 0.0f, "dpad has size");
        // D-pad and B cluster must not overlap: the layout scales down
        // instead (never below 65%), it never overlaps.
        const float dx = p.b.x - p.dpad.x;
        check(dx > p.dpad.r + p.b.r, "dpad and B cluster do not touch");
        check(p.l.w > 0.0f && p.r.w > 0.0f, "shoulders have width");
        check(p.select.w > 0.0f && p.start.w > 0.0f, "pills have width");
    }

    // ---- portrait arrangement -------------------------------------------------
    {
        PadLayoutInputs in;
        in.drawable_w = 640.0f;
        in.drawable_h = 960.0f;
        in.px_per_mm = 6.0f;
        in.game = {0, 0, 640, 426, 0};
        const PadLayout p = compute_pad_layout(in);
        check(p.dpad.y > 426.0f, "portrait dpad sits below the game image");
        check(p.a.y > 426.0f, "portrait A sits below the game image");
        check(p.dpad.x < p.a.x, "portrait dpad left, A right");
    }

    // ---- hit testing ------------------------------------------------------------
    {
        const PadLayout p = compute_pad_layout(landscape());
        const float mm = 6.0f;
        check(pad_buttons_at(p, p.dpad.x + p.dpad.r * 0.9f, p.dpad.y, mm) ==
                  (1u << 4),
              "dpad right edge reads Right");
        check(pad_buttons_at(p, p.dpad.x - p.dpad.r * 0.9f, p.dpad.y, mm) ==
                  (1u << 5),
              "dpad left edge reads Left");
        check(pad_buttons_at(p, p.dpad.x, p.dpad.y - p.dpad.r * 0.9f, mm) ==
                  (1u << 6),
              "dpad top reads Up");
        check(pad_buttons_at(p, p.dpad.x, p.dpad.y + p.dpad.r * 0.9f, mm) ==
                  (1u << 7),
              "dpad bottom reads Down");
        check(pad_buttons_at(p, p.dpad.x, p.dpad.y, mm) == 0,
              "dpad centre dead zone reads nothing");
        check((pad_buttons_at(p, p.a.x, p.a.y, mm) & (1u << 0)) != 0,
              "A centre reads A");
        check((pad_buttons_at(p, p.b.x, p.b.y, mm) & (1u << 1)) != 0,
              "B centre reads B");
        check((pad_buttons_at(p, p.start.x + p.start.w * 0.5f,
                              p.start.y + p.start.h * 0.5f, mm) &
               (1u << 3)) != 0,
              "Start pill reads Start");
        check(pad_buttons_at(p, 480.0f, 320.0f, mm) == 0,
              "middle of the game image reads nothing");
    }

    // ---- toggle placement ----------------------------------------------------------
    {
        PadLayoutInputs in = landscape();
        const PadLayout def = compute_pad_layout(in);
        // Default toggle parks bottom-right.
        check(def.toggle.x + def.toggle.w > 900.0f &&
                  def.toggle.y + def.toggle.h > 580.0f,
              "default toggle parks bottom-right");
        in.toggle_pos_saved = true;
        in.toggle_fx = 0.1f;
        in.toggle_fy = 0.1f;
        const PadLayout saved = compute_pad_layout(in);
        check(saved.toggle.x < 200.0f && saved.toggle.y < 200.0f,
              "saved toggle position is honoured");
    }

    // ---- fast-forward pill placement + toggle rule ------------------------------
    {
        const PadLayout p = compute_pad_layout(landscape());
        check(p.ff.w > 0.0f && p.ff.h > 0.0f, "FF pill has size");
        check(p.ff.x + p.ff.w <= p.select.x, "FF pill sits left of select");
        check(p.ff.y == p.select.y && p.ff.y == p.start.y,
              "FF pill shares the select/start row");
        const float mm = 6.0f;
        check(pad_ff_at(p, p.ff.x + p.ff.w * 0.5f, p.ff.y + p.ff.h * 0.5f, mm),
              "pill centre hits");
        check(!pad_ff_at(p, p.start.x + p.start.w * 0.5f,
                         p.start.y + p.start.h * 0.5f, mm),
              "start pill is not FF");
        check(pad_buttons_at(p, p.ff.x + p.ff.w * 0.5f,
                             p.ff.y + p.ff.h * 0.5f, mm) == 0,
              "FF pill sets no GBA bit");

        PadLayoutInputs pin;
        pin.drawable_w = 640.0f;
        pin.drawable_h = 960.0f;
        pin.px_per_mm = 6.0f;
        pin.game = {0, 0, 640, 426, 0};
        const PadLayout pp = compute_pad_layout(pin);
        check(pp.ff.x + pp.ff.w <= pp.select.x,
              "portrait FF pill sits left of select");
        check(pp.ff.y == pp.select.y, "portrait FF shares the pill row");
    }

    // ---- predicates -------------------------------------------------------------------
    {
        check(touch_input_enabled(true, false, false),
              "touch controls alone enable touch input");
        check(touch_input_enabled(false, true, false),
              "game policy alone enables touch input");
        check(!touch_input_enabled(false, false, false),
              "nothing enabled means no touch input");
        check(pad_overlay_shown(true, true, false),
              "visible+controls+not-idle shows the pad");
        check(!pad_overlay_shown(true, true, true),
              "idle-hidden hides the pad");
        check(!pad_overlay_shown(false, true, false),
              "player-hidden hides the pad");
        check(pad_toggle_available(true, true, false),
              "policy+controls offer the toggle");
        check(!pad_toggle_available(false, true, false),
              "no policy means no toggle (pad-only title keeps controls)");
        check(pad_toggle_shown(true, true, false, false),
              "toggle shows when available and not idle");
        check(!pad_toggle_shown(true, true, false, true),
              "idle hides the toggle too");
    }

    std::printf("touch_pad_layout tests: %s (%d failure(s))\n",
                failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
