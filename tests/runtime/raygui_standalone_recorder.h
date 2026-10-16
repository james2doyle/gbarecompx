// raygui_standalone_recorder.h — the observation seam shared by
// raygui_standalone_stub.cpp (which defines the recorders) and the tests
// that assert against them (menu_engine_raygui_test.cpp).
//
// THIS 5.1-DEV SNAPSHOT'S STANDALONE SHAPE: GuiDrawText/GuiDrawRectangle
// are internal statics (implemented in raygui.h itself) — the user
// primitives they bottom out in are DrawRectangle() and per-codepoint
// DrawTextCodepoint(). Those two are the recorders. Text runs are
// assembled from the codepoint log (group by line y, order by x, split on
// gaps) so tests assert readable strings.
#pragma once

#include "raygui/raygui.h"

#include <string>
#include <vector>

namespace gbarecomp {
namespace testing {

struct DrawnRect {
    int x = 0, y = 0, w = 0, h = 0;
    Color color{};
};

struct TextRun {
    std::string text;
    Vector2 position{};
    float font_size = 0.0f;
    Color color{};
};

std::vector<DrawnRect>& drawn_rects();
std::vector<TextRun>& text_runs();
void reset_recording();
// Fold the per-codepoint log into text runs — call after each engine draw,
// before asserting on text_runs().
void assemble_text_runs();

}  // namespace testing
}  // namespace gbarecomp
