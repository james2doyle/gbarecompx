// menu_render_contract_test.cpp — the real backend's menu seams, observed
// through the WindowBackend contract.
//
// Context (2026-10-16): the settings overlay stopped rendering on Delete —
// the menu OPEN path ran (model open, suppress_game_input flipped) while
// draw_pending_menu() read the Impl engine member its constructor never
// assigned, and the null guard turned a wiring bug into "Delete does
// nothing". Contract and engine unit tests both stayed green: the engine
// was tested over a recording overlay, the host loop over fakes; the glue
// between them had no executable coverage.
//
// What the CONTRACT can observe about the menu (and what this suite
// asserts, on the REAL backend, wherever a window can open — CI's xvfb
// job):
//   1. menu().available() with a live raygui engine.
//   2. Present/readback fidelity on the REAL backend — the contract test
//      only asserted byte-exact readback for the FakeBackend; the real
//      pipeline (upload → texture → last_upload) was never pinned.
//   3. The menu NEVER leaks into the guest upload (plan §3.4: the overlay
//      draws after the shader/scale pass): presenting the same guest frame
//      with the menu open vs closed yields byte-identical readback.
//
// What the contract CANNOT observe: the composited screen (readback is the
// guest-frame upload by design, so a silent no-draw is invisible here).
// The pixel-level render assertion needs a screen capture tier — the xvfb
// CI job screenshots the window while a menu-open present loop holds it up
// (logged as the follow-up; BRINGUP 2026-10-16).
//
// Display-gated exactly like backend_contract_test: on a display-less box
// open() fails with Err::Window and the suite prints + passes.

#include "menu_model.h"
#include "raylib_backend.h"
#include "window_backend.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace {

using gbarecomp::MenuModel;
using gbarecomp::MenuRow;
using gbarecomp::MenuRowKind;
using gbarecomp::OpenParams;
using gbarecomp::PointerState;
using gbarecomp::RaylibBackend;
using gbarecomp::Result;

// A mid-gray guest frame, clearly distinct from the menu palette (panel
// 18/20/30, dim black) so a leak into the upload would move many bytes.
std::vector<std::uint8_t> guest_frame() {
    std::vector<std::uint8_t> frame(240 * 160 * 3);
    for (std::size_t i = 0; i < frame.size(); ++i)
        frame[i] = static_cast<std::uint8_t>(0x60u + (i * 7u) % 32u);
    return frame;
}

std::vector<std::uint8_t> present_and_readback(
    RaylibBackend& b, const std::vector<std::uint8_t>& f, int* w, int* h) {
    b.video().present(f.data(), true);
    std::vector<std::uint8_t> back;
    CHECK(b.video().readback(back, *w, *h));
    return back;
}

std::size_t differing_bytes(const std::vector<std::uint8_t>& a,
                            const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) return a.size() + b.size();
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) ++n;
    return n;
}

}  // namespace

int main() {
    RaylibBackend b;
    const Result r = b.open(OpenParams{});
    if (!r.ok()) {
        std::printf("menu_render_contract: open unavailable (%s) — the "
                    "real-backend assertions need a display (CI: xvfb)\n",
                    r.detail ? r.detail : "?");
        return 0;
    }
    CHECK(b.is_open());

    // 1. The menu presentation is available with a live engine. A stub
    //    build (no raylib) opens fine but the presentation is state-only —
    //    every seam below is raylib-only, so there is nothing to assert.
    if (!b.menu().available()) {
        std::printf("menu_render_contract: stub backend (no raylib) — "
                    "presentation is state-only, render seams skipped\n");
        b.close();
        return 0;
    }
    CHECK(b.menu().available());

    // 2. Readback fidelity on the REAL backend: the bytes presented are the
    //    bytes read back (the contract test pinned this for the fake only).
    const std::vector<std::uint8_t> frame = guest_frame();
    int w = 0, h = 0;
    const std::vector<std::uint8_t> baseline =
        present_and_readback(b, frame, &w, &h);
    CHECK(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3u ==
          baseline.size());
    CHECK(baseline == frame);

    // 3. The menu must not leak into the guest upload (plan §3.4): open
    //    menu + present + readback == baseline, and closed restores too.
    MenuModel m;
    CHECK(menu_open(m).opened);
    menu_set_backdrop(m, 0.5f, 1.0f);
    m.sections[0].title = "Render";
    m.section_count = 1;
    MenuRow toggle;
    toggle.kind = MenuRowKind::Toggle;
    toggle.label = "Toggle";
    toggle.section = 0;
    CHECK(menu_add_row(m, toggle) >= 0);

    const PointerState pointer;
    (void)b.menu().present(m, pointer);  // stash the open model
    const std::vector<std::uint8_t> open_frame =
        present_and_readback(b, frame, &w, &h);
    CHECK(differing_bytes(baseline, open_frame) == 0);

    CHECK(menu_close(m).closed);
    (void)b.menu().present(m, pointer);
    const std::vector<std::uint8_t> closed_frame =
        present_and_readback(b, frame, &w, &h);
    CHECK(differing_bytes(baseline, closed_frame) == 0);

    b.close();
    std::printf("menu_render_contract: PASS (available, fidelity, no "
                "upload leak across open/close)\n");
    return 0;
}
