// host_menu.h — host-side wiring for the generic menu (plan §3.4 / Phase 3c).
//
// The host owns a HostMenu; it owns the MenuModel (single source of truth),
// builds the default rows from the backend's current settings, translates
// frame input into navigation and the presentation call, and applies accepted
// changes back to the backend. Backend-neutral: everything goes through the
// V2 facade (WindowBackendV2), so the same wiring serves raylib and the
// host-loop fakes.
//
// Frame contract (matches Menu::present — after backend.pump() and before
// video().present()):
//
//     const Events ev = backend.pump();
//     host.frame(backend, ev, pointer);
//     bus.io().set_keyinput(ev.keyinput);   // already masked while open
//
// While the menu is open the backend masks `ev.keyinput` (its snapshot flag
// derives from the presented model), so the host hands the game the masked
// levels; the menu navigates from `ev.menu_keyinput`, which always carries
// the pre-suppression levels. A host that opens the menu between pump() and
// frame() may additionally mask `ev.keyinput` itself for that one frame.
//
// The open trigger is host policy (Phase 3d). open()/toggle() rebuild the
// values first, so the menu always shows current settings; the rebuild hook
// is where games re-append their rows.
#pragma once

#include <cstdint>

#include "menu_model.h"
#include "window_backend.h"

namespace gbarecomp {

class HostMenu {
public:
    HostMenu() = default;
    HostMenu(const HostMenu&) = delete;
    HostMenu& operator=(const HostMenu&) = delete;

    // The host-owned model. Games append rows with menu_add_row() (never a
    // direct row_count write — the capacity is a hard bound), typically from
    // the rebuild hook.
    MenuModel& model() { return model_; }
    const MenuModel& model() const { return model_; }

    // Rebuild the default rows from the backend's current settings. Resets
    // the model (including any game-appended rows).
    void build(WindowBackendV2& backend);

    // Open/close policy. The trigger is host-side (Phase 3d). open()/toggle()
    // re-seed the values from the backend first.
    void open(WindowBackendV2& backend);
    void close();
    void toggle(WindowBackendV2& backend);

    // One host-loop step: navigation from `ev.menu_keyinput` (the
    // pre-suppression levels; newly-pressed edges only, so a held direction
    // does not scroll at frame rate), then the presentation via
    // backend.menu().present(model, pointer). Applies whatever either
    // produced. Never opens the menu by itself.
    void frame(WindowBackendV2& backend, const Events& ev,
               const PointerState& pointer);

    // While open, the host must suppress game input for the frame (§3.4).
    // The raylib backend derives its snapshot flag from the same state.
    bool suppress_game_input() const { return model_.open; }

    // Rebuild hook (host policy; optional): called at the end of build(),
    // before the model opens — the place for a game to re-append its rows
    // (via menu_add_row) with current values. build() resets the model, so
    // rows added outside this hook do not survive an open/toggle. The hook
    // must not call back into HostMenu (open/toggle/build): builds are not
    // re-entrant.
    void set_rebuild_hook(void (*fn)(void* user, MenuModel& model), void* user) {
        rebuild_hook_ = fn;
        rebuild_user_ = user;
    }

    // Post-apply hook (host policy; optional): called after apply() handled a
    // change or an action the host owns — persist the settings file, apply
    // game-appended rows, run extra actions. Never called for navigation that
    // changed nothing, or for the framework-owned Resume action. A settings
    // change applied with this hook set also shows the "Saved" status.
    void set_apply_hook(void (*fn)(void* user, WindowBackendV2& backend,
                                   const MenuResult& result),
                        void* user) {
        apply_hook_ = fn;
        apply_user_ = user;
    }

    // Apply one MenuResult (public so hosts can feed results they produce
    // themselves, e.g. hotkey-driven changes). Returns true when a change or
    // action was handled.
    bool apply(WindowBackendV2& backend, const MenuResult& result);

private:
    // A result row is addressable when it exists in the model AND fits the
    // binding table — a game that overran row_count must not walk binds_ out
    // of bounds. Out-of-range rows are simply not ours.
    bool row_in_range(int row) const;
    // Which backend setting a default row drives. Game-appended rows stay
    // None; the apply hook owns them.
    enum class Bind : std::uint8_t {
        None = 0,
        Fullscreen,
        Scale,
        LinearFilter,
        FpsReadout,
        Ghosting,
        FrameInterp,
        Lcd,
        LcdBoost,
        LcdDither,
        LcdDeband,
        AudioEnabled,
        Volume,
        Resume,
    };

    MenuModel model_;
    Bind binds_[MenuModel::kMaxRows] = {};
    // Keyinput LEVELS are sampled once per pump; menu navigation fires on the
    // newly-pressed edge only. Active-low: 0x03FF = all released.
    std::uint16_t prev_keys_ = 0x03FF;
    void (*rebuild_hook_)(void*, MenuModel&) = nullptr;
    void* rebuild_user_ = nullptr;
    void (*apply_hook_)(void*, WindowBackendV2&, const MenuResult&) = nullptr;
    void* apply_user_ = nullptr;
};

}  // namespace gbarecomp
