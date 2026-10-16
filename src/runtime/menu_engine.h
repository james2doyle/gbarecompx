// menu_engine.h — the swappable in-game menu presentation engine (contract).
//
// The V2 `Menu` backend seam (window_backend.h) renders through one of these
// engines instead of hard-coding a toolkit. The contract is deliberately a
// PURE RENDERER — no input, no state, no mutation:
//
//   - Navigation is NOT here. The host drives keyboard/gamepad navigation
//     through menu_navigate() (host_menu.cpp) exactly as before; the model is
//     the single source of truth and the engine only pictures it. There is no
//     mouse in the menu: widgets are display elements, and a pointer-driven
//     presenter (raygui's own hit-testing) is out of contract.
//   - draw() runs inside the backend's frame context (Begin/EndDrawing) and
//     may run more than once per pump — frame-interpolation ticks subdivide
//     the guest wait with extra presents. Engines must render identically on
//     every call within a pump (the raygui engine gets this for free via
//     GuiLock(): locked widgets render normally but never interact).
//   - draw() receives the HostOverlay surface so any engine can mix raw
//     shapes (backdrop dim, selection highlight) with toolkit widgets and so
//     no engine needs a geometry parameter — dims, insets and scale come from
//     the surface.
//   - Engines mutate nothing: no MenuModel writes, no backend calls. Menu
//     results reach the host only through menu_navigate()'s MenuResult.
//
// make_menu_engine() is the selection point: the raygui engine in raylib
// builds, the legacy menu_draw.h renderer otherwise (it is also the
// fallback/reference if the toolkit engine misbehaves on some platform).
#pragma once

#include <memory>

#include "host_overlay.h"
#include "menu_model.h"

namespace gbarecomp {

class MenuEngine {
public:
    virtual ~MenuEngine() = default;

    // Gates Menu::available() (== Caps::menu) alongside the backend's own
    // build gates.
    virtual bool available() const = 0;

    // Render `model` in screen space. Called zero or more times per pump,
    // always inside the backend's draw context. A closed model with a live
    // toast must still draw its toast; a closed model with neither draws
    // nothing.
    virtual void draw(HostOverlay& surface, const MenuModel& model) = 0;
};

// The legacy presentation: menu_draw.h over the surface, verbatim (minus the
// touch marker — there is no pointer in the menu). Pure, headless-testable
// over a recording overlay.
class LegacyMenuEngine final : public MenuEngine {
public:
    bool available() const override;
    void draw(HostOverlay& surface, const MenuModel& model) override;
};

// The raygui presentation: real widgets (GuiToggle/GuiSpinner/GuiSliderBar/
// GuiComboBox/GuiButton) laid out label-left/value-right, drawn locked.
// Compiled only in GBARECOMP_USE_RAYLIB builds.
class RayguiMenuEngine final : public MenuEngine {
public:
    bool available() const override;
    void draw(HostOverlay& surface, const MenuModel& model) override;
};

// Engine selection point (see header comment).
std::unique_ptr<MenuEngine> make_menu_engine();

}  // namespace gbarecomp
