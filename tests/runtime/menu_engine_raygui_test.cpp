// menu_engine_raygui_test.cpp — the raygui menu engine, driven HEADLESS.
//
// The raygui engine could only be compile-checked before (its draw needs a
// raygui frame context, and the real build's raygui is welded to raylib).
// The RAYGUI_STANDALONE path (raygui_standalone_stub.cpp) breaks that
// weld: raygui's implementation runs with recording primitives, so the
// engine's rendering DECISIONS are assertable here — what text each row
// kind draws, the wording, the per-draw theme restore.
//
// First assertion on file: toggle rows read "On"/"Off" — the legacy
// renderer's exact wording (menu_draw.h). The wording was untested in BOTH
// engines when the raygui engine shipped (menu_draw_test draws a toggle
// but never asserts its text; the raygui engine had no test at all) and a
// wording change landed unobserved.
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG; see BRINGUP
// "vacuous test" finding.
#include "menu_engine.h"
#include "menu_model.h"
#include "raygui_standalone_recorder.h"

#include "host_menu.h"
#include "support/fake_backend.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

#include "host_overlay.h"
#include "presentation_layout.h"

namespace {

using gbarecomp::HostOverlay;
using gbarecomp::MenuModel;
using gbarecomp::MenuRow;
using gbarecomp::MenuRowKind;
using gbarecomp::MenuSection;
using gbarecomp::OverlayAlign;
using gbarecomp::OverlayColor;
using gbarecomp::OverlayPoint;
using gbarecomp::OverlaySpace;
using gbarecomp::PresentationLayout;
using gbarecomp::RayguiMenuEngine;

// Recording overlay: the engine's raw shapes (backdrop dim, accent bar)
// land here; the toolkit widgets land in the stub's recorders.
class RecordingOverlay final : public HostOverlay {
public:
    int drawable_width() const override { return 720; }
    int drawable_height() const override { return 480; }
    PresentationLayout game_rect() const override {
        PresentationLayout l;
        l.x = 60;
        l.y = 40;
        l.width = 240;
        l.height = 160;
        return l;
    }
    int view_width() const override { return 240; }
    int view_height() const override { return 160; }
    float drawable_px_per_mm() const override { return 4.0f; }
    gbarecomp::HostOverlay::Insets safe_insets() const override { return {}; }
    std::uint32_t host_ms() const override { return 0; }
    void to_drawable(float x, float y, OverlaySpace space, float* dx,
                     float* dy) const override {
        if (dx) *dx = x;
        if (dy) *dy = y;
        (void)space;
    }
    void line(float, float, float, float, float, OverlayColor,
              OverlaySpace) override {}
    void polyline(const OverlayPoint*, std::size_t, float, OverlayColor,
                  OverlaySpace) override {}
    void fill_rect(float x, float y, float w, float h, float, OverlayColor,
                   OverlaySpace) override {
        rects.push_back({x, y, w, h});
    }
    void stroke_rect(float, float, float, float, float, float, OverlayColor,
                     OverlaySpace) override {}
    void fill_circle(float, float, float, OverlayColor, OverlaySpace) override {
    }
    void stroke_circle(float, float, float, float, OverlayColor,
                       OverlaySpace) override {}
    void arc(float, float, float, float, float, float, OverlayColor,
             OverlaySpace) override {}
    bool text_supported() const override { return true; }
    void text(float, float, float, OverlayColor, const char* utf8,
              OverlayAlign, OverlaySpace) override {
        texts.emplace_back(utf8 ? utf8 : "");
    }
    float text_width(const char*, float, OverlaySpace) const override {
        return 8.0f;
    }

    struct Rect {
        float x, y, w, h;
    };
    std::vector<Rect> rects;
    std::vector<std::string> texts;
};

bool has_text(const std::string& needle) {
    gbarecomp::testing::assemble_text_runs();
    for (const auto& t : gbarecomp::testing::text_runs())
        if (t.text == needle) return true;
    return false;
}

MenuModel two_toggle_model(bool first_on) {
    MenuModel m;
    menu_open(m);
    m.sections[0].title = "Test";
    m.section_count = 1;
    m.editing = true;  // rows render only in the editing view

    MenuRow a;
    a.kind = MenuRowKind::Toggle;
    a.label = "Auto-run";
    a.section = 0;
    a.int_value = first_on ? 1 : 0;
    CHECK(menu_add_row(m, a) >= 0);
    MenuRow b;
    b.kind = MenuRowKind::Toggle;
    b.label = "Widescreen";
    b.section = 0;
    b.int_value = first_on ? 0 : 1;
    CHECK(menu_add_row(m, b) >= 0);
    return m;
}

}  // namespace

int main() {
    RayguiMenuEngine engine;
    CHECK(engine.available());
    RecordingOverlay ov;

    // The editing view renders the rows of section 0. Seed the selection on
    // the first row so the accent bar lands on a widget row too.
    {
        MenuModel m = two_toggle_model(/*first_on=*/true);
        m.row_index = 0;
        gbarecomp::testing::reset_recording();
        engine.draw(ov, m);

        // The wording: "On" for the on-row, "Off" for the off-row — the
        // legacy renderer's exact strings (menu_draw.h), riding the toggle
        // widgets themselves.
        CHECK(has_text("On"));
        CHECK(has_text("Off"));
        CHECK(!has_text("on") && !has_text("off"));  // exact casing
        // The row labels and the section title draw too.
        CHECK(has_text("Auto-run"));
        CHECK(has_text("Widescreen"));
        CHECK(has_text("Test"));
        // The engine drew widget chrome (toggles are rectangles).
        CHECK(!gbarecomp::testing::drawn_rects().empty());
    }

    // The inverse seeding flips the strings.
    {
        MenuModel m = two_toggle_model(/*first_on=*/false);
        gbarecomp::testing::reset_recording();
        engine.draw(ov, m);
        CHECK(has_text("On"));
        CHECK(has_text("Off"));
    }

    // Repeat draws within a pump are free (the engine is a pure renderer):
    // a second draw records the same runs, no state carried.
    {
        MenuModel m = two_toggle_model(true);
        gbarecomp::testing::reset_recording();
        engine.draw(ov, m);
        gbarecomp::testing::assemble_text_runs();
        const auto first = gbarecomp::testing::text_runs().size();
        engine.draw(ov, m);
        gbarecomp::testing::assemble_text_runs();
        CHECK(gbarecomp::testing::text_runs().size() == first * 2);
        CHECK(has_text("On") && has_text("Off"));
    }

    // Closed with no toast draws nothing.
    {
        MenuModel m = two_toggle_model(true);
        CHECK(menu_close(m).closed);
        gbarecomp::testing::reset_recording();
        ov.rects.clear();
        ov.texts.clear();
        engine.draw(ov, m);
        gbarecomp::testing::assemble_text_runs();
        CHECK(gbarecomp::testing::text_runs().empty());
        CHECK(gbarecomp::testing::drawn_rects().empty());
        CHECK(ov.texts.empty() && ov.rects.empty());
    }

    // A draw heals a STALE font binding. raygui loads its style lazily,
    // once per process — the launcher fires that init inside the launcher's
    // GL context, CloseWindow destroys that font texture, and after boot
    // the overlay sampled the dead texture: shapes rendered, text didn't.
    // The engine therefore re-runs GuiLoadStyleDefault on every draw, so a
    // binding pointing anywhere but the live default font is re-bound
    // before anything reads it (and when it's already current, the reload
    // is a no-op — no redundant texture work). The harness reproduces the
    // real state by poisoning the binding with a foreign texture id.
    {
        MenuModel m = two_toggle_model(/*first_on=*/true);
        gbarecomp::testing::reset_recording();
        gbarecomp::testing::corrupt_font_texture(4242);
        engine.draw(ov, m);
        CHECK(gbarecomp::testing::font_texture_id() == 0);  // healed
        CHECK(gbarecomp::testing::shapes_texture_binds() == 1);
        engine.draw(ov, m);
        CHECK(gbarecomp::testing::font_texture_id() == 0);  // stays healed
        CHECK(gbarecomp::testing::shapes_texture_binds() == 1);  // idempotent
    }

    // The dead-context case that the id-only guard used to miss: the driver
    // hands the new default font the same texture id the launcher's font
    // had. guiFont.texture.id matches, but recs/glyphs still point at the
    // old context's tables. The explicit live-font bind on every draw heals
    // it; text must still be recorded.
    {
        MenuModel m = two_toggle_model(/*first_on=*/true);
        gbarecomp::testing::reset_recording();
        gbarecomp::testing::corrupt_font_same_texture();
        CHECK(!gbarecomp::testing::font_binding_is_default());
        engine.draw(ov, m);
        CHECK(gbarecomp::testing::font_binding_is_default());
        gbarecomp::testing::assemble_text_runs();
        CHECK(!gbarecomp::testing::text_runs().empty());
    }

    // The State rows render through THIS engine too (the desktop product
    // path): a HostMenu-built model — the product path, not a fixture —
    // drawn and asserted at the run layer. The run assembler reconstructs
    // word gaps, so multi-word labels assert exactly; a missing row, an
    // invisible row, or a label change fails here.
    {
        gbarecomp::testing::FakeBackend backend;
        CHECK(backend.open(gbarecomp::OpenParams{}).ok());
        gbarecomp::HostMenu host;
        host.build(backend);
        host.open(backend);

        gbarecomp::testing::reset_recording();
        engine.draw(ov, host.model());  // top level: the section list
        CHECK(has_text("Save states"));

        MenuModel& m = host.model();
        int save_states = -1;
        for (int i = 0; i < m.section_count; ++i)
            if (m.sections[i].title &&
                std::strcmp(m.sections[i].title, "Save states") == 0)
                save_states = i;
        CHECK(save_states >= 0);
        m.editing = true;
        m.section_index = save_states;
        m.row_index = 0;
        gbarecomp::testing::reset_recording();
        engine.draw(ov, m);
        CHECK(has_text("Slot"));
        CHECK(has_text("Save state"));
        CHECK(has_text("Load state"));
        CHECK(has_text("1"));  // the seeded slot value
        CHECK(has_text("Resume game"));
    }

    std::printf("menu_engine_raygui_test: PASS (toggle wording On/Off, "
                "labels, title, chrome, repeat-draw, closed-silence, "
                "per-draw font state)\n");
    return 0;
}
