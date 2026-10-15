// Touch pad drawing. Pure: a recording fake overlay captures every primitive
// the shared draw functions emit, so the layout->pixels contract is verified
// with no window and no GPU.

#include "touch_pad_draw.h"

#include <cstdio>
#include <string>
#include <vector>

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

// Records every primitive. Geometry-only assertions need no rendering.
struct FakeOverlay final : public HostOverlay {
    struct Call {
        std::string kind;
        float x = 0, y = 0, w = 0, h = 0, r = 0;
        OverlayColor color{};
    };
    std::vector<Call> calls;
    bool text_on = false;

    int drawable_width() const override { return 960; }
    int drawable_height() const override { return 640; }
    PresentationLayout game_rect() const override { return {240, 0, 480, 320, 0}; }
    int view_width() const override { return 240; }
    int view_height() const override { return 160; }
    float drawable_px_per_mm() const override { return 6.0f; }
    Insets safe_insets() const override { return {}; }
    std::uint32_t host_ms() const override { return 0; }
    void to_drawable(float x, float y, OverlaySpace, float* dx,
                     float* dy) const override {
        *dx = x;
        *dy = y;
    }
    void line(float, float, float, float, float, OverlayColor,
              OverlaySpace) override {}
    void polyline(const OverlayPoint*, std::size_t, float, OverlayColor,
                  OverlaySpace) override {}
    void fill_rect(float x, float y, float w, float h, float rounding,
                   OverlayColor color, OverlaySpace) override {
        calls.push_back({"fill_rect", x, y, w, h, rounding, color});
    }
    void stroke_rect(float x, float y, float w, float h, float rounding,
                     float thickness, OverlayColor color,
                     OverlaySpace) override {
        calls.push_back({"stroke_rect", x, y, w, h, rounding, color});
        (void)thickness;
    }
    void fill_circle(float cx, float cy, float radius, OverlayColor color,
                     OverlaySpace) override {
        calls.push_back({"fill_circle", cx, cy, 0, 0, radius, color});
    }
    void stroke_circle(float cx, float cy, float radius, float thickness,
                       OverlayColor color, OverlaySpace) override {
        calls.push_back({"stroke_circle", cx, cy, 0, 0, radius, color});
        (void)thickness;
    }
    void arc(float, float, float, float, float, float, OverlayColor,
             OverlaySpace) override {}
    bool text_supported() const override { return text_on; }
    void text(float, float, float, OverlayColor, const char*, OverlayAlign,
              OverlaySpace) override {}
    float text_width(const char*, float, OverlaySpace) const override {
        return 0.0f;
    }
};

PadLayoutInputs landscape() {
    PadLayoutInputs in;
    in.drawable_w = 960.0f;
    in.drawable_h = 640.0f;
    in.px_per_mm = 6.0f;
    in.game = {240, 0, 480, 320, 0};
    return in;
}

int count_kind(const FakeOverlay& ov, const char* kind) {
    int n = 0;
    for (const auto& c : ov.calls)
        if (c.kind == kind) ++n;
    return n;
}

// Was a primitive issued covering point (x, y)?
bool covers(const FakeOverlay& ov, float x, float y) {
    for (const auto& c : ov.calls) {
        if (c.kind == "fill_rect") {
            if (x >= c.x && x <= c.x + c.w && y >= c.y && y <= c.y + c.h)
                return true;
        } else if (c.kind == "fill_circle") {
            const float dx = x - c.x, dy = y - c.y;
            if (dx * dx + dy * dy <= c.r * c.r) return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    const PadLayout layout = compute_pad_layout(landscape());

    // ---- pad draws its full control set --------------------------------------
    {
        FakeOverlay ov;
        draw_touch_pad(ov, layout, 0, 6.0f, false);
        check(!ov.calls.empty(), "pad emits primitives");
        check(count_kind(ov, "fill_circle") == 2, "two round buttons (A/B)");
        check(count_kind(ov, "stroke_circle") == 2, "both buttons outlined");
        // 2 dpad backing + 5 pills (L/R/select/start/FF) + highlights
        check(covers(ov, layout.dpad.x, layout.dpad.y), "dpad cross drawn");
        check(covers(ov, layout.a.x, layout.a.y), "A button drawn");
        check(covers(ov, layout.start.x + 1, layout.start.y + 1),
              "Start pill drawn");
        check(covers(ov, layout.l.x + 1, layout.l.y + 1), "L shoulder drawn");
    }

    // ---- pressed controls highlight gold ---------------------------------------
    {
        FakeOverlay idle, held;
        draw_touch_pad(idle, layout, 0, 6.0f, false);
        draw_touch_pad(held, layout, 1u << 0, 6.0f, false);   // A held
        bool gold_idle = false, gold_held = false;
        for (const auto& c : idle.calls)
            if (c.kind == "fill_circle" && c.color.r == 255 &&
                c.color.g == 214 && c.color.b == 74)
                gold_idle = true;
        for (const auto& c : held.calls)
            if (c.kind == "fill_circle" && c.color.r == 255 &&
                c.color.g == 214 && c.color.b == 74)
                gold_held = true;
        check(!gold_idle, "nothing gold when nothing held");
        check(gold_held, "held A fills gold");
    }

    // ---- toggle draws in both states ----------------------------------------------
    {
        FakeOverlay ov;
        draw_pad_toggle(ov, layout, false, true);
        check(!ov.calls.empty(), "toggle emits primitives");
        bool glyph = false;
        for (const auto& c : ov.calls)
            if (c.kind == "fill_circle") glyph = true;
        check(glyph, "toggle gamepad glyph drawn");
    }

    // ---- fast-forward pill: drawn, teal at rest, gold while held ------------
    {
        FakeOverlay idle, held_ff;
        draw_touch_pad(idle, layout, 0, 6.0f, false);
        draw_touch_pad(held_ff, layout, 0, 6.0f, true);
        check(covers(idle, layout.ff.x + layout.ff.w * 0.5f,
                     layout.ff.y + layout.ff.h * 0.5f),
              "FF pill drawn");
        auto pill_color = [](const FakeOverlay& ov, float x, float y) {
            OverlayColor found{0, 0, 0, 0};
            for (const auto& c : ov.calls)
                if (c.kind == "fill_rect" && x >= c.x && x <= c.x + c.w &&
                    y >= c.y && y <= c.y + c.h)
                    found = c.color;
            return found;
        };
        const float fx = layout.ff.x + layout.ff.w * 0.5f;
        const float fy = layout.ff.y + layout.ff.h * 0.5f;
        const OverlayColor rest = pill_color(idle, fx, fy);
        check(rest.r == 40 && rest.g == 180 && rest.b == 170,
              "FF pill teal at rest (not a game button)");
        const OverlayColor hot = pill_color(held_ff, fx, fy);
        check(hot.r == 255 && hot.g == 214 && hot.b == 74,
              "FF pill gold while held");
    }

    std::printf("touch_pad_draw tests: %s (%d failure(s))\n",
                failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
