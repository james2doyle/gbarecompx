// backend_contract_test.cpp — §5.2 item 2, display-free slice.
//
// Drives the V2 seam through its §3.3 contracts against:
//   (a) the FakeBackend (always; the full contract incl. scripted input,
//       readback fidelity, audio claim, fx/overlay), and
//   (b) the real RaylibBackend (the subset that holds without a display —
//       a display-less GLFW box fails open() with Err::Window, which the
//       suite accepts).
//
// The full contract (present/readback pixels, pump timing) runs on the
// headless tier in Phase 5; this slice is what can hold today in both
// configs.
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG; see BRINGUP
// "vacuous test" finding.
#include "raylib_backend.h"
#include "raylib_keymap.h"
#include "support/fake_backend.h"
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

using gbarecomp::Err;
using gbarecomp::Events;
using gbarecomp::FxSettings;
using gbarecomp::InputSnapshot;
using gbarecomp::kRayKeyX;
using gbarecomp::OpenParams;
using gbarecomp::RaylibBackend;
using gbarecomp::Result;
using gbarecomp::WindowBackendV2;
using gbarecomp::testing::FakeBackend;

// `fake` is non-null only for the FakeBackend run: it unlocks the scripted
// checks (input scripting, readback fidelity, audio claim, fx/overlay).
void run_contract(WindowBackendV2& b, const char* name,
                  bool must_open, FakeBackend* fake) {
    // Fresh: closed; close() idempotent; lifecycle calls safe.
    CHECK(!b.is_open());
    b.close();
    b.close();
    CHECK(!b.is_open());
    CHECK(!b.wait_for_foreground());  // closed -> false
    b.suspend();
    b.resume();

    // Headless requires Caps::headless; refused with a static-lifetime
    // detail (§3.3).
    if (!b.caps().headless) {
        OpenParams headless;
        headless.mode = OpenParams::Mode::Headless;
        const Result hr = b.open(headless);
        CHECK(!hr.ok() && hr.code == Err::Unsupported && hr.detail != nullptr);
        CHECK(!b.is_open());
    }

    // Accessors are stable objects.
    CHECK(&b.video() == &b.video());
    CHECK(&b.audio() == &b.audio());
    CHECK(&b.input() == &b.input());
    CHECK(&b.fx() == &b.fx());
    CHECK(&b.overlay() == &b.overlay());

    // Open: strict for the fake; the real backend may fail with Err::Window
    // on a display-less box (both are legal for it).
    const Result r = b.open(OpenParams{});
    if (must_open) {
        CHECK(r.ok());
    } else {
        CHECK(r.ok() || r.code == Err::Window);
    }
    if (!r.ok()) {
        std::printf("backend_contract [%s]: open unavailable (%s) — "
                    "generic checks only\n",
                    name, r.detail ? r.detail : "?");
        return;
    }

    CHECK(b.is_open());
    const Result again = b.open(OpenParams{});
    CHECK(!again.ok() && again.code == Err::AlreadyOpen &&
          again.detail != nullptr);

    if (fake) {
        // Scripted input -> pump -> GBA keyinput (active-low: pressed = bit
        // clear). The clock stamps now_ms (mirrors the real backend).
        fake->scripted_input.binds.gba_key[0] = kRayKeyX;  // index 0 = A
        fake->scripted_input.next = InputSnapshot{};
        fake->scripted_input.next.hold(kRayKeyX);
        fake->clock.now_s = 1.5;
        Events ev = b.pump();
        CHECK((ev.keyinput & 0x0001u) == 0u);  // A pressed
        CHECK(fake->scripted_input.last_snapshot.now_ms == 1500u);
        fake->scripted_input.next = InputSnapshot{};
        ev = b.pump();
        CHECK(ev.keyinput == 0x03FFu);  // all released

        // Fidelity (§3.3): readback returns the exact bytes presented.
        std::vector<std::uint8_t> frame(240 * 160 * 3);
        for (std::size_t i = 0; i < frame.size(); ++i)
            frame[i] = static_cast<std::uint8_t>(i * 7u);
        b.video().present(frame.data(), true);
        std::vector<std::uint8_t> back;
        int w = 0, h = 0;
        CHECK(b.video().readback(back, w, h));
        CHECK(w == 240 && h == 160 && back == frame);

        // Audio: single-owner claim; a second claimant is refused; release
        // re-arms; an unavailable device refuses too (§3.3).
        CHECK(b.audio().claim().ok());
        const Result second = b.audio().claim();
        CHECK(!second.ok() && second.code == Err::AudioDevice &&
              second.detail != nullptr);
        b.audio().release();
        CHECK(b.audio().claim().ok());
        fake->tapped_audio.release();
        fake->tapped_audio.refuse_claim = true;
        CHECK(!b.audio().claim().ok());
        fake->tapped_audio.refuse_claim = false;

        // Fx settings round-trip; overlay pad visibility.
        FxSettings want;
        want.lcd = true;
        want.lcd_boost = 1.5f;
        b.fx().apply(want);
        CHECK(b.fx().settings().lcd && b.fx().settings().lcd_boost == 1.5f);
        b.overlay().set_touch_pad_visible(true);
        CHECK(b.overlay().touch_pad_visible());
    }

    // Close -> reopen, gated by Caps::reopen (§3.3).
    b.close();
    CHECK(!b.is_open());
    if (b.caps().reopen) {
        CHECK(b.open(OpenParams{}).ok());
        b.close();
        CHECK(!b.is_open());
    }
}

}  // namespace

int main() {
    FakeBackend fake;
    run_contract(fake, "fake", /*must_open=*/true, &fake);

    // The same suite against the real backend: display-free boxes take the
    // Err::Window early-out; a display runs the generic lifecycle checks.
    RaylibBackend real;
    run_contract(real, "raylib", /*must_open=*/false, nullptr);

    std::printf("backend_contract tests: PASS\n");
    return 0;
}
