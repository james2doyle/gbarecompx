// host_menu.cpp — see host_menu.h.
//
// The default content mirrors the legacy recomp-ui standard catalog
// (recomp-ui/src/common/recomp_runtime_settings.c) where the V2 capabilities
// overlap: Display (Fullscreen, Window scale, Linear filter, Show FPS),
// Graphics (LCD ghosting, Frame interpolation, the LCD shader group when the
// backend can run it), Audio (Audio, Volume), System (Resume game).
#include "host_menu.h"

#include <cstdint>

namespace gbarecomp {

namespace {

// Mirrors the legacy catalog's fullscreen choice values 0..2
// (Video::set_fullscreen clamps to the same range).
const char* const kFullscreenChoices[] = {"Windowed", "Borderless",
                                          "Fullscreen"};

// The View row order is the ViewMode order (Native / Adaptive / Fixed).
// The third state is honestly named "Fixed", not "16:9": it means the view
// is fixed at the current fixed width — which may be 285 (legacy 16:9) or
// any other carried width such as a --view-width 320 CLI run.
const char* const kViewChoices[] = {"Native", "Adaptive", "Fixed"};

// The State section's Slot choice: the menu shows slot NUMBERS; the row's
// int_value is the choice INDEX (slot - 1).
const char* const kStateSlotChoices[kMenuStateSlotMax] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9"};

}  // namespace

void HostMenu::build(WindowBackend& backend) {
    model_ = MenuModel{};
    for (int i = 0; i < MenuModel::kMaxRows; ++i) binds_[i] = Bind::None;

    const Caps caps = backend.caps();
    Video& video = backend.video();
    Fx& fx = backend.fx();
    Audio& audio = backend.audio();
    const FxSettings fx_settings = fx.settings();

    auto section = [this](const char* title) {
        if (model_.section_count < MenuModel::kMaxSections)
            model_.sections[model_.section_count++].title = title;
        return model_.section_count - 1;
    };
    auto add = [this](MenuRow row, Bind bind) {
        const int index = menu_add_row(model_, row);
        if (index < 0) return;  // full: the default catalog cannot overflow
        binds_[index] = bind;
    };

    // ── Display ─────────────────────────────────────────────────────────
    const int display = section("Display");
#if !defined(__ANDROID__)
    {
        MenuRow row;
        row.kind = MenuRowKind::Choice;
        row.label = "Fullscreen";
        row.choices = kFullscreenChoices;
        row.choice_count = 3;
        row.int_value = video.fullscreen();
        row.section = display;
        add(row, Bind::Fullscreen);
    }
    {
        MenuRow row;
        row.kind = MenuRowKind::StepInt;
        row.label = "Window scale";
        row.min_value = 1.0f;
        row.max_value = 8.0f;
        row.step = 1.0f;
        row.int_value = video.scale();
        row.section = display;
        add(row, Bind::Scale);
    }
#endif
    {
        MenuRow row;
        row.kind = MenuRowKind::Toggle;
        row.label = "Linear filter";
        row.int_value = video.linear_filter() ? 1 : 0;
        row.section = display;
        add(row, Bind::LinearFilter);
    }
    {
        // Sharp scaling: integer prescale + bilinear finish at fractional
        // scales (the >3x uneven-pixel fix). Mutually exclusive with the
        // linear filter (sharp wins at resolve time; the apply cases clear
        // the sibling). Plain RT + sampler state, so no Caps::shaders gate.
        MenuRow row;
        row.kind = MenuRowKind::Toggle;
        row.label = "Sharp scaling";
        row.int_value = fx_settings.sharp ? 1 : 0;
        row.section = display;
        add(row, Bind::SharpScaling);
    }
    {
        MenuRow row;
        row.kind = MenuRowKind::Toggle;
        row.label = "Show FPS";
        row.int_value = video.fps_readout() ? 1 : 0;
        row.section = display;
        add(row, Bind::FpsReadout);
    }
    if (view_get_ && view_available_) {
        // Last in Display so the earlier rows keep their positions: the
        // mode lives in the host loop (see set_view_mode_endpoint), the
        // menu only seeds and forwards.
        int seeded = view_get_(view_user_);
        if (seeded < static_cast<int>(ViewMode::Native) ||
            seeded > static_cast<int>(ViewMode::Fixed16x9))
            seeded = static_cast<int>(ViewMode::Native);
        MenuRow row;
        row.kind = MenuRowKind::Choice;
        row.label = "View";
        row.choices = kViewChoices;
        row.choice_count = 3;
        row.int_value = seeded;
        row.section = display;
        add(row, Bind::ViewMode);
    }

    // ── Graphics ────────────────────────────────────────────────────────
    const int graphics = section("Graphics");
    {
        MenuRow row;
        row.kind = MenuRowKind::Toggle;
        row.label = "LCD ghosting (frame blending)";
        row.int_value = fx_settings.frame_blend != 0.0f ? 1 : 0;
        row.section = graphics;
        add(row, Bind::Ghosting);
    }
    {
        MenuRow row;
        row.kind = MenuRowKind::Toggle;
        row.label = "Frame interpolation (experimental)";
        row.int_value = fx_settings.frame_interp ? 1 : 0;
        row.section = graphics;
        add(row, Bind::FrameInterp);
    }
    if (caps.shaders) {
        // The LCD shader group is meaningful only where the backend can run
        // it; the Fx contract stores values regardless.
        {
            // MMPX edge smoothing: style-preserving 2x reconstruction
            // (mmpx.h — JCGT McGuire & Gagiu). A shader pass like the LCD
            // group, so gated the same way; placed first as the image-
            // quality filter the overlays then decorate.
            MenuRow row;
            row.kind = MenuRowKind::Toggle;
            row.label = "Edge smoothing";
            row.int_value = fx_settings.edge_smoothing ? 1 : 0;
            row.section = graphics;
            add(row, Bind::EdgeSmoothing);
        }
        {
            MenuRow row;
            row.kind = MenuRowKind::Toggle;
            row.label = "LCD";
            row.int_value = fx_settings.lcd ? 1 : 0;
            row.section = graphics;
            add(row, Bind::Lcd);
        }
        {
            // Border pixel grid (LADXHD PixelGrid port): a 1-screen-px dark
            // line on every game-pixel boundary. Rides the LCD program; the
            // two effects compose (uMaskMode).
            MenuRow row;
            row.kind = MenuRowKind::Toggle;
            row.label = "Pixel grid";
            row.int_value = fx_settings.lcd_border ? 1 : 0;
            row.section = graphics;
            add(row, Bind::LcdBorder);
        }
        {
            MenuRow row;
            row.kind = MenuRowKind::StepFloat;
            row.label = "Grid opacity";
            row.min_value = 0.0f;  // kLcdMinGridAlpha/MaxGridAlpha (lcd_shader.h)
            row.max_value = 1.0f;
            row.step = 0.05f;
            row.float_value = fx_settings.lcd_grid_alpha;
            row.section = graphics;
            add(row, Bind::LcdGridAlpha);
        }
        {
            MenuRow row;
            row.kind = MenuRowKind::StepFloat;
            row.label = "LCD boost";
            row.min_value = 1.0f;  // lcd_shader.h kLcdMinBoost/MaxBoost
            row.max_value = 3.0f;
            row.step = 0.05f;
            row.float_value = fx_settings.lcd_boost;
            row.section = graphics;
            add(row, Bind::LcdBoost);
        }
        {
            MenuRow row;
            row.kind = MenuRowKind::StepFloat;
            row.label = "LCD dither";
            row.min_value = 0.0f;  // kLcdMinDither/MaxDither
            row.max_value = 2.0f;
            row.step = 0.05f;
            row.float_value = fx_settings.lcd_dither;
            row.section = graphics;
            add(row, Bind::LcdDither);
        }
        {
            MenuRow row;
            row.kind = MenuRowKind::StepFloat;
            row.label = "LCD deband";
            row.min_value = 0.0f;  // kLcdMinDeband/MaxDeband (display LSB)
            row.max_value = 16.0f;
            row.step = 1.0f;
            row.float_value = fx_settings.lcd_deband;
            row.section = graphics;
            add(row, Bind::LcdDeband);
        }
    }

    // ── Audio ───────────────────────────────────────────────────────────
    const int audio_section = section("Audio");
    {
        MenuRow row;
        row.kind = MenuRowKind::Toggle;
        row.label = "Audio";
        row.int_value = audio.enabled() ? 1 : 0;
        row.section = audio_section;
        add(row, Bind::AudioEnabled);
    }
    {
        MenuRow row;
        row.kind = MenuRowKind::StepInt;
        row.label = "Volume";
        row.min_value = 0.0f;
        row.max_value = 100.0f;
        row.step = 5.0f;  // mirrors the recomp-ui standard catalog
        row.int_value = audio.volume();
        row.section = audio_section;
        add(row, Bind::Volume);
    }

    // ── System ──────────────────────────────────────────────────────────
    const int system = section("System");
    {
        // Save-state slots (framework content, every product build): the
        // Slot choice picks 1..9; the Save/Load actions QUEUE ops that the
        // host loop drains into ev.save_slot / ev.load_slot (see
        // drain_menu_state_ops) and executes at its clean dispatch
        // boundary — never from inside a menu callback mid-frame. The
        // executor and slot paths in runtime.cpp are deliberately ungated
        // framework content; the imgui-era menu's version of this feature
        // compiled out with it, which is why the rows carry literal-label
        // pins in the headless suites.
        MenuRow slot_row;
        slot_row.kind = MenuRowKind::Choice;
        slot_row.label = "Slot";
        slot_row.choices = kStateSlotChoices;
        slot_row.choice_count = kMenuStateSlotMax;
        slot_row.int_value = state_slot_ - 1;
        slot_row.section = system;
        add(slot_row, Bind::StateSlot);

        MenuRow save_row;
        save_row.kind = MenuRowKind::Action;
        save_row.label = "Save state";
        save_row.section = system;
        add(save_row, Bind::SaveState);

        MenuRow load_row;
        load_row.kind = MenuRowKind::Action;
        load_row.label = "Load state";
        load_row.section = system;
        add(load_row, Bind::LoadState);
    }
    {
        MenuRow row;
        row.kind = MenuRowKind::Action;
        row.label = "Resume game";
        row.section = system;
        add(row, Bind::Resume);
    }

    // Games re-append their rows here, with values seeded from game state.
    if (rebuild_hook_) rebuild_hook_(rebuild_user_, model_);
}

void HostMenu::open(WindowBackend& backend) {
    build(backend);
    menu_open(model_);
}

void HostMenu::close() { menu_close(model_); }

void HostMenu::toggle(WindowBackend& backend) {
    if (model_.open) {
        close();
    } else {
        open(backend);
    }
}

void HostMenu::frame(WindowBackend& backend, const Events& ev,
                     const PointerState& pointer) {
    // Newly-pressed edges only: active-low, so a bit that was released and is
    // now clear is a fresh press. menu_keyinput, not keyinput: while the menu
    // is open the game-facing levels are masked (the backend derives
    // suppress_game_input from the presented model), but the menu itself must
    // keep navigating.
    const std::uint16_t newly =
        static_cast<std::uint16_t>(prev_keys_ & ~ev.menu_keyinput);
    prev_keys_ = ev.menu_keyinput;

    if (model_.open) {
        MenuInput in;
        if (menu_input_from_keyinput(
                static_cast<std::uint16_t>(0x03FFu & ~newly), &in)) {
            apply(backend, menu_navigate(model_, in));
        }
    }
    // The toast/status clocks are frame counts and the model has no clock
    // (menu_model_test: "the host decrements") — the host ticks them here,
    // every frame, open or closed. The toast draws while the menu is
    // closed, so without this tick "State loaded" rendered forever
    // (device, 2026-10-16); the identical dead status clock was masked by
    // the m.open gate in the presentation and never screamed.
    if (model_.status_frames > 0) --model_.status_frames;
    if (model_.toast_frames > 0) --model_.toast_frames;
    // The presentation runs every frame (a toast draws while closed).
    apply(backend, backend.menu().present(model_, pointer));
}

bool HostMenu::row_in_range(int row) const {
    return row >= 0 && row < model_.row_count && row < MenuModel::kMaxRows;
}

bool HostMenu::apply(WindowBackend& backend, const MenuResult& result) {
    if (result.changed_row >= 0) {
        if (!row_in_range(result.changed_row)) return false;
        const int row = result.changed_row;
        const Bind bind = binds_[row];
        switch (bind) {
        case Bind::None:
            // Game-appended row: the hook applies it from the model.
            break;
        case Bind::Fullscreen:
            backend.video().set_fullscreen(menu_value_int(model_, row));
            menu_set_value_int(model_, row, backend.video().fullscreen());
            break;
        case Bind::Scale:
            backend.video().adjust_scale(menu_value_int(model_, row) -
                                         backend.video().scale());
            menu_set_value_int(model_, row, backend.video().scale());
            break;
        case Bind::LinearFilter:
            backend.video().set_linear_filter(menu_value_int(model_, row) != 0);
            menu_set_value_int(model_, row,
                               backend.video().linear_filter() ? 1 : 0);
            break;
        case Bind::FpsReadout:
            backend.video().set_fps_readout(menu_value_int(model_, row) != 0);
            menu_set_value_int(model_, row,
                               backend.video().fps_readout() ? 1 : 0);
            break;
        case Bind::Ghosting: {
            FxSettings s = backend.fx().settings();
            // Toggle semantics mirror the flat setter: on = full strength.
            s.frame_blend = menu_value_int(model_, row) ? 1.0f : 0.0f;
            backend.fx().apply(s);
            menu_set_value_int(
                model_, row,
                backend.fx().settings().frame_blend != 0.0f ? 1 : 0);
            break;
        }
        case Bind::FrameInterp: {
            FxSettings s = backend.fx().settings();
            s.frame_interp = menu_value_int(model_, row) != 0;
            backend.fx().apply(s);
            menu_set_value_int(model_, row,
                               backend.fx().settings().frame_interp ? 1 : 0);
            break;
        }
        case Bind::EdgeSmoothing: {
            FxSettings s = backend.fx().settings();
            s.edge_smoothing = menu_value_int(model_, row) != 0;
            backend.fx().apply(s);
            menu_set_value_int(model_, row,
                               backend.fx().settings().edge_smoothing ? 1 : 0);
            break;
        }
        case Bind::Lcd: {
            FxSettings s = backend.fx().settings();
            s.lcd = menu_value_int(model_, row) != 0;
            backend.fx().apply(s);
            menu_set_value_int(model_, row,
                               backend.fx().settings().lcd ? 1 : 0);
            break;
        }
        case Bind::SharpScaling: {
            // Mutual exclusion, mirror of set_linear_filter's side: sharp on
            // clears the linear filter (the resolve-time "sharp wins" rule
            // lives in the backend's set_linear_filter; this is the other
            // direction). Linear lives on the Video interface, not FxSettings.
            FxSettings s = backend.fx().settings();
            s.sharp = menu_value_int(model_, row) != 0;
            if (s.sharp && backend.video().linear_filter())
                backend.video().set_linear_filter(false);
            backend.fx().apply(s);
            menu_set_value_int(model_, row,
                               backend.fx().settings().sharp ? 1 : 0);
            break;
        }
        case Bind::LcdBorder: {
            FxSettings s = backend.fx().settings();
            s.lcd_border = menu_value_int(model_, row) != 0;
            backend.fx().apply(s);
            menu_set_value_int(model_, row,
                               backend.fx().settings().lcd_border ? 1 : 0);
            break;
        }
        case Bind::LcdGridAlpha: {
            FxSettings s = backend.fx().settings();
            s.lcd_grid_alpha = menu_value_float(model_, row);
            backend.fx().apply(s);
            menu_set_value_float(model_, row,
                                 backend.fx().settings().lcd_grid_alpha);
            break;
        }
        case Bind::LcdBoost: {
            FxSettings s = backend.fx().settings();
            s.lcd_boost = menu_value_float(model_, row);
            backend.fx().apply(s);
            menu_set_value_float(model_, row,
                                 backend.fx().settings().lcd_boost);
            break;
        }
        case Bind::LcdDither: {
            FxSettings s = backend.fx().settings();
            s.lcd_dither = menu_value_float(model_, row);
            backend.fx().apply(s);
            menu_set_value_float(model_, row,
                                 backend.fx().settings().lcd_dither);
            break;
        }
        case Bind::LcdDeband: {
            FxSettings s = backend.fx().settings();
            s.lcd_deband = menu_value_float(model_, row);
            backend.fx().apply(s);
            menu_set_value_float(model_, row,
                                 backend.fx().settings().lcd_deband);
            break;
        }
        case Bind::AudioEnabled:
            backend.audio().set_enabled(menu_value_int(model_, row) != 0);
            menu_set_value_int(model_, row, backend.audio().enabled() ? 1 : 0);
            break;
        case Bind::Volume:
            backend.audio().set_volume(menu_value_int(model_, row));
            menu_set_value_int(model_, row, backend.audio().volume());
            break;
        case Bind::Resume:
            // Unreachable via changed_row (Action rows return action_row).
            menu_close(model_);
            break;
        case Bind::ViewMode: {
            const int v = menu_value_int(model_, row);
            if (view_set_) view_set_(view_user_, v);
            // Reseed from the host: the setter owns the mode, the model
            // only proposed it (mirrors the Fullscreen reseed above).
            menu_set_value_int(
                model_, row, view_get_ ? view_get_(view_user_) : v);
            break;
        }
        case Bind::StateSlot: {
            // Choice index -> slot number; reseed like the View row (the
            // host owns the slot, the model only proposed it).
            state_slot_ = std::clamp(menu_value_int(model_, row) + 1, 1,
                                     kMenuStateSlotMax);
            menu_set_value_int(model_, row, state_slot_ - 1);
            break;
        }
        }
        if (apply_hook_ && bind != Bind::Resume) {
            menu_set_status(model_, "Saved");
            apply_hook_(apply_user_, backend, result);
        }
        // Handled when the framework applied it or the host took it.
        return bind != Bind::None || apply_hook_ != nullptr;
    }

    if (result.action_row >= 0) {
        if (!row_in_range(result.action_row)) return false;
        const int row = result.action_row;
        if (binds_[row] == Bind::Resume) {
            // Framework-owned: close settings and return to the game.
            menu_close(model_);
            return true;
        }
        if (binds_[row] == Bind::SaveState) {
            // Record and close: the actual save runs on the host loop at
            // the clean dispatch boundary (drain_menu_state_ops -> the
            // ev.save_slot executor), never inside a menu callback
            // mid-frame — the imgui-era menu's documented doctrine.
            pending_save_slot_ = state_slot_;
            menu_close(model_);
            return true;
        }
        if (binds_[row] == Bind::LoadState) {
            pending_load_slot_ = state_slot_;
            menu_close(model_);
            return true;
        }
        if (apply_hook_) {
            // Actions set no status: "Saved" marks a persisted settings
            // change, not a fired action (documented asymmetry, §3.4).
            apply_hook_(apply_user_, backend, result);
            return true;
        }
        return false;
    }
    return false;
}

}  // namespace gbarecomp
