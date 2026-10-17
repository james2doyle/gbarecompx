// host_menu.h — host-side wiring for the generic menu (plan §3.4 / Phase 3c).
//
// The host owns a HostMenu; it owns the MenuModel (single source of truth),
// builds the default rows from the backend's current settings, translates
// frame input into navigation and the presentation call, and applies accepted
// changes back to the backend. Backend-neutral: everything goes through the
// V2 facade (WindowBackend), so the same wiring serves raylib and the
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

#include <algorithm>
#include <cstdint>

#include "menu_model.h"
#include "window_backend.h"

namespace gbarecomp {

// View-mode vocabulary for the endpoint below (menu order = numeric order).
// The host owns the mode (it also owns the ppu geometry the mode drives);
// the menu only seeds from get() and forwards changes to set().
enum class ViewMode : int { Native = 0, Adaptive = 1, Fixed16x9 = 2 };

// The desktop menu-hotkey (Delete by default) policy, extracted so the host
// loop and the tests share it: an edge while closed opens, an edge while
// open closes, anything else does nothing. Edge-triggered upstream, so a
// hold flips exactly once; a hotkey is never game input, so neither press
// leaks into the game.
enum class MenuToggleAction { None, Open, Close };

inline MenuToggleAction menu_toggle_step(bool toggle_edge, bool menu_open) {
    if (!toggle_edge) return MenuToggleAction::None;
    return menu_open ? MenuToggleAction::Close : MenuToggleAction::Open;
}

// The save-state slot vocabulary: slots 1..9, the same paths the assist
// scripts and the (retired) F1..F9 hotkeys used — states/<rom>.state<N>.
inline constexpr int kMenuStateSlotMax = 9;

// The View dirty step sync_resize_driven_view consumes: a framework mode
// maps to either drawable-driven geometry (Adaptive), 240 (Native), or a
// fixed width (Fixed). `fixed_width` carries the actual fixed width — the
// CLI's --view-width or the saved setting; 0 means no width was carried,
// so a legacy 16:9-shaped fixed mode falls back to 285. Pure so the policy
// is testable without a window; the host loop applies the result.
struct MenuViewGeometry {
    bool resize_driven = false;
    int fixed_width = 240;
};

inline MenuViewGeometry menu_view_step(ViewMode mode, int max_view_width,
                                       int fixed_width) {
    if (mode == ViewMode::Adaptive) return {true, -1};
    int fixed = 240;
    if (mode == ViewMode::Fixed16x9)
        fixed = fixed_width > 0 ? fixed_width : (160 * 16 + 8) / 9;
    fixed = std::clamp(fixed, 240, std::max(240, max_view_width));
    return {false, fixed};
}

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
    void build(WindowBackend& backend);

    // Open/close policy. The trigger is host-side (Phase 3d). open()/toggle()
    // re-seed the values from the backend first.
    void open(WindowBackend& backend);
    void close();
    void toggle(WindowBackend& backend);

    // One host-loop step: navigation from `ev.menu_keyinput` (the
    // pre-suppression levels; newly-pressed edges only, so a held direction
    // does not scroll at frame rate), then the presentation via
    // backend.menu().present(model, pointer). Applies whatever either
    // produced. Never opens the menu by itself.
    void frame(WindowBackend& backend, const Events& ev,
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
    void set_apply_hook(void (*fn)(void* user, WindowBackend& backend,
                                   const MenuResult& result),
                        void* user) {
        apply_hook_ = fn;
        apply_user_ = user;
    }

    // View-mode endpoint (host policy; optional): the View row
    // (Native / Adaptive / 16:9) is framework content, but the mode lives in
    // the host loop next to the geometry it drives. When set with
    // available=true, build() appends the row seeded from get() (clamped to
    // the ViewMode range), and apply() forwards accepted changes to set() —
    // which the host applies on its frame (e.g. through a dirty flag the
    // present path already polls). Null get disarms. Games keep the rebuild
    // hook for their own rows; this endpoint is the host's own.
    void set_view_mode_endpoint(int (*get)(void*), void (*set)(void*, int),
                                void* user, bool available) {
        view_get_ = get;
        view_set_ = set;
        view_user_ = user;
        view_available_ = available;
    }

    // Save-state slot (framework content): the State section's Slot choice
    // seeds from here and updates it; the host persists it (the apply hook
    // fires on the change) and seeds it back at startup. 1..kMenuStateSlotMax.
    void set_state_slot(int slot) {
        state_slot_ = std::clamp(slot, 1, kMenuStateSlotMax);
    }
    int state_slot() const { return state_slot_; }

    // Take and clear the ops the Save/Load action rows queued. Returns true
    // when either slot is nonzero. The HOST LOOP calls this right after
    // frame() — see drain_menu_state_ops — and the executor runs the op at
    // that loop's clean dispatch boundary, never inside a menu callback
    // mid-frame (the same doctrine the imgui-era menu documented).
    bool take_pending_state_ops(int* save_slot, int* load_slot) {
        const bool any = pending_save_slot_ != 0 || pending_load_slot_ != 0;
        if (save_slot) {
            *save_slot = pending_save_slot_;
            pending_save_slot_ = 0;
        }
        if (load_slot) {
            *load_slot = pending_load_slot_;
            pending_load_slot_ = 0;
        }
        return any;
    }

    // Apply one MenuResult (public so hosts can feed results they produce
    // themselves, e.g. hotkey-driven changes). Returns true when a change or
    // action was handled.
    bool apply(WindowBackend& backend, const MenuResult& result);

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
        SharpScaling,
        FpsReadout,
        Ghosting,
        FrameInterp,
        EdgeSmoothing,
        Lcd,
        LcdBorder,
        LcdGridAlpha,
        LcdBoost,
        LcdDither,
        LcdDeband,
        AudioEnabled,
        Volume,
        Resume,
        ViewMode,
        StateSlot,
        SaveState,
        LoadState,
    };

    MenuModel model_;
    Bind binds_[MenuModel::kMaxRows] = {};
    // Keyinput LEVELS are sampled once per pump; menu navigation fires on the
    // newly-pressed edge only. Active-low: 0x03FF = all released.
    std::uint16_t prev_keys_ = 0x03FF;
    void (*rebuild_hook_)(void*, MenuModel&) = nullptr;
    void* rebuild_user_ = nullptr;
    void (*apply_hook_)(void*, WindowBackend&, const MenuResult&) = nullptr;
    void* apply_user_ = nullptr;
    int (*view_get_)(void*) = nullptr;
    void (*view_set_)(void*, int) = nullptr;
    void* view_user_ = nullptr;
    bool view_available_ = false;
    // Save-state slot state (see set_state_slot / take_pending_state_ops).
    int state_slot_ = 1;
    int pending_save_slot_ = 0;
    int pending_load_slot_ = 0;
};

// Fold the menu's queued state ops into the Events the host loop executes
// at its dispatch boundary — the same ev.save_slot / ev.load_slot
// vocabulary the assist scripts write, so exactly one executor handles
// every save/load regardless of how the player asked for it. Writes only
// what fired; a hotkey/assist request already in `ev` survives. Pure and
// headless-testable; the runtime loop's call site (right after
// host_menu.frame) is pinned by tools/check_menu_state_wiring.py.
inline void drain_menu_state_ops(HostMenu& host, Events& ev) {
    int save = 0, load = 0;
    if (host.take_pending_state_ops(&save, &load)) {
        if (save) ev.save_slot = save;
        if (load) ev.load_slot = load;
    }
}

}  // namespace gbarecomp
