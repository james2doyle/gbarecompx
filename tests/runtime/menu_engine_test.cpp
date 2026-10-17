// menu_engine_test.cpp — the MenuEngine contract (menu_engine.h).
//
// The contract is a PURE RENDERER: no mutation, no pointer, identical draws
// on repeat calls within a pump. Driven headless over a recording overlay,
// same shape as menu_draw_test.cpp. The raygui engine is compile-checked by
// the backend target itself (raylib builds); the legacy engine is asserted
// here because it is pure and headless-testable.
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG; see BRINGUP
// "vacuous test" finding.
#include "menu_engine.h"
#include "menu_model.h"

#include "host_menu.h"
#include "support/fake_backend.h"

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
using gbarecomp::MenuSection;
using gbarecomp::OverlayAlign;
using gbarecomp::OverlayColor;
using gbarecomp::OverlayPoint;
using gbarecomp::OverlaySpace;
using gbarecomp::PresentationLayout;

// Same recording overlay as menu_draw_test.cpp (720x480 drawable).
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
    void stroke_rect(float, float, float, float, float, float, OverlayColor,
                     OverlaySpace) override {}
    void fill_circle(float, float, float, OverlayColor, OverlaySpace) override {
    }
    void stroke_circle(float, float, float, float, OverlayColor,
                       OverlaySpace) override {}
    void arc(float, float, float, float, float, float, OverlayColor,
             OverlaySpace) override {}
    bool text_supported() const override { return true; }
    void text(float x, float y, float size, OverlayColor color,
              const char* utf8, OverlayAlign align,
              OverlaySpace space) override {
        texts.push_back({x, y, size, color, utf8 ? utf8 : "", align, space});
    }
    float text_width(const char* utf8, float size,
                     OverlaySpace space) const override {
        (void)space;
        return utf8 ? static_cast<float>(std::strlen(utf8)) * size * 0.6f
                    : 0.0f;
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
    std::vector<Text> texts;
};

// A model with every row kind, seeded values, and a closed-by-default state.
MenuModel make_model() {
    MenuModel m;
    m.section_count = 2;
    m.sections[0] = MenuSection{"Display"};
    m.sections[1] = MenuSection{"Audio"};
    MenuRow row;
    row.kind = MenuRowKind::Toggle;
    row.label = "Linear filter";
    row.int_value = 1;
    row.section = 0;
    (void)gbarecomp::menu_add_row(m, row);
    row.kind = MenuRowKind::StepInt;
    row.label = "Scale";
    row.min_value = 1.0f;
    row.max_value = 8.0f;
    row.int_value = 3;
    row.section = 0;
    (void)gbarecomp::menu_add_row(m, row);
    static const char* const kChoices[] = {"Windowed", "Borderless",
                                           "Fullscreen"};
    row.kind = MenuRowKind::Choice;
    row.label = "Fullscreen";
    row.choices = kChoices;
    row.choice_count = 3;
    row.int_value = 2;
    row.section = 1;
    (void)gbarecomp::menu_add_row(m, row);
    row.kind = MenuRowKind::Action;
    row.label = "Resume game";
    row.section = 1;
    (void)gbarecomp::menu_add_row(m, row);
    row.kind = MenuRowKind::Info;
    row.label = "Version 1.5";
    row.section = 1;
    (void)gbarecomp::menu_add_row(m, row);
    return m;
}

}  // namespace

int main(void) {
    using gbarecomp::LegacyMenuEngine;
    using gbarecomp::make_menu_engine;

    // ── the contract: a pure renderer ──────────────────────────────────
    {
        MenuModel m = make_model();
        m.open = true;
        m.editing = true;  // drill in: rows, not sections
        const MenuModel before = m;  // bitwise snapshot for the no-mutation
                                     // check (the model is trivially copyable)

        RecordingOverlay ov;
        LegacyMenuEngine engine;
        CHECK(engine.available());

        engine.draw(ov, m);
        const std::size_t rects_after_1 = ov.rects.size();
        const std::size_t texts_after_1 = ov.texts.size();
        CHECK(rects_after_1 > 0);
        CHECK(texts_after_1 > 0);

        // Identical repeat draws within a pump (interp ticks): the nth call
        // must picture the model exactly like the first.
        ov.rects.clear();
        ov.texts.clear();
        engine.draw(ov, m);
        CHECK(ov.rects.size() == rects_after_1);
        CHECK(ov.texts.size() == texts_after_1);

        // No mutation: the model is byte-identical after draws.
        CHECK(std::memcmp(&before, &m, sizeof(MenuModel)) == 0);
    }

    // ── closed + toast ─────────────────────────────────────────────────
    {
        MenuModel m = make_model();  // closed
        RecordingOverlay ov;
        LegacyMenuEngine engine;

        // Closed with no toast: nothing draws.
        engine.draw(ov, m);
        CHECK(ov.rects.empty());
        CHECK(ov.texts.empty());

        // Closed with a live toast: the toast draws.
        gbarecomp::menu_set_toast(m, "Saved", "config.ini written");
        engine.draw(ov, m);
        CHECK(!ov.texts.empty());
        bool saw_title = false;
        for (const auto& t : ov.texts)
            if (t.utf8 == "Saved") saw_title = true;
        CHECK(saw_title);
    }

    // ── the factory: non-null in every build ───────────────────────────
    {
        auto engine = make_menu_engine();
        CHECK(engine != nullptr);
        CHECK(engine->available());
    }

    // ── the State rows render: existence pinned at the DRAWN layer ────
    // The model comes from HostMenu::build (the product path), not a
    // fixture: if the host stops appending the rows, marks them invisible,
    // or a label changes, these drawn-string pins fail. The System section
    // shows at the top level; its rows draw in the editing view with the
    // seeded slot value.
    {
        gbarecomp::testing::FakeBackend backend;
        CHECK(backend.open(gbarecomp::OpenParams{}).ok());
        gbarecomp::HostMenu host;
        host.build(backend);
        host.open(backend);

        RecordingOverlay ov;
        LegacyMenuEngine engine;

        engine.draw(ov, host.model());  // top level: the section list
        bool saw_system = false;
        for (const auto& t : ov.texts)
            if (t.utf8 == "System") saw_system = true;
        CHECK(saw_system);

        MenuModel& m = host.model();
        int sys = -1;
        for (int i = 0; i < m.section_count; ++i)
            if (m.sections[i].title &&
                std::strcmp(m.sections[i].title, "System") == 0)
                sys = i;
        CHECK(sys >= 0);
        m.editing = true;
        m.section_index = sys;
        m.row_index = 0;
        ov.texts.clear();
        engine.draw(ov, m);
        auto has = [&](const char* s) {
            for (const auto& t : ov.texts)
                if (t.utf8 == s) return true;
            return false;
        };
        CHECK(has("Slot"));
        CHECK(has("Save state"));
        CHECK(has("Load state"));
        CHECK(has("1"));  // the seeded slot value
        CHECK(has("Resume game"));  // the section's exit row, still last
    }

    std::printf("menu_engine_tests: PASS\n");
    return 0;
}
