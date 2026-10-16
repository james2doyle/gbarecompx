// menu_draw_test.cpp — Phase 3b: the shared menu presentation (plan §3.4).
//
// Drives the pure drawing/geometry/hit-test layer over a RECORDING
// HostOverlay: the plan's "assertable with a recording overlay". No window,
// no raylib, no backend.
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG; see BRINGUP
// "vacuous test" finding.
#include "menu_draw.h"
#include "menu_model.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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

using gbarecomp::HostOverlay;
using gbarecomp::MenuModel;
using gbarecomp::MenuRow;
using gbarecomp::MenuRowKind;
using gbarecomp::OverlayAlign;
using gbarecomp::OverlayColor;
using gbarecomp::OverlayPoint;
using gbarecomp::OverlaySpace;
using gbarecomp::PointerState;
using gbarecomp::PresentationLayout;

// A HostOverlay that records every call. Geometry: 720x480 drawable,
// 240x160 view, a centered 4:3 game rect.
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
    void to_drawable(float x, float y, OverlaySpace space, float* dx,
                     float* dy) const override {
        *dx = x;
        *dy = y;
        (void)space;
    }

    void line(float, float, float, float, float, OverlayColor,
              OverlaySpace) override {}
    void polyline(const OverlayPoint*, std::size_t, float, OverlayColor,
                  OverlaySpace) override {}
    void fill_rect(float x, float y, float w, float h, float rounding,
                   OverlayColor color, OverlaySpace space) override {
        rects.push_back({x, y, w, h, rounding, color, space});
    }
    void stroke_rect(float x, float y, float w, float h, float rounding,
                     float thickness, OverlayColor color,
                     OverlaySpace space) override {
        strokes.push_back({x, y, w, h, rounding, color, space});
        (void)thickness;
    }
    void fill_circle(float, float, float, OverlayColor, OverlaySpace) override {}
    void stroke_circle(float, float, float, float, OverlayColor,
                       OverlaySpace) override {}
    void arc(float, float, float, float, float, float, OverlayColor,
             OverlaySpace) override {}
    bool text_supported() const override { return true; }
    void text(float x, float y, float size, OverlayColor color, const char* utf8,
              OverlayAlign align, OverlaySpace space) override {
        texts.push_back({x, y, size, color, utf8 ? utf8 : "", align, space});
    }
    float text_width(const char* utf8, float size,
                     OverlaySpace space) const override {
        (void)space;
        return utf8 ? static_cast<float>(std::strlen(utf8)) * size * 0.6f : 0.0f;
    }

    struct Rect {
        float x, y, w, h, rounding;
        OverlayColor color;
        OverlaySpace space;
    };
    struct Text {
        float x, y, size;
        OverlayColor color;
        std::string utf8;
        OverlayAlign align;
        OverlaySpace space;
    };
    std::vector<Rect> rects;
    std::vector<Rect> strokes;
    std::vector<Text> texts;

    bool has_text(const char* needle) const {
        for (const auto& t : texts)
            if (t.utf8 == needle) return true;
        return false;
    }
    int text_count(const char* needle) const {
        int n = 0;
        for (const auto& t : texts)
            if (t.utf8 == needle) ++n;
        return n;
    }
};

const char* const kViewChoices[] = {"Native", "16:9", "Adaptive"};

MenuModel make_model() {
    MenuModel m;
    m.sections[0].title = "Gameplay";
    m.sections[1].title = "System";
    m.section_count = 2;

    MenuRow toggle;
    toggle.kind = MenuRowKind::Toggle;
    toggle.label = "Auto-run";
    toggle.section = 0;
    toggle.int_value = 1;  // ON
    m.rows[0] = toggle;

    MenuRow level;
    level.kind = MenuRowKind::StepInt;
    level.label = "Level";
    level.section = 0;
    level.min_value = 0.0f;
    level.max_value = 5.0f;
    level.step = 1.0f;
    level.int_value = 3;
    m.rows[1] = level;

    MenuRow hidden;
    hidden.kind = MenuRowKind::Info;
    hidden.label = "Hidden row";
    hidden.section = 0;
    hidden.visible = false;
    m.rows[2] = hidden;

    MenuRow action;
    action.kind = MenuRowKind::Action;
    action.label = "Save now";
    action.section = 1;
    m.rows[3] = action;

    m.row_count = 4;
    return m;
}

void geometry_shape() {
    MenuModel m = make_model();
    RecordingOverlay ov;
    m.open = true;  // the list geometry is only meaningful while open
    const gbarecomp::MenuGeometry g = gbarecomp::menu_compute_geometry(
        ov, m);
    CHECK(g.valid);

    // Top level: two sections listed; the list starts at the panel's pad.
    CHECK(g.list_count == 2);
    CHECK(g.list_y == g.panel_y + g.pad);  // absolute: no header at top level
    // Panel centered horizontally.
    CHECK(g.panel_x > 0.0f && g.panel_x + g.panel_w <= 720.0f);
    CHECK(g.row_h > 0.0f);
    // The panel fits within the drawable.
    CHECK(g.panel_y >= 0.0f && g.panel_y + g.panel_h <= 480.0f);

    // Editing: the list is the section's rows (all storage rows, the
    // invisible one included in the geometry; the draw skips it) and the
    // header occupies the panel's first line — an ABSOLUTE check, because a
    // hit-test computed relative to list_y would move with the bug.
    m.open = true;
    m.editing = true;
    m.section_index = 0;
    const gbarecomp::MenuGeometry e = gbarecomp::menu_compute_geometry(
        ov, m);
    CHECK(e.list_count == 3);  // section 0 has three storage rows
    CHECK(e.header);
    CHECK(e.list_y == e.panel_y + e.pad + e.row_h);
}

void hit_test_rows_and_sections() {
    MenuModel m = make_model();
    RecordingOverlay ov;
    m.open = true;

    // Top level: a press on the second section's line maps to its first
    // selectable row (the Action, global 3).
    gbarecomp::MenuGeometry g = gbarecomp::menu_compute_geometry(ov, m);
    const float y1 = g.list_y + g.row_h * 1.5f;
    CHECK(gbarecomp::menu_hit_test(g, m, g.panel_x + 10.0f, y1) == 3);
    // Above the list / outside the panel: no hit.
    CHECK(gbarecomp::menu_hit_test(g, m, 2.0f, 2.0f) == -1);
    CHECK(gbarecomp::menu_hit_test(g, m, g.panel_x + 10.0f,
                                   g.list_y - g.row_h) == -1);

    // Editing: rows map to themselves (invisible rows are not hit).
    m.editing = true;
    m.section_index = 0;
    g = gbarecomp::menu_compute_geometry(ov, m);
    CHECK(gbarecomp::menu_hit_test(g, m, g.panel_x + 10.0f,
                                   g.list_y + g.row_h * 0.5f) == 0);
    CHECK(gbarecomp::menu_hit_test(g, m, g.panel_x + 10.0f,
                                   g.list_y + g.row_h * 1.5f) == 1);
    CHECK(gbarecomp::menu_hit_test(g, m, g.panel_x + 10.0f,
                                   g.list_y + g.row_h * 2.5f) == -1);  // hidden
}

void draw_top_level() {
    MenuModel m = make_model();
    RecordingOverlay ov;
    m.open = true;
    const gbarecomp::MenuGeometry g = gbarecomp::menu_compute_geometry(ov, m);
    gbarecomp::menu_draw(ov, m, g, PointerState{});

    // Backdrop + panel + section titles; the selected section highlighted.
    CHECK(ov.rects.size() >= 2);
    CHECK(ov.has_text("Gameplay") && ov.has_text("System"));
    CHECK(!ov.has_text("Auto-run"));  // rows only draw when editing
    // The highlight (a filled rect inside the panel) is present.
    bool highlight = false;
    for (const auto& r : ov.rects)
        if (r.w < g.panel_w && r.h <= g.row_h + 1.0f) highlight = true;
    CHECK(highlight);
}

void draw_editing_rows_and_values() {
    MenuModel m = make_model();
    RecordingOverlay ov;
    m.open = true;
    m.editing = true;
    m.section_index = 0;
    m.row_index = 1;  // Level selected
    const gbarecomp::MenuGeometry g = gbarecomp::menu_compute_geometry(ov, m);
    gbarecomp::menu_draw(ov, m, g, PointerState{});

    // The section title header + the visible rows' labels + values.
    CHECK(ov.has_text("Gameplay"));
    CHECK(ov.has_text("Auto-run"));
    CHECK(ov.has_text("Level"));
    CHECK(!ov.has_text("Hidden row"));  // invisible rows are not drawn
    CHECK(ov.has_text("On"));           // Toggle value
    CHECK(ov.has_text("3"));            // StepInt value

    // The SELECTED row (Level, row_index 1) carries the highlight — checked
    // at its absolute y, so dropping the highlight cannot pass.
    bool row_highlight = false;
    for (const auto& r : ov.rects) {
        if (r.w < g.panel_w && r.h <= g.row_h + 1.0f &&
            r.y == g.list_y + g.row_h)
            row_highlight = true;
    }
    CHECK(row_highlight);

    // The Off wording is asserted too (both halves of the toggle value:
    // the raygui engine mirrors these exact strings — menu_engine_raygui_test).
    m.rows[0].int_value = 0;  // OFF
    RecordingOverlay ov_off;
    const gbarecomp::MenuGeometry g_off =
        gbarecomp::menu_compute_geometry(ov_off, m);
    gbarecomp::menu_draw(ov_off, m, g_off, PointerState{});
    CHECK(ov_off.has_text("Off"));
    CHECK(!ov_off.has_text("On"));

    // A disabled row draws dimmed (its label color differs from an enabled
    // row's).
    m.rows[0].enabled = false;
    RecordingOverlay ov2;
    gbarecomp::menu_draw(ov2, m, g, PointerState{});
    OverlayColor enabled_c{}, disabled_c{};
    for (const auto& t : ov2.texts) {
        if (t.utf8 == "Level") enabled_c = t.color;
        if (t.utf8 == "Auto-run") disabled_c = t.color;
    }
    CHECK(disabled_c.r != enabled_c.r || disabled_c.a != enabled_c.a);
}

void draw_choice_and_float_and_status() {
    MenuModel m = make_model();
    MenuRow choice;
    choice.kind = MenuRowKind::Choice;
    choice.label = "View";
    choice.section = 0;
    choice.choices = kViewChoices;
    choice.choice_count = 3;
    choice.int_value = 2;
    m.rows[4] = choice;

    MenuRow flt;
    flt.kind = MenuRowKind::StepFloat;
    flt.label = "Gamma";
    flt.section = 0;
    flt.min_value = 0.0f;
    flt.max_value = 2.0f;
    flt.step = 0.25f;
    flt.float_value = 1.25f;
    m.rows[5] = flt;
    m.row_count = 6;

    m.open = true;
    m.editing = true;
    m.section_index = 0;
    gbarecomp::menu_set_status(m, "Saved");

    RecordingOverlay ov;
    const gbarecomp::MenuGeometry g = gbarecomp::menu_compute_geometry(ov, m);
    gbarecomp::menu_draw(ov, m, g, PointerState{});
    CHECK(ov.has_text("Adaptive"));  // choice index 2
    CHECK(ov.has_text("1.25"));      // float formatted
    CHECK(ov.has_text("Saved"));     // status line
}

void draw_toast_when_closed_and_nothing_when_idle() {
    MenuModel m = make_model();
    RecordingOverlay ov;
    const gbarecomp::MenuGeometry g = gbarecomp::menu_compute_geometry(ov, m);

    // Closed + no toast: nothing at all.
    gbarecomp::menu_draw(ov, m, g, PointerState{});
    CHECK(ov.rects.empty() && ov.texts.empty());

    // Closed + toast: the toast draws (top-center), still no panel.
    gbarecomp::menu_set_toast(m, "Achievement", "You found the menu");
    RecordingOverlay ov2;
    gbarecomp::menu_draw(ov2, m, g, PointerState{});
    CHECK(ov2.has_text("Achievement") && ov2.has_text("You found the menu"));
    CHECK(!ov2.has_text("Gameplay"));  // no panel while closed
}

void pointer_position_feedback() {
    MenuModel m = make_model();
    RecordingOverlay ov;
    m.open = true;
    m.editing = true;
    m.section_index = 0;
    const gbarecomp::MenuGeometry g = gbarecomp::menu_compute_geometry(ov, m);

    // A touch pointer draws a marker (the trail pattern); a mouse pointer
    // does not.
    PointerState p;
    p.touch = true;
    p.down = true;
    p.x = g.panel_x + 10.0f;
    p.y = g.list_y + g.row_h * 0.5f;
    gbarecomp::menu_draw(ov, m, g, p);
    bool marker = false;
    for (const auto& r : ov.rects)
        if (r.w <= 12.0f && r.h <= 12.0f) marker = true;
    CHECK(marker);

    RecordingOverlay ov2;
    p.touch = false;
    gbarecomp::menu_draw(ov2, m, g, p);
    bool marker2 = false;
    for (const auto& r : ov2.rects)
        if (r.w <= 12.0f && r.h <= 12.0f) marker2 = true;
    CHECK(!marker2);
}

}  // namespace

int main() {
    geometry_shape();
    hit_test_rows_and_sections();
    draw_top_level();
    draw_editing_rows_and_values();
    draw_choice_and_float_and_status();
    draw_toast_when_closed_and_nothing_when_idle();
    pointer_position_feedback();

    std::printf("menu_draw tests: PASS\n");
    return 0;
}
