// menu_engine_legacy.cpp — the legacy menu presentation as a MenuEngine.
//
// Wraps menu_draw.h verbatim: geometry, drawing, hit-test helpers all stay
// untouched behind the contract. This is the reference engine (and the
// fallback when the toolkit engine is unavailable); its recording-overlay
// test suite (menu_draw_test.cpp) stays live against it.
//
// The one intentional difference from menu_draw.h's menu_draw(): the touch
// position marker is dropped. There is no mouse in the menu (the contract is
// a pure renderer); the marker only ever showed where a touch press would
// hit-test.
//
// make_menu_engine() lives here too: the raylib build compiles the raygui
// branch (GBARECOMP_USE_RAYLIB is PUBLIC on gbarecomp_raylib_backend, which
// links this TU); stub builds compile the legacy branch.
#include "menu_engine.h"

#include "menu_draw.h"

namespace gbarecomp {

bool LegacyMenuEngine::available() const { return true; }

void LegacyMenuEngine::draw(HostOverlay& surface, const MenuModel& model) {
    menu_draw(surface, model, menu_compute_geometry(surface, model),
              PointerState{});
}

std::unique_ptr<MenuEngine> make_menu_engine() {
#if defined(GBARECOMP_USE_RAYLIB)
    return std::make_unique<RayguiMenuEngine>();
#else
    return std::make_unique<LegacyMenuEngine>();
#endif
}

}  // namespace gbarecomp
