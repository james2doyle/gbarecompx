// test_raygui_links.cpp — compile+link guard for vendored raygui (Step 1.2).
//
// Confirms raygui/raygui.h compiles and links against raylib (via
// gbarecomp_raylib_backend, which owns the single raygui implementation from
// launcher_raygui.cpp). No window is opened, so this runs cleanly headless.
// Taking the address of GuiButton proves the raygui symbols resolve at link
// time; GuiGetStyle/GuiSetStyle are pure style accessors needing no GL context.

#include "raygui/raygui.h"

int main(void) {
    // Link-time symbol check: GuiButton must resolve from raylib+raygui.
    if (&GuiButton == nullptr) return 1;

    // Pure style accessors — no window, no GL context required.
    GuiSetStyle(DEFAULT, TEXT_SIZE, 10);
    if (GuiGetStyle(DEFAULT, TEXT_SIZE) != 10) return 2;

    return 0;
}
