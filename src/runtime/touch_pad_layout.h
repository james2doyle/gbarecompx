#pragma once

// On-screen touch pad geometry: physical-size layout (millimetres via display
// density) derived from the live presentation. Controls sit in the
// letter/pillar-box bands when those are large enough, and overlay the screen
// edges otherwise. Landscape and portrait both work, including live rotation.
//
// SHARED by the SDL and raylib backends (and the Android port through the
// raylib one): the layout must be identical wherever the pad appears, so it
// lives here rather than as parallel copies. Extracted verbatim from
// host_window.cpp; the SDL backend's compute_pad_layout() is now a thin
// wrapper that builds the inputs struct.
//
// All policy inputs arrive as plain values (no Backend*), so this header stays
// free of SDL/raylib and is fully unit-testable.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "presentation_layout.h"

namespace gbarecomp {

struct PadCircle {
    float x = 0, y = 0, r = 0;
};

struct PadRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py, float pad = 0.0f) const {
        return px >= x - pad && py >= y - pad &&
               px <= x + w + pad && py <= y + h + pad;
    }
};

struct PadLayout {
    PadCircle dpad;       // r = arm length (centre to tip)
    float dpad_thick = 0;
    PadCircle a, b;
    PadRect l, r, select, start;
    // Fast-forward pill: host speed control, NOT a GBA bit. Hold-to-run like
    // desktop Tab and the right-trigger Assist binding: engaged while a
    // finger is down inside, released when it lifts or slides off.
    PadRect ff;
    PadRect toggle;
};

// Everything compute_pad_layout() needs, as values. A backend builds this
// from its own state (drawable size, density, safe insets, game rect, saved
// toggle position).
struct PadLayoutInputs {
    float drawable_w = 0.0f, drawable_h = 0.0f;
    float px_per_mm = 96.0f / 25.4f;
    int inset_left = 0, inset_top = 0, inset_right = 0, inset_bottom = 0;
    PresentationLayout game{};
    float toggle_fx = 0.0f, toggle_fy = 0.0f;
    bool toggle_pos_saved = false;
};

inline PadLayout compute_pad_layout(const PadLayoutInputs& in) {
    PadLayout p;
    const float dw = in.drawable_w;
    const float dh = in.drawable_h;
    const float mm = std::max(1.0f, in.px_per_mm);
    const PresentationLayout& g = in.game;

    const float left = static_cast<float>(in.inset_left);
    const float right = dw - static_cast<float>(in.inset_right);
    const float top = static_cast<float>(in.inset_top);
    const float bottom = dh - static_cast<float>(in.inset_bottom);

    // Design sizes (mm). The D-pad and the A/B cluster must never touch: when
    // the safe width cannot hold both plus a clear gap, every control scales
    // down together (never below 65%) instead of overlapping.
    constexpr float kMargin = 4.0f, kArm = 11.5f, kThick = 9.0f, kButtonR = 6.5f;
    constexpr float kGap = 5.0f;                    // D-pad <-> B clearance
    constexpr float kClusterDx = 2.3f;              // A-B centre distance / r
    const float cluster_w = (kClusterDx + 2.0f) * kButtonR;
    const float need_w = kMargin + 2.0f * kArm + kGap + cluster_w + kMargin;
    const float avail_w = (right - left) / mm;
    // Portrait: when there is a usable band under the game image (native 3:2
    // scenes), shrink to fit it rather than overlaying the image.
    constexpr float kBandNeed = 50.0f;              // mm at full size
    const float band_avail = (bottom - static_cast<float>(g.y + g.height)) / mm;
    float s = 1.0f;
    if (dw < dh) {
        s = std::clamp(avail_w / need_w, 0.65f, 1.0f);
        if (band_avail >= kBandNeed * 0.65f) s = std::min(s, band_avail / kBandNeed);
    }
    const float margin = kMargin * s * mm;
    const float arm = kArm * s * mm;
    const float button_r = kButtonR * s * mm;
    const float shoulder_w = 18.0f * s * mm, shoulder_h = 7.5f * s * mm;
    const float pill_w = 11.0f * s * mm, pill_h = 4.5f * s * mm;
    p.dpad_thick = kThick * s * mm;
    p.dpad.r = arm;
    p.a.r = p.b.r = button_r;

    if (dw >= dh) {
        // Landscape: prefer the pillar-box rails beside the game image.
        const float rail_l = std::max(0.0f, static_cast<float>(g.x) - left);
        const float rail_r = std::max(0.0f, right - static_cast<float>(g.x + g.width));
        const float lx = rail_l >= 2.0f * (arm + margin)
            ? left + rail_l * 0.5f : left + margin + arm;
        const float half_cluster = cluster_w * 0.5f * mm;
        const float rx = rail_r >= 2.0f * (half_cluster + margin)
            ? right - rail_r * 0.5f : right - margin - half_cluster;
        const float cy = bottom - margin - arm - 6.0f * mm;
        p.dpad.x = lx;
        p.dpad.y = cy;
        p.a.x = rx + button_r * kClusterDx * 0.5f;
        p.a.y = cy - button_r * 0.9f;
        p.b.x = rx - button_r * kClusterDx * 0.5f;
        p.b.y = cy + button_r * 0.9f;
        p.l = {left + margin, top + margin, shoulder_w, shoulder_h};
        p.r = {right - margin - shoulder_w, top + margin, shoulder_w, shoulder_h};
        const float mid = (left + right) * 0.5f;
        p.select = {mid - pill_w - 3.0f * mm, bottom - margin - pill_h, pill_w, pill_h};
        p.start = {mid + 3.0f * mm, bottom - margin - pill_h, pill_w, pill_h};
        p.ff = {p.select.x - 3.0f * mm - pill_w, bottom - margin - pill_h,
                pill_w, pill_h};
    } else {
        // Portrait: a control band at the bottom (the space under the game
        // image when it is tall enough, else overlaid on the image).
        const float band_need = kBandNeed * s * mm;
        float band_top = static_cast<float>(g.y + g.height);
        if (bottom - band_top < band_need) band_top = bottom - band_need;
        const float pills_y = bottom - margin * 0.75f - pill_h;
        // Shoulders on the band's top edge, D-pad and A/B between them and
        // the Start/Select pills.
        p.l = {left + margin, band_top, shoulder_w, shoulder_h};
        p.r = {right - margin - shoulder_w, band_top, shoulder_w, shoulder_h};
        const float cy = (band_top + shoulder_h + pills_y) * 0.5f;
        p.dpad.x = left + margin + arm;
        p.dpad.y = cy;
        p.a.x = right - margin - button_r;
        p.a.y = cy - button_r * 0.9f;
        p.b.x = p.a.x - button_r * kClusterDx;
        p.b.y = cy + button_r * 0.9f;
        const float mid = (left + right) * 0.5f;
        p.select = {mid - pill_w - 3.0f * mm, pills_y, pill_w, pill_h};
        p.start = {mid + 3.0f * mm, pills_y, pill_w, pill_h};
        p.ff = {p.select.x - 3.0f * mm - pill_w, pills_y, pill_w, pill_h};
    }
    const float tw = 8.0f * mm, th = tw * 0.72f, edge = 3.0f * mm;
    float cx = right - edge - tw * 0.5f;
    float cy = bottom - edge - th * 0.5f;
    if (in.toggle_pos_saved) {
        cx = left + in.toggle_fx * (right - left);
        cy = top + in.toggle_fy * (bottom - top);
    }
    cx = std::clamp(cx, left + tw * 0.5f, std::max(left + tw * 0.5f, right - tw * 0.5f));
    cy = std::clamp(cy, top + th * 0.5f, std::max(top + th * 0.5f, bottom - th * 0.5f));
    p.toggle = {cx - tw * 0.5f, cy - th * 0.5f, tw, th};
    return p;
}

// GBA button bits (active-high: bit set = pressed) for a touch point.
// D-pad resolves to one axis (dominant wins, with a centre dead zone);
// sliding between pad buttons follows the finger, so callers re-query on
// every move. Bit order matches KEYINPUT: 0=A 1=B 2=Sel 3=Sta 4=Right
// 5=Left 6=Up 7=Down 8=R 9=L.
inline std::uint16_t pad_buttons_at(const PadLayout& p, float x, float y,
                                    float mm) {
    std::uint16_t buttons = 0;
    const float dx = x - p.dpad.x;
    const float dy = y - p.dpad.y;
    const float reach = p.dpad.r * 1.35f;
    if (std::abs(dx) <= reach && std::abs(dy) <= reach) {
        const float dead = p.dpad.r * 0.18f;
        if (std::abs(dx) > std::abs(dy)) {
            if (dx > dead) buttons |= 1u << 4;   // Right
            if (dx < -dead) buttons |= 1u << 5;  // Left
        } else {
            if (dy < -dead) buttons |= 1u << 6;  // Up
            if (dy > dead) buttons |= 1u << 7;   // Down
        }
    }
    auto in_circle = [x, y](const PadCircle& c, float scale) {
        const float ex = x - c.x, ey = y - c.y;
        const float r = c.r * scale;
        return ex * ex + ey * ey <= r * r;
    };
    if (in_circle(p.a, 1.3f)) buttons |= 1u << 0;
    if (in_circle(p.b, 1.3f)) buttons |= 1u << 1;
    const float pad = 2.0f * mm;
    if (p.select.contains(x, y, pad)) buttons |= 1u << 2;
    if (p.start.contains(x, y, pad)) buttons |= 1u << 3;
    if (p.r.contains(x, y, pad)) buttons |= 1u << 8;
    if (p.l.contains(x, y, pad)) buttons |= 1u << 9;
    return buttons;
}

// Fast-forward pill hit: separate from pad_buttons_at because FF is host
// speed control, not a GBA KEYINPUT bit. Same touch slop as the pills.
inline bool pad_ff_at(const PadLayout& p, float x, float y, float mm) {
    return p.ff.contains(x, y, 2.0f * mm);
}

// Touch policy predicates. Pure so every backend agrees on when touch can
// drive the game and when the overlays show.
inline bool touch_input_enabled(bool touch_controls, bool touch_policy,
                                bool touch_emulation) {
    return touch_controls || touch_policy || touch_emulation;
}

// Pad overlay (buttons) currently shown: player choice minus the idle
// auto-hide state.
inline bool pad_overlay_shown(bool pad_visible, bool touch_controls,
                              bool pad_idle_hidden) {
    return pad_visible && touch_controls && !pad_idle_hidden;
}

// The toggle is offered only when something other than the pad can drive
// the game; a pad-only title must never be left without controls.
inline bool pad_toggle_available(bool touch_policy, bool touch_controls,
                                 bool touch_emulation) {
    return touch_policy && (touch_controls || touch_emulation);
}

inline bool pad_toggle_shown(bool touch_policy, bool touch_controls,
                             bool touch_emulation, bool pad_idle_hidden) {
    return pad_toggle_available(touch_policy, touch_controls,
                                touch_emulation) &&
           !pad_idle_hidden;
}

}  // namespace gbarecomp
