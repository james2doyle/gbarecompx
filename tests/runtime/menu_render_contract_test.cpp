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
#include <string>
#include <vector>

#if defined(GBARECOMP_MENU_CAPTURE_X11)
#include <X11/Xlib.h>
#endif

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

// ── The composited-screen tier (the 2026-10-16 follow-up) ────────────────
// Readback is the guest upload BY DESIGN, so a silent no-draw is invisible
// to it — that blind spot is exactly where two device-visible regressions
// lived (the unwired engine seam: menu open, input suppressed, nothing
// painted; the dead raygui font: shapes painted, text didn't). This tier
// reads the X framebuffer DIRECTLY (X11 is already linked via raylib; no
// external capture tool) and asserts the menu is VISIBLE: panel pixels and
// TEXT pixels both. Skips (pass, with a note) where X11 is unavailable.
struct Capture {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgb;  // RGB triples
};

#if defined(GBARECOMP_MENU_CAPTURE_X11)
// Depth-first search for a mapped top-level window whose WM_NAME matches.
// The root window itself is NOT captured: compositors (KWin et al.) give
// X_GetImage on root a BadMatch (root depth differs from the framebuffers
// beneath it) — the app's own window reads reliably under xvfb and on
// desktop alike.
bool find_window_by_name(Display* dpy, Window w, const char* name,
                         Window* out) {
    char* title = nullptr;
    int status = XFetchName(dpy, w, &title);
    if (status && title) {
        const bool match = std::string(title) == name;
        XFree(title);
        if (match) {
            *out = w;
            return true;
        }
    }
    Window root_ret = 0, parent = 0;
    Window* children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(dpy, w, &root_ret, &parent, &children, &n)) return false;
    for (unsigned int i = 0; i < n; ++i) {
        if (find_window_by_name(dpy, children[i], name, out)) {
            XFree(children);
            return true;
        }
    }
    if (children) XFree(children);
    return false;
}

bool capture_window(const char* title, Capture* out) {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return false;
    const int scr = DefaultScreen(dpy);
    Window win = 0;
    if (!find_window_by_name(dpy, RootWindow(dpy, scr), title, &win)) {
        XCloseDisplay(dpy);
        return false;
    }
    XWindowAttributes a{};
    if (!XGetWindowAttributes(dpy, win, &a) || a.width <= 0 ||
        a.height <= 0) {
        XCloseDisplay(dpy);
        return false;
    }
    XImage* img =
        XGetImage(dpy, win, 0, 0, a.width, a.height, AllPlanes, ZPixmap);
    if (!img) {
        XCloseDisplay(dpy);
        return false;
    }
    out->w = a.width;
    out->h = a.height;
    out->rgb.assign(static_cast<std::size_t>(a.width) *
                        static_cast<std::size_t>(a.height) * 3u,
                    0);
    auto shift_of = [](unsigned long mask) {
        int s = 0;
        while (mask && !(mask & 1u)) {
            ++s;
            mask >>= 1;
        }
        return s;
    };
    const int rsh = shift_of(img->red_mask);
    const int gsh = shift_of(img->green_mask);
    const int bsh = shift_of(img->blue_mask);
    std::size_t o = 0;
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            const unsigned long px = img->f.get_pixel(img, x, y);
            out->rgb[o + 0] = static_cast<std::uint8_t>((px >> rsh) & 0xFFu);
            out->rgb[o + 1] = static_cast<std::uint8_t>((px >> gsh) & 0xFFu);
            out->rgb[o + 2] = static_cast<std::uint8_t>((px >> bsh) & 0xFFu);
            o += 3;
        }
    }
    img->f.destroy_image(img);
    XCloseDisplay(dpy);
    return true;
}
#else
bool capture_window(const char*, Capture*) { return false; }
#endif

int channel_near(const Capture& c, std::size_t idx, unsigned target,
                 unsigned tol) {
    const int d = static_cast<int>(c.rgb[idx]) - static_cast<int>(target);
    return d < 0 ? -d : d;
}

// Pixels within tol of (r,g,b) anywhere in the capture.
long count_near(const Capture& c, unsigned r, unsigned g, unsigned b,
                unsigned tol) {
    long n = 0;
    for (std::size_t i = 0; i + 2 < c.rgb.size(); i += 3)
        if (channel_near(c, i, r, tol) <= static_cast<int>(tol) &&
            channel_near(c, i + 1, g, tol) <= static_cast<int>(tol) &&
            channel_near(c, i + 2, b, tol) <= static_cast<int>(tol))
            ++n;
    return n;
}

}  // namespace

// Unique WM_NAME so the capture tier finds THIS window (X11 tier reads the
// app's own window — root captures BadMatch under compositors).
constexpr char kWindowTitle[] = "gbarecomp_menu_render_contract";

int main() {
    RaylibBackend b;
    OpenParams params{};
    params.title = kWindowTitle;
    const Result r = b.open(params);
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

    // 4. The composited-screen tier (the 2026-10-16 follow-up): readback is
    //    the guest upload, so a silent no-draw is invisible to it — the
    //    exact blind spot where two device-visible regressions lived (the
    //    unwired engine seam: open + suppressed + nothing painted; the dead
    //    raygui font: shapes painted, text didn't). Capture the X
    //    framebuffer and assert the open menu is VISIBLE — panel pixels AND
    //    text pixels — then gone when closed. Skips where `import`
    //    (ImageMagick) is unavailable, like the display gate above.
    CHECK(menu_open(m).opened);
    (void)b.menu().present(m, pointer);
    (void)present_and_readback(b, frame, &w, &h);
    Capture cap;
    if (capture_window(kWindowTitle, &cap)) {
        const long panel = count_near(cap, 18, 20, 30, 6);
        const long text = count_near(cap, 235, 235, 240, 12);
        CHECK(panel > 300);  // the GuiPanel backdrop is on the composite
        CHECK(text > 30);    // label glyphs too — no shapes-without-text
        CHECK(menu_close(m).closed);
        (void)b.menu().present(m, pointer);
        (void)present_and_readback(b, frame, &w, &h);
        Capture after;
        CHECK(capture_window(kWindowTitle, &after));
        CHECK(count_near(after, 18, 20, 30, 6) < 100);  // no leak on close
    } else {
        std::printf("menu_render_contract: capture tier skipped (X11 window "
                    "capture unavailable)\n");
    }

    b.close();
    std::printf("menu_render_contract: PASS (available, fidelity, no "
                "upload leak across open/close)\n");
    return 0;
}
