#pragma once

// On-screen touch pad + toggle drawing, shared by the SDL and raylib
// backends. The drawing itself is backend-neutral HostOverlay primitives;
// only the pad STATE (layout, held bits, visibility) arrives as plain values,
// so this header stays free of SDL/raylib and is fully unit-testable with a
// recording fake overlay.
//
// Extracted verbatim from host_window.cpp; the SDL backend's draw_touch_pad()
// / draw_pad_toggle() are thin wrappers that forward Backend state.

#include <algorithm>
#include <cstdint>

#include "host_overlay.h"
#include "touch_pad_layout.h"

namespace gbarecomp {

// Every control: a dark translucent backing (legible over any scenery), a
// bright outline, and a solid gold fill while pressed.
inline void draw_touch_pad(HostOverlay& ov, const PadLayout& p,
                           std::uint16_t held, float mm, bool ff_active) {
    using S = OverlaySpace;
    const auto Ds = S::Drawable;
    const OverlayColor backing{12, 16, 28, 150};
    const OverlayColor outline{240, 244, 252, 210};
    const OverlayColor pressed{255, 214, 74, 230};
    const OverlayColor label{255, 255, 255, 235};
    const float line = std::max(2.0f, 0.45f * mm);
    auto on = [held](int bit) { return (held & (1u << bit)) != 0; };

    // D-pad cross: backing + outline per arm, pressed arm highlighted.
    const float arm = p.dpad.r, t = p.dpad_thick, cx = p.dpad.x, cy = p.dpad.y;
    ov.fill_rect(cx - t * 0.5f, cy - arm, t, arm * 2.0f, t * 0.2f, backing, Ds);
    ov.fill_rect(cx - arm, cy - t * 0.5f, arm * 2.0f, t, t * 0.2f, backing, Ds);
    const float half = arm - t * 0.5f;
    struct Arm { int bit; float x, y, w, h; };
    const Arm arms[4] = {{6, cx - t * 0.5f, cy - arm, t, half},        // Up
                         {7, cx - t * 0.5f, cy + t * 0.5f, t, half},   // Down
                         {5, cx - arm, cy - t * 0.5f, half, t},        // Left
                         {4, cx + t * 0.5f, cy - t * 0.5f, half, t}};  // Right
    for (const Arm& a : arms)
        if (on(a.bit)) ov.fill_rect(a.x, a.y, a.w, a.h, t * 0.2f, pressed, Ds);
    ov.stroke_rect(cx - t * 0.5f, cy - arm, t, arm * 2.0f, t * 0.2f, line, outline, Ds);
    ov.stroke_rect(cx - arm, cy - t * 0.5f, arm * 2.0f, t, t * 0.2f, line, outline, Ds);

    auto round_button = [&](const PadCircle& c, int bit, OverlayColor tint, const char* text) {
        ov.fill_circle(c.x, c.y, c.r, on(bit) ? pressed : tint, Ds);
        ov.stroke_circle(c.x, c.y, c.r, line, outline, Ds);
        if (ov.text_supported())
            ov.text(c.x, c.y - 2.6f * mm, 5.2f * mm, label, text, OverlayAlign::Center, Ds);
    };
    round_button(p.a, 0, {196, 52, 64, 190}, "A");
    round_button(p.b, 1, {40, 120, 200, 190}, "B");

    auto pill = [&](const PadRect& r, int bit, const char* text, float radius) {
        ov.fill_rect(r.x, r.y, r.w, r.h, radius, on(bit) ? pressed : backing, Ds);
        ov.stroke_rect(r.x, r.y, r.w, r.h, radius, line, outline, Ds);
        if (ov.text_supported()) {
            const float size = std::min(r.h * 0.62f, 3.4f * mm);
            ov.text(r.x + r.w * 0.5f, r.y + (r.h - size) * 0.5f, size, label, text,
                    OverlayAlign::Center, Ds);
        }
    };
    pill(p.l, 9, "L", 2.0f * mm);
    pill(p.r, 8, "R", 2.0f * mm);
    pill(p.select, 2, "SELECT", p.select.h * 0.5f);
    pill(p.start, 3, "START", p.start.h * 0.5f);
    // Fast-forward pill: teal at rest (not a game button), gold while a
    // finger holds it — same hold-to-run rule as Tab and the right trigger.
    // Has no KEYINPUT bit, so it bypasses `pill`.
    {
        const PadRect& r = p.ff;
        const OverlayColor idle{40, 180, 170, 190};
        ov.fill_rect(r.x, r.y, r.w, r.h, r.h * 0.5f,
                     ff_active ? pressed : idle, Ds);
        ov.stroke_rect(r.x, r.y, r.w, r.h, r.h * 0.5f, line, outline, Ds);
        if (ov.text_supported()) {
            const float size = std::min(r.h * 0.62f, 3.4f * mm);
            ov.text(r.x + r.w * 0.5f, r.y + (r.h - size) * 0.5f, size, label,
                    "FF", OverlayAlign::Center, Ds);
        }
    }
}

inline void draw_pad_toggle(HostOverlay& ov, const PadLayout& p,
                            bool toggle_held, bool pad_visible) {
    using S = OverlaySpace;
    const auto Ds = S::Drawable;
    const PadRect& r = p.toggle;
    const OverlayColor bg = toggle_held ? OverlayColor{80, 210, 255, 170}
                          : pad_visible ? OverlayColor{255, 214, 74, 120}
                                        : OverlayColor{20, 26, 38, 110};
    ov.fill_rect(r.x, r.y, r.w, r.h, r.h * 0.3f, bg, Ds);
    ov.stroke_rect(r.x, r.y, r.w, r.h, r.h * 0.3f, 1.5f, {230, 238, 248, 150}, Ds);
    // Minimal gamepad glyph: a cross on the left, two dots on the right.
    const float cx = r.x + r.w * 0.32f, cy = r.y + r.h * 0.5f;
    const float s = r.h * 0.18f;
    ov.fill_rect(cx - s * 0.4f, cy - s * 1.2f, s * 0.8f, s * 2.4f, 0, {230, 238, 248, 190}, Ds);
    ov.fill_rect(cx - s * 1.2f, cy - s * 0.4f, s * 2.4f, s * 0.8f, 0, {230, 238, 248, 190}, Ds);
    ov.fill_circle(r.x + r.w * 0.68f, cy - s * 0.5f, s * 0.55f, {230, 238, 248, 190}, Ds);
    ov.fill_circle(r.x + r.w * 0.80f, cy + s * 0.5f, s * 0.55f, {230, 238, 248, 190}, Ds);
}

}  // namespace gbarecomp
