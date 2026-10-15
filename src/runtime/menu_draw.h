// menu_draw.h — the shared menu presentation (plan §3.4): pure drawing over
// HostOverlay primitives, plus the geometry and hit-test that the presenter
// and the pointer share. No backend, no toolkit — the touch_pad_draw.h
// pattern, so it runs under the software renderer and is assertable with a
// recording overlay.
//
// Draw order is fixed by the plan: the menu is the topmost layer, drawn
// after the pad chrome, never inside a shader/scale pass. The panel is
// screen-space (drawable pixels); the toast draws even while the menu is
// closed.
#pragma once

#include "host_overlay.h"
#include "menu_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gbarecomp {

// Panel geometry for one frame. Computed once per frame and shared by the
// hit-test and the draw so they can never disagree (even across a resize).
struct MenuGeometry {
    bool valid = false;
    float scale = 1.0f;    // drawable-px scale (clamped 1..4 of a 480px-high
                           // drawable)
    float panel_x = 0.0f, panel_y = 0.0f, panel_w = 0.0f, panel_h = 0.0f;
    float pad = 0.0f;
    float row_h = 0.0f;
    float text_size = 0.0f;
    float list_y = 0.0f;   // first list line's top (after any header)
    int list_count = 0;    // sections (top level) or storage rows (editing)
    bool header = false;   // editing: the section-title header line
    float status_y = 0.0f;
    bool has_status = false;
};

inline MenuGeometry menu_compute_geometry(const HostOverlay& ov,
                                          const MenuModel& m) {
    MenuGeometry g;
    const float dw = static_cast<float>(ov.drawable_width());
    const float dh = static_cast<float>(ov.drawable_height());
    if (dw <= 0.0f || dh <= 0.0f) return g;
    g.valid = true;
    g.scale = std::clamp(dh / 480.0f, 1.0f, 4.0f);
    g.row_h = std::round(22.0f * g.scale);
    g.text_size = std::round(16.0f * g.scale);
    g.pad = std::round(10.0f * g.scale);
    g.header = m.open && m.editing;
    g.list_count = m.open
                       ? (m.editing
                              ? menu_section_row_count(m, m.section_index)
                              : m.section_count)
                       : 0;
    g.has_status = m.open && m.status_frames > 0;
    const float lines = static_cast<float>(g.list_count + (g.header ? 1 : 0) +
                                           (g.has_status ? 1 : 0));
    g.panel_w = std::min(dw - 2.0f * g.pad, std::round(420.0f * g.scale));
    g.panel_h = 2.0f * g.pad + lines * g.row_h;
    g.panel_x = std::round((dw - g.panel_w) * 0.5f);
    g.panel_y = std::round((dh - g.panel_h) * 0.5f);
    if (g.panel_y < g.pad) g.panel_y = g.pad;
    g.list_y = g.panel_y + g.pad + (g.header ? g.row_h : 0.0f);
    g.status_y = g.list_y + static_cast<float>(g.list_count) * g.row_h;
    return g;
}

// Pointer (drawable pixels) -> global row index, or -1. At the top level a
// press on a section line maps to that section's first selectable row (so
// the press drills in through the shared pointer mutator). Invisible and
// disabled rows are not hit.
inline int menu_hit_test(const MenuGeometry& g, const MenuModel& m, float px,
                         float py) {
    if (!g.valid || !m.open) return -1;
    if (px < g.panel_x || px > g.panel_x + g.panel_w) return -1;
    const float rel = py - g.list_y;
    if (rel < 0.0f) return -1;
    const int index = static_cast<int>(rel / g.row_h);
    if (index < 0 || index >= g.list_count) return -1;
    if (m.editing) {
        const int gi = menu_section_row(m, m.section_index, index);
        if (gi < 0) return -1;
        const MenuRow& row = m.rows[gi];
        if (!row.visible || !row.enabled) return -1;
        return gi;
    }
    return menu_section_first_selectable(m, index);
}

inline void menu_draw(HostOverlay& ov, const MenuModel& m,
                      const MenuGeometry& g, const PointerState& pointer) {
    const bool toast = m.toast_frames > 0 &&
                       (m.toast_title[0] != '\0' || m.toast_body[0] != '\0');
    if (!m.open && !toast) return;

    const float dw = static_cast<float>(ov.drawable_width());
    const float dh = static_cast<float>(ov.drawable_height());
    const OverlaySpace ds = OverlaySpace::Drawable;

    if (m.open && g.valid) {
        // Backdrop dim, then the panel.
        const auto dim_a = static_cast<std::uint8_t>(m.dim * 255.0f);
        if (dim_a > 0)
            ov.fill_rect(0.0f, 0.0f, dw, dh, 0.0f, {0, 0, 0, dim_a}, ds);
        const auto panel_a =
            static_cast<std::uint8_t>(m.opacity * 255.0f);
        ov.fill_rect(g.panel_x, g.panel_y, g.panel_w, g.panel_h,
                     6.0f * g.scale, {18, 20, 30, panel_a}, ds);
        ov.stroke_rect(g.panel_x, g.panel_y, g.panel_w, g.panel_h,
                       6.0f * g.scale, 1.0f * g.scale,
                       {90, 100, 130, panel_a}, ds);

        // Header: the current section's title while editing.
        if (g.header) {
            const char* title =
                (m.section_index >= 0 && m.section_index < m.section_count)
                    ? m.sections[m.section_index].title
                    : nullptr;
            if (title)
                ov.text(g.panel_x + g.pad,
                        g.panel_y + g.pad + (g.row_h - g.text_size) * 0.5f,
                        g.text_size, {170, 180, 210, 255}, title,
                        OverlayAlign::Left, ds);
        }

        // List lines: sections (top level) or rows (editing).
        for (int i = 0; i < g.list_count; ++i) {
            const float y = g.list_y + static_cast<float>(i) * g.row_h;
            const float ty = y + (g.row_h - g.text_size) * 0.5f;

            if (!m.editing) {
                const bool selected = i == m.section_index;
                if (selected)
                    ov.fill_rect(g.panel_x + g.pad * 0.5f, y, g.panel_w - g.pad,
                                 g.row_h, 4.0f * g.scale, {60, 90, 160, 200},
                                 ds);
                const char* title =
                    (i >= 0 && i < m.section_count) ? m.sections[i].title
                                                    : nullptr;
                if (title)
                    ov.text(g.panel_x + g.pad, ty, g.text_size,
                            {235, 235, 240, 255}, title, OverlayAlign::Left, ds);
                continue;
            }

            const int gi = menu_section_row(m, m.section_index, i);
            if (gi < 0) continue;
            const MenuRow& row = m.rows[gi];
            if (!row.visible) continue;
            if (menu_selected_row(m) == gi)
                ov.fill_rect(g.panel_x + g.pad * 0.5f, y, g.panel_w - g.pad,
                             g.row_h, 4.0f * g.scale, {60, 90, 160, 200}, ds);
            const OverlayColor c =
                row.enabled ? OverlayColor{235, 235, 240, 255}
                            : OverlayColor{130, 130, 140, 255};
            if (row.label)
                ov.text(g.panel_x + g.pad, ty, g.text_size, c, row.label,
                        OverlayAlign::Left, ds);

            char buf[32];
            const char* value = nullptr;
            switch (row.kind) {
            case MenuRowKind::Toggle:
                value = row.int_value ? "On" : "Off";
                break;
            case MenuRowKind::StepInt:
                std::snprintf(buf, sizeof(buf), "%d", row.int_value);
                value = buf;
                break;
            case MenuRowKind::StepFloat:
                std::snprintf(buf, sizeof(buf), "%.2f",
                              static_cast<double>(row.float_value));
                value = buf;
                break;
            case MenuRowKind::Choice:
                if (row.choices && row.int_value >= 0 &&
                    row.int_value < row.choice_count)
                    value = row.choices[row.int_value];
                break;
            case MenuRowKind::Action:
            case MenuRowKind::Info:
                break;
            }
            if (value)
                ov.text(g.panel_x + g.panel_w - g.pad, ty, g.text_size, c,
                        value, OverlayAlign::Right, ds);
        }

        // Status line.
        if (g.has_status)
            ov.text(g.panel_x + g.pad,
                    g.status_y + (g.row_h - g.text_size) * 0.5f, g.text_size,
                    {200, 220, 170, 255}, m.status, OverlayAlign::Left, ds);
    }

    // Touch position marker (feedback only; touch origin).
    if (m.open && g.valid && pointer.touch && pointer.down) {
        const float r = 5.0f * g.scale;
        ov.fill_rect(pointer.x - r, pointer.y - r, 2.0f * r, 2.0f * r, r,
                     {255, 255, 255, 120}, ds);
    }

    // Toast: drawn whether or not the menu is open.
    if (toast) {
        const float s = g.valid ? g.scale : 1.0f;
        const float y = std::round(16.0f * s);
        if (m.toast_title[0])
            ov.text(dw * 0.5f, y, std::round(18.0f * s),
                    {255, 240, 180, 255}, m.toast_title, OverlayAlign::Center,
                    ds);
        if (m.toast_body[0])
            ov.text(dw * 0.5f, y + std::round(26.0f * s),
                    std::round(14.0f * s), {230, 230, 235, 255}, m.toast_body,
                    OverlayAlign::Center, ds);
    }
}

}  // namespace gbarecomp
