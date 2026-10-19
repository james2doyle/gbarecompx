// menu_engine_raygui.cpp — the raygui menu presentation engine.
//
// Renders the generic menu (MenuModel) with real raygui widgets instead of
// the shared rectangle renderer (menu_draw.h): GuiPanel backdrop,
// GuiLabel/GuiButton rows, GuiToggle / GuiSpinner / GuiSliderBar+value /
// GuiComboBox values, GuiStatusBar status line. The selection is a small
// accent bar drawn AFTER the widgets (widget bases are opaque, and raygui
// has no focus concept to piggyback on).
//
// Contract notes (menu_engine.h):
//   - PURE RENDERER. Every widget call runs under GuiLock(): locked widgets
//     render normally but their pointer-update path never runs (raygui
//     checks guiLocked before hit-testing). There is no mouse in the menu;
//     keyboard/gamepad navigation flows through menu_navigate() at the host,
//     and the model is the single source of truth. Locking also makes repeat
//     draws (interp ticks present more than once per pump) free: a locked
//     widget cannot double-fire.
//   - HostOverlay& is the surface: the accent bar and the backdrop dim are
//     raw shapes; dims, insets and scale come from the surface.
//   - The toast draws with the menu open or closed, exactly as the legacy
//     engine.
//
// All widget values are local copies seeded from the model — locked widgets
// cannot write through them, and engines mutate nothing.
//
// The dark theme is applied (and restored) per draw: raygui's default light
// palette on this panel reads as a skin change, not a redesign — labels and
// widgets each carry their own style slots, so DEFAULT alone is not enough.
#include "menu_engine.h"

#if defined(GBARECOMP_USE_RAYLIB)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "raygui/raygui.h"

namespace gbarecomp {
namespace {

// Layout constants (legacy menu_draw.h proportions, at scale = clamped
// drawable height / 480).
constexpr float kRowH = 22.0f;      // row pitch
constexpr float kTextSize = 16.0f;  // label glyph height
constexpr float kPad = 10.0f;       // panel padding
constexpr float kPanelW = 420.0f;   // panel width (pre-scale)

// The menu theme. hex = RRGGBBAA (raygui GetColor byte order). Chosen to sit
// on the legacy presentation's palette (panel 18/20/30, selection 60/90/160,
// label 235/235/240) so the switch reads as a skin change, not a redesign.
constexpr unsigned int kPanelBg = 0x12141EFF;
constexpr unsigned int kPanelBorder = 0x5A6482FF;
constexpr unsigned int kHeaderText = 0xAAB4D2FF;
constexpr unsigned int kLabelText = 0xEBEBF0FF;
constexpr unsigned int kLabelDim = 0x82828CFF;
constexpr unsigned int kWidgetBorder = 0x5A6482FF;
constexpr unsigned int kWidgetBase = 0x2A3450FF;     // quiet control base
constexpr unsigned int kWidgetBasePressed = 0x3C5AA0FF;  // selection blue
constexpr unsigned int kWidgetText = 0xEBEBF0FF;
constexpr unsigned int kWidgetTextDisabled = 0x82828CFF;
constexpr unsigned int kStatusText = 0xC8DCAAFF;

// Every control the engine draws (plus LABEL — labels read their own text
// slots, not DEFAULT's). Order is fixed; the theme save/restore arrays are
// indexed against it. NOTE: no SPINNER — raygui 5.x removed the GuiSpinner
// control id ("using BUTTON + VALUEBOX properties", raygui.h changelog), so
// the spinner is themed through those two slots.
const int kThemedControls[] = {
    LABEL,   BUTTON, TOGGLE, SLIDER,
    COMBOBOX, VALUEBOX, STATUSBAR,
};
constexpr int kThemedControlCount =
    static_cast<int>(sizeof(kThemedControls) / sizeof(kThemedControls[0]));
// GuiControlProperty color slots (0..11): 3 border + 3 base + 3 text +
// BORDER_WIDTH is 12 (not themed), TEXT_COLOR_DISABLED = 11.
constexpr int kThemedProps[] = {
    BORDER_COLOR_NORMAL, BORDER_COLOR_FOCUSED, BORDER_COLOR_PRESSED,
    BASE_COLOR_NORMAL,   BASE_COLOR_FOCUSED,   BASE_COLOR_PRESSED,
    TEXT_COLOR_NORMAL,   TEXT_COLOR_FOCUSED,   TEXT_COLOR_PRESSED,
    TEXT_COLOR_DISABLED,
};
constexpr int kThemedPropCount =
    static_cast<int>(sizeof(kThemedProps) / sizeof(kThemedProps[0]));

float menu_scale(const HostOverlay& ov) {
    const float dh = static_cast<float>(ov.drawable_height());
    if (dh <= 0.0f) return 1.0f;
    return std::clamp(dh / 480.0f, 1.0f, 4.0f);
}

// Visible-within-section index of the selected row (editing), for the accent
// bar. -1 when the selection is not a visible row of this section.
int selected_visible_index(const MenuModel& m) {
    const int selected = menu_selected_row(m);
    if (selected < 0 || selected >= m.row_count) return -1;
    int vis = 0;
    for (int i = 0; i < m.row_count; ++i) {
        const MenuRow& row = m.rows[i];
        if (row.section != m.section_index || !row.visible) continue;
        if (i == selected) return vis;
        ++vis;
    }
    return -1;
}

void draw_accent_bar(HostOverlay& surface, float x, float y, float row_h) {
    // Selection blue (the legacy row fill), as a left-edge bar over the
    // widgets — always visible, no per-widget style juggling.
    surface.fill_rect(x, y, std::max(3.0f, row_h * 0.18f), row_h, 1.0f,
                      {60, 90, 160, 255}, OverlaySpace::Drawable);
}

}  // namespace

bool RayguiMenuEngine::available() const { return true; }

void RayguiMenuEngine::draw(HostOverlay& surface, const MenuModel& m) {
    const bool toast = m.toast_frames > 0 &&
                       (m.toast_title[0] != '\0' || m.toast_body[0] != '\0');
    if (!m.open && !toast) return;

    const float dw = static_cast<float>(surface.drawable_width());
    const float dh = static_cast<float>(surface.drawable_height());
    if (dw <= 0.0f || dh <= 0.0f) return;
    const float s = menu_scale(surface);
    const float row_h = std::round(kRowH * s);
    const float text_size = std::round(kTextSize * s);
    const float pad = std::round(kPad * s);

    // Re-establish raygui's font/style state in THIS draw's GL context
    // BEFORE anything reads or writes it. raygui loads its style lazily,
    // once per process — but the launcher (same process, earlier window)
    // fires that init inside the launcher's context; CloseWindow then
    // destroys that font texture, and after boot this overlay sampled the
    // dead texture: shapes rendered, text didn't. raylib reloads the
    // default font on every InitWindow (rcore.c LoadFontDefault), so
    // re-running the load here re-binds the live one; the engine's own
    // styles land right after, and the save/restore below still sees a
    // clean default state.
    GuiLoadStyleDefault();
    // The gbarecompx raygui guard below rebinds the live default when the
    // glyph table identity differs, even if the driver reused the launcher
    // texture id — the case the old id-only check missed.

    // Global style: dark palette + scaled text + opacity. Restored on the
    // way out so the launcher (same raygui instance, same process) keeps its
    // look.
    int saved_theme[kThemedControlCount][kThemedPropCount];
    for (int c = 0; c < kThemedControlCount; ++c)
        for (int p = 0; p < kThemedPropCount; ++p)
            saved_theme[c][p] =
                GuiGetStyle(kThemedControls[c], kThemedProps[p]);
    const int saved_text_size = GuiGetStyle(DEFAULT, TEXT_SIZE);
    const int saved_bg = GuiGetStyle(DEFAULT, BACKGROUND_COLOR);
    const int saved_line = GuiGetStyle(DEFAULT, LINE_COLOR);

    GuiLock();  // display-only: widgets render but never interact
    GuiSetAlpha(m.opacity);
    GuiSetStyle(DEFAULT, TEXT_SIZE, static_cast<int>(text_size));
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, (int)kPanelBg);
    GuiSetStyle(DEFAULT, LINE_COLOR, (int)kPanelBorder);
    for (int c = 0; c < kThemedControlCount; ++c) {
        GuiSetStyle(kThemedControls[c], BASE_COLOR_NORMAL, (int)kWidgetBase);
        GuiSetStyle(kThemedControls[c], BASE_COLOR_PRESSED,
                    (int)kWidgetBasePressed);
        GuiSetStyle(kThemedControls[c], TEXT_COLOR_NORMAL, (int)kWidgetText);
        GuiSetStyle(kThemedControls[c], TEXT_COLOR_FOCUSED, (int)kWidgetText);
        GuiSetStyle(kThemedControls[c], TEXT_COLOR_PRESSED,
                    (int)kWidgetText);
        GuiSetStyle(kThemedControls[c], TEXT_COLOR_DISABLED,
                    (int)kWidgetTextDisabled);
    }
    GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, (int)kLabelText);
    GuiSetStyle(LABEL, TEXT_COLOR_DISABLED, (int)kLabelDim);
    GuiSetStyle(STATUSBAR, TEXT_COLOR_NORMAL, (int)kStatusText);
    GuiSetStyle(BUTTON, TEXT_COLOR_NORMAL, (int)kWidgetText);
    // Header bar text (GuiPanel draws its title through GuiStatusBar).
    GuiSetStyle(STATUSBAR, BASE_COLOR_NORMAL, (int)kPanelBorder);

    if (m.open) {
        // Backdrop dim under everything (raw shape — matches legacy).
        const auto dim_a = static_cast<std::uint8_t>(m.dim * 255.0f);
        if (dim_a > 0)
            surface.fill_rect(0.0f, 0.0f, dw, dh, 0.0f, {0, 0, 0, dim_a},
                              OverlaySpace::Drawable);

        // Panel geometry: same proportions as the legacy renderer.
        const int row_count =
            m.editing ? menu_section_row_count(m, m.section_index)
                      : m.section_count;
        const bool header = m.editing;
        const bool has_status = m.status_frames > 0;
        const float lines = static_cast<float>(row_count +
                                               (header ? 1 : 0) +
                                               (has_status ? 1 : 0));
        const float panel_w = std::min(dw - 2.0f * pad,
                                       std::round(420.0f * s));
        const float panel_h = 2.0f * pad + lines * row_h;
        const float panel_x = std::round((dw - panel_w) * 0.5f);
        float panel_y = std::round((dh - panel_h) * 0.5f);
        if (panel_y < pad) panel_y = pad;

        const char* title = nullptr;
        if (header && m.section_index >= 0 &&
            m.section_index < m.section_count)
            title = m.sections[m.section_index].title;
        GuiPanel(Rectangle{panel_x, panel_y, panel_w, panel_h}, title);

        // Body rows start under the panel's header bar when it has a title
        // (RAYGUI_WINDOWBOX_STATUSBAR_HEIGHT = 24).
        float y = panel_y + pad + (header ? 24.0f * s : 0.0f);
        const float label_w = (panel_w - 2.0f * pad) * 0.55f;
        const float value_x = panel_x + pad + label_w;
        const float value_w = panel_w - 2.0f * pad - label_w;
        const float bar_x = panel_x + pad * 0.25f;

        // Section list (top level): one GuiButton per section.
        if (!m.editing) {
            for (int i = 0; i < row_count && i < m.section_count; ++i) {
                (void)GuiButton(
                    Rectangle{panel_x + pad, y, panel_w - 2.0f * pad, row_h},
                    m.sections[i].title ? m.sections[i].title : "");
                if (i == m.section_index)
                    draw_accent_bar(surface, bar_x, y, row_h);
                y += row_h;
            }
        } else {
            for (int i = 0; i < m.row_count; ++i) {
                const MenuRow& row = m.rows[i];
                if (row.section != m.section_index || !row.visible) continue;
                const float ty = y + (row_h - text_size) * 0.5f;
                if (!row.enabled) GuiSetState(STATE_DISABLED);

                // Label column (left).
                GuiLabel(Rectangle{panel_x + pad, ty, label_w, text_size},
                         row.label ? row.label : "");

                // Value column (right): widget per kind. Values are local
                // copies; locked widgets cannot change them.
                switch (row.kind) {
                case MenuRowKind::Toggle: {
                    bool on = row.int_value != 0;
                    // The value text rides the toggle itself — "On"/"Off",
                    // the legacy renderer's exact wording (menu_draw.h).
                    // GuiToggle centers its text on the control, and the
                    // active base (selection blue) carries the state color.
                    (void)GuiToggle(
                        Rectangle{value_x, ty, value_w, row_h},
                        on ? "On" : "Off", &on);
                    break;
                }
                case MenuRowKind::StepInt:
                    if (row.max_value > row.min_value) {
                        int v = row.int_value;
                        (void)GuiSpinner(
                            Rectangle{value_x, ty, value_w, row_h}, nullptr,
                            &v, static_cast<int>(row.min_value),
                            static_cast<int>(row.max_value), false);
                    }
                    break;
                case MenuRowKind::StepFloat:
                    if (row.max_value > row.min_value) {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "%.2f",
                                      static_cast<double>(row.float_value));
                        // GuiSliderBar does not draw its value; the formatted
                        // value rides as textLeft (right-aligned against the
                        // bar's left edge).
                        float v = row.float_value;
                        (void)GuiSliderBar(
                            Rectangle{value_x, ty, value_w, row_h}, buf,
                            nullptr, &v, row.min_value, row.max_value);
                    }
                    break;
                case MenuRowKind::Choice:
                    // GuiTextSplit on an empty string yields zero items and
                    // GuiComboBox then indexes items[-1] — never call it
                    // without choices.
                    if (row.choice_count > 0 && row.choices) {
                        std::string joined;
                        for (int c = 0; c < row.choice_count; ++c) {
                            if (c > 0) joined += ';';
                            joined += row.choices[c] ? row.choices[c] : "";
                        }
                        int v = row.int_value;
                        (void)GuiComboBox(
                            Rectangle{value_x, ty, value_w, row_h},
                            joined.c_str(), &v);
                    }
                    break;
                case MenuRowKind::Action:
                    (void)GuiButton(
                        Rectangle{value_x, ty, value_w, row_h},
                        row.label ? row.label : "");
                    break;
                case MenuRowKind::Info:
                    break;  // the label is the whole row
                }

                if (!row.enabled) GuiSetState(STATE_NORMAL);
                y += row_h;
            }
            const int vis = selected_visible_index(m);
            if (vis >= 0)
                draw_accent_bar(surface, bar_x,
                                panel_y + pad + (header ? 24.0f * s : 0.0f) +
                                    static_cast<float>(vis) * row_h,
                                row_h);
        }

        // Status line.
        if (has_status) {
            (void)GuiStatusBar(
                Rectangle{panel_x + pad, y, panel_w - 2.0f * pad, row_h},
                m.status);
        }
    }

    // Toast: centered title + body, open or closed (labels over nothing —
    // the legacy toast has no chrome). Per-control alignment override.
    if (toast) {
        const float y = std::round(16.0f * s);
        const int saved_align = GuiGetStyle(LABEL, TEXT_ALIGNMENT);
        GuiSetStyle(LABEL, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
        if (m.toast_title[0])
            GuiLabel(Rectangle{0.0f, y, dw, std::round(18.0f * s)},
                     m.toast_title);
        if (m.toast_body[0])
            GuiLabel(Rectangle{0.0f, y + std::round(26.0f * s), dw,
                               std::round(14.0f * s)},
                     m.toast_body);
        GuiSetStyle(LABEL, TEXT_ALIGNMENT, saved_align);
    }

    // Restore everything the draw touched.
    for (int c = 0; c < kThemedControlCount; ++c)
        for (int p = 0; p < kThemedPropCount; ++p)
            GuiSetStyle(kThemedControls[c], kThemedProps[p],
                        saved_theme[c][p]);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, saved_bg);
    GuiSetStyle(DEFAULT, LINE_COLOR, saved_line);
    GuiSetStyle(DEFAULT, TEXT_SIZE, saved_text_size);
    GuiSetAlpha(1.0f);
    GuiUnlock();
}

}  // namespace gbarecomp

#endif  // GBARECOMP_USE_RAYLIB
