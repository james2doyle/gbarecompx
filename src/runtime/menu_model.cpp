// menu_model.cpp — the pure menu model (see menu_model.h for the contract).
#include "menu_model.h"

#include <cstddef>

namespace gbarecomp {
namespace {

int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float clamp01(float v) {
    if (!(v > 0.0f)) return 0.0f;  // NaN-safe floor
    if (v > 1.0f) return 1.0f;
    return v;
}

void copy_trunc(char* dst, std::size_t n, const char* src) {
    if (n == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    std::size_t i = 0;
    for (; i + 1 < n && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

bool selectable(const MenuRow& r) { return r.visible && r.enabled; }

bool section_has_selectable(const MenuModel& m, int section) {
    for (int i = 0; i < m.row_count; ++i)
        if (m.rows[i].section == section && selectable(m.rows[i])) return true;
    return false;
}

// Next selectable section from `from` (wrapping); -1 when none exists.
int step_section(const MenuModel& m, int from, int delta) {
    if (m.section_count <= 0) return -1;
    int s = from;
    for (int n = 0; n < m.section_count; ++n) {
        s = (s + delta + m.section_count) % m.section_count;
        if (section_has_selectable(m, s)) return s;
    }
    return -1;
}

// First selectable row of a section as a within-section index (0 when none).
int first_selectable_row(const MenuModel& m, int section) {
    const int count = menu_section_row_count(m, section);
    for (int i = 0; i < count; ++i) {
        const int gi = menu_section_row(m, section, i);
        if (gi >= 0 && selectable(m.rows[gi])) return i;
    }
    return 0;
}

// Next selectable row within a section (wrapping); -1 when none.
int step_row(const MenuModel& m, int section, int from, int delta) {
    const int count = menu_section_row_count(m, section);
    if (count <= 0) return -1;
    int i = from;
    for (int n = 0; n < count; ++n) {
        i = (i + delta + count) % count;
        const int gi = menu_section_row(m, section, i);
        if (gi >= 0 && selectable(m.rows[gi])) return i;
    }
    return -1;
}

// Adjust (or activate) the selected row. `direction` is +1/-1; `activate`
// is true for Confirm (fires Actions; for the value kinds it behaves like
// +1, exactly as recomp-ui's adjust_current does).
MenuResult menu_adjust(MenuModel& m, int direction, bool activate) {
    MenuResult r;
    const int gi = menu_selected_row(m);
    if (gi < 0) return r;
    MenuRow& row = m.rows[gi];
    if (!selectable(row)) return r;

    switch (row.kind) {
    case MenuRowKind::Info:
        return r;
    case MenuRowKind::Action:
        if (activate) r.action_row = gi;
        return r;
    case MenuRowKind::Toggle: {
        // Direction-independent flip (recomp-ui semantics).
        row.int_value = row.int_value ? 0 : 1;
        r.changed_row = gi;
        return r;
    }
    case MenuRowKind::Choice: {
        const int count = row.choice_count;
        if (count <= 0) return r;
        int base = row.int_value % count;
        if (base < 0) base += count;
        base = (base + (direction < 0 ? -1 : 1) + count) % count;
        if (base != row.int_value) {
            row.int_value = base;
            r.changed_row = gi;
        }
        return r;
    }
    case MenuRowKind::StepInt: {
        int step = static_cast<int>(row.step);
        if (step < 1) step = 1;
        const int lo = static_cast<int>(row.min_value);
        const int hi = static_cast<int>(row.max_value);
        const int next =
            clamp_int(row.int_value + (direction < 0 ? -step : step), lo, hi);
        if (next != row.int_value) {
            row.int_value = next;
            r.changed_row = gi;
        }
        return r;
    }
    case MenuRowKind::StepFloat: {
        const float step = row.step > 0.0f ? row.step : 1.0f;
        float next = row.float_value + (direction < 0 ? -step : step);
        if (next < row.min_value) next = row.min_value;
        if (next > row.max_value) next = row.max_value;
        if (next != row.float_value) {
            row.float_value = next;
            r.changed_row = gi;
        }
        return r;
    }
    }
    return r;
}

}  // namespace

MenuResult menu_open(MenuModel& m) {
    MenuResult r;
    if (m.open) return r;
    m.open = true;
    r.opened = true;
    m.editing = false;
    m.row_index = 0;
    if (m.section_count > 0) {
        m.section_index = clamp_int(m.section_index, 0, m.section_count - 1);
        if (!section_has_selectable(m, m.section_index)) {
            const int s = step_section(m, m.section_index, +1);
            if (s >= 0) m.section_index = s;
        }
    } else {
        m.section_index = 0;
    }
    return r;
}

MenuResult menu_close(MenuModel& m) {
    MenuResult r;
    if (!m.open) return r;
    m.open = false;
    m.editing = false;
    m.row_index = 0;
    r.closed = true;
    return r;
}

MenuResult menu_toggle(MenuModel& m) {
    return m.open ? menu_close(m) : menu_open(m);
}

MenuResult menu_navigate(MenuModel& m, MenuInput in) {
    MenuResult r;
    if (!m.open) return r;

    switch (in) {
    case MenuInput::Back:
        if (m.editing) {
            m.editing = false;
            m.row_index = 0;
        } else {
            m.open = false;
            r.closed = true;
        }
        return r;

    case MenuInput::Up:
    case MenuInput::Down: {
        const int delta = (in == MenuInput::Up) ? -1 : +1;
        if (m.editing) {
            const int next = step_row(m, m.section_index, m.row_index, delta);
            if (next >= 0) m.row_index = next;
        } else {
            const int next = step_section(m, m.section_index, delta);
            if (next >= 0) m.section_index = next;
        }
        return r;
    }

    case MenuInput::Confirm:
        if (!m.editing) {
            if (!section_has_selectable(m, m.section_index)) return r;
            m.editing = true;
            m.row_index = first_selectable_row(m, m.section_index);
            return r;
        }
        return menu_adjust(m, +1, /*activate=*/true);

    case MenuInput::Left:
    case MenuInput::Right:
        if (!m.editing) return r;
        return menu_adjust(m, in == MenuInput::Left ? -1 : +1,
                           /*activate=*/false);
    }
    return r;
}

MenuResult menu_pointer(MenuModel& m, const MenuPointer& p) {
    MenuResult r;
    if (!m.open || !p.pressed) return r;
    if (p.row < 0 || p.row >= m.row_count) return r;
    const MenuRow& row = m.rows[p.row];
    if (!selectable(row)) return r;

    // Pressing the already-selected row activates it; any other press
    // selects it (drilling into its section). One tap selects, a second tap
    // acts.
    if (m.editing && menu_selected_row(m) == p.row)
        return menu_adjust(m, +1, /*activate=*/true);

    if (m.section_count > 0)
        m.section_index = clamp_int(row.section, 0, m.section_count - 1);
    m.editing = true;
    const int count = menu_section_row_count(m, m.section_index);
    for (int i = 0; i < count; ++i) {
        if (menu_section_row(m, m.section_index, i) == p.row) {
            m.row_index = i;
            break;
        }
    }
    return r;
}

int menu_value_int(const MenuModel& m, int row) {
    if (row < 0 || row >= m.row_count) return 0;
    return m.rows[row].int_value;
}

float menu_value_float(const MenuModel& m, int row) {
    if (row < 0 || row >= m.row_count) return 0.0f;
    return m.rows[row].float_value;
}

void menu_set_value_int(MenuModel& m, int row, int value) {
    if (row < 0 || row >= m.row_count) return;
    m.rows[row].int_value = value;
}

void menu_set_value_float(MenuModel& m, int row, float value) {
    if (row < 0 || row >= m.row_count) return;
    m.rows[row].float_value = value;
}

void menu_set_status(MenuModel& m, const char* text) {
    const bool on = text && text[0];
    copy_trunc(m.status, sizeof(m.status), on ? text : nullptr);
    m.status_frames = on ? MenuModel::kStatusFrames : 0;
}

void menu_set_toast(MenuModel& m, const char* title, const char* body) {
    const bool on = (title && title[0]) || (body && body[0]);
    copy_trunc(m.toast_title, sizeof(m.toast_title), on ? title : nullptr);
    copy_trunc(m.toast_body, sizeof(m.toast_body), on ? body : nullptr);
    m.toast_frames = on ? MenuModel::kToastFrames : 0;
}

void menu_set_backdrop(MenuModel& m, float dim, float opacity) {
    m.dim = clamp01(dim);
    m.opacity = clamp01(opacity);
}

int menu_selected_row(const MenuModel& m) {
    if (!m.open || !m.editing) return -1;
    return menu_section_row(m, m.section_index, m.row_index);
}

int menu_section_row_count(const MenuModel& m, int section) {
    int count = 0;
    for (int i = 0; i < m.row_count; ++i)
        if (m.rows[i].section == section) ++count;
    return count;
}

int menu_section_row(const MenuModel& m, int section, int index) {
    if (index < 0) return -1;
    int seen = 0;
    for (int i = 0; i < m.row_count; ++i) {
        if (m.rows[i].section != section) continue;
        if (seen == index) return i;
        ++seen;
    }
    return -1;
}

int menu_section_first_selectable(const MenuModel& m, int section) {
    for (int i = 0; i < m.row_count; ++i) {
        if (m.rows[i].section != section) continue;
        if (selectable(m.rows[i])) return i;
    }
    return -1;
}

int menu_add_row(MenuModel& m, const MenuRow& row) {
    if (m.row_count < 0 || m.row_count >= MenuModel::kMaxRows) return -1;
    m.rows[m.row_count] = row;
    return m.row_count++;
}

bool menu_input_from_keyinput(std::uint16_t gba_bits, MenuInput* out) {
    // GBA KEYINPUT layout, active-low: bits 0=A 1=B 4=Right 5=Left 6=Up
    // 7=Down (a CLEAR bit means pressed).
    const bool a = (gba_bits & (1u << 0)) == 0;
    const bool b = (gba_bits & (1u << 1)) == 0;
    const bool right = (gba_bits & (1u << 4)) == 0;
    const bool left = (gba_bits & (1u << 5)) == 0;
    const bool up = (gba_bits & (1u << 6)) == 0;
    const bool down = (gba_bits & (1u << 7)) == 0;

    MenuInput in;
    if (b) in = MenuInput::Back;
    else if (a) in = MenuInput::Confirm;
    else if (up) in = MenuInput::Up;
    else if (down) in = MenuInput::Down;
    else if (left) in = MenuInput::Left;
    else if (right) in = MenuInput::Right;
    else return false;

    if (out) *out = in;
    return true;
}

}  // namespace gbarecomp
