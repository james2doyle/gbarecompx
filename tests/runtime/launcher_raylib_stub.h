// launcher_raylib_stub.h — the observation/scripting seam shared by
// launcher_raylib_stub.cpp (which defines the recorders and the raylib
// surface) and the test that drives the launcher's draw loop headless
// (launcher_raygui_test.cpp).
//
// Every record carries the frame index it happened on (EndDrawing
// advances), so tests script N frames and assert per-frame draws.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gbarecomp {
namespace launcher_stub {

struct DrawnText {
    int frame;
    std::string text;
    float x, y;
    float size;
    std::uint32_t color;  // 0xRRGGBB, alpha dropped
};

struct DrawnRect {
    int frame;
    float x, y, w, h;
    std::uint32_t color;
};

struct DrawnRing {
    int frame;
    float x, y, w, h;
    float thickness;
    std::uint32_t color;
};

struct DrawnClear {
    int frame;
    std::uint32_t color;
};

std::vector<DrawnText>& texts();      // the launcher's own DrawText calls
std::vector<DrawnText>& codepoints(); // raygui's per-glyph DrawTextCodepoint
std::vector<DrawnRect>& rects();      // filled rectangles (rounded + plain)
std::vector<DrawnRing>& rings();      // outlines (focus ring, panel stroke)
std::vector<DrawnClear>& clears();    // ClearBackground per frame
int& window_opens();                  // InitWindow count
int& window_closes();                 // CloseWindow count

// Readable text for one frame: the frame's DrawText strings plus the
// per-glyph codepoint log folded into runs (raygui draws labels glyph by
// glyph — same assembly as the menu harness).
std::vector<std::string> text_runs(int frame);

void reset_all();

// Scripted input. Index = frame; past the end of a vector there is no
// input that frame, and WindowShouldClose() returns true (a run without a
// scripted end cannot loop forever).
struct Script {
    std::vector<std::vector<int>> keys_pressed;
    std::vector<std::vector<int>> mouse_buttons_pressed;
    std::vector<std::pair<float, float>> mouse_position;
    std::vector<bool> should_close;
};

Script& script();
int current_frame();
// Frame-advance hook (EndDrawing in the stub TU).
int& current_frame_int();

}  // namespace launcher_stub
}  // namespace gbarecomp
