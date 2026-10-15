// menu_model.h — the generic in-game menu model (plan §3.4): pure data +
// pure mutators. No backend, no toolkit, no host pointers, no clock.
//
// The host owns the model: it builds sections/rows and seeds values before
// opening, hands the model to the presentation each frame (Menu::present),
// and applies the MenuResult of every navigation/pointer call (Fx/Audio/
// Video setters, config persistence). The presentation only draws and
// hit-tests.
//
// Navigation policy — drill-in, wrap, skip disabled/invisible, clamping,
// choice cycling — lives HERE, never in a backend or presentation. The
// semantics port recomp-ui's runtime_ui model (its test is the reference):
// a two-level list (sections, then rows), Confirm enters a section, Left/
// Right/Confirm adjust the selected row, Back leaves then closes. The V2
// additions: disabled/invisible rows and empty sections are skipped, and
// the pointer selects then activates.
#pragma once

#include <cstdint>

namespace gbarecomp {

enum class MenuRowKind {
    Toggle,     // 0/1 in int_value
    StepInt,    // int_value, stepped by (int)step, clamped to [min, max]
    StepFloat,  // float_value, stepped by step, clamped to [min, max]
    Choice,     // int_value = index into choices[], cycles with wrap
    Action,     // fires only on Confirm (MenuResult::action_row)
    Info,       // display only
};

enum class MenuInput { Up, Down, Left, Right, Confirm, Back };

struct MenuRow {
    MenuRowKind kind = MenuRowKind::Info;
    const char* label = nullptr;
    bool enabled = true;
    bool visible = true;

    // Step* descriptors (StepInt uses the integer part).
    float min_value = 0.0f;
    float max_value = 0.0f;
    float step = 1.0f;

    // Choice descriptors (host-owned strings; no copying).
    const char* const* choices = nullptr;
    int choice_count = 0;

    // Kind-dependent value storage. The host seeds these at open time and
    // reads them back after MenuResult::changed_row.
    int int_value = 0;
    float float_value = 0.0f;

    int section = 0;  // index into MenuModel::sections
};

struct MenuSection {
    const char* title = nullptr;
};

struct MenuModel {
    // Fixed capacities (no heap in the model).
    static constexpr int kMaxSections = 8;
    static constexpr int kMaxRows = 64;
    static constexpr int kMaxStatus = 48;
    static constexpr int kMaxToastTitle = 48;
    static constexpr int kMaxToastBody = 96;
    // Frame counts the host decrements (the model keeps no clock of its own).
    static constexpr int kStatusFrames = 90;
    static constexpr int kToastFrames = 180;

    // Structure (host-filled once, then stable).
    MenuSection sections[kMaxSections];
    int section_count = 0;
    MenuRow rows[kMaxRows];
    int row_count = 0;

    // Navigation state.
    int section_index = 0;  // selected section (always meaningful)
    int row_index = 0;      // within-section storage index (when editing)
    bool open = false;
    bool editing = false;   // drilled into section_index (recomp-ui's
                            // in_section; the section list is the top level)

    // Status / toast / backdrop (the presentation reads these; the host
    // times them by decrementing the frame counts).
    char status[kMaxStatus] = {};
    int status_frames = 0;
    char toast_title[kMaxToastTitle] = {};
    char toast_body[kMaxToastBody] = {};
    int toast_frames = 0;
    float dim = 0.0f;
    float opacity = 1.0f;
};

struct MenuResult {
    int changed_row = -1;  // global row whose value changed
    int action_row = -1;   // global row whose Action fired
    bool opened = false;
    bool closed = false;
};

// The presentation's raw pointer/touch sample (hit-tested against the
// drawn rows). `touch` marks touch-origin samples; the policy is the same.
struct PointerState {
    float x = 0.0f;
    float y = 0.0f;
    bool down = false;
    bool pressed = false;
    bool released = false;
    bool touch = false;
};

// A hit-tested pointer event: the presentation resolves which row (global
// index) the pointer is over and forwards the press through the model's
// mutator — all mutation goes through the shared mutators.
struct MenuPointer {
    int row = -1;
    bool pressed = false;
};

// ── mutators (pure) ─────────────────────────────────────────────────────

MenuResult menu_open(MenuModel& m);
MenuResult menu_close(MenuModel& m);
MenuResult menu_toggle(MenuModel& m);
MenuResult menu_navigate(MenuModel& m, MenuInput in);
MenuResult menu_pointer(MenuModel& m, const MenuPointer& p);

// ── values ──────────────────────────────────────────────────────────────
// Out-of-range rows are safe no-ops (getters return 0).
int menu_value_int(const MenuModel& m, int row);
float menu_value_float(const MenuModel& m, int row);
void menu_set_value_int(MenuModel& m, int row, int value);
void menu_set_value_float(MenuModel& m, int row, float value);

// Append a row; returns its global index, or -1 when the model is full.
// The safe idiom for hosts/games appending rows — never write row_count
// directly (the capacity is a hard bound and the row walkers trust it).
int menu_add_row(MenuModel& m, const MenuRow& row);

// ── status / toast / backdrop ───────────────────────────────────────────
// Empty/null text clears and zeroes the frame count.
void menu_set_status(MenuModel& m, const char* text);
void menu_set_toast(MenuModel& m, const char* title, const char* body);
void menu_set_backdrop(MenuModel& m, float dim, float opacity);  // clamped 0..1

// ── selection helpers (presentation/host) ───────────────────────────────
// Global row index of the selection, or -1 at the section list.
int menu_selected_row(const MenuModel& m);
// Section row list in storage order (all rows, visible or not).
int menu_section_row_count(const MenuModel& m, int section);
int menu_section_row(const MenuModel& m, int section, int index);  // -1 if none
// First selectable (visible+enabled) row of a section as a global index, or
// -1. The pointer hit-test maps a section-line press through this.
int menu_section_first_selectable(const MenuModel& m, int section);

// ── input mapping ───────────────────────────────────────────────────────
// GBA KEYINPUT (active-low: a CLEAR bit means pressed) -> the nav input.
// Returns false when no nav key is pressed (out untouched). Priority when
// several are held: Back, Confirm, Up, Down, Left, Right.
bool menu_input_from_keyinput(std::uint16_t gba_bits, MenuInput* out);

}  // namespace gbarecomp
