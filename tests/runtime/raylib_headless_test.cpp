// raylib_headless_test.cpp — Phase 5.2, §5.2 items 5-6 (red-first).
//
// Headless tier smoke: open -> present synthetic frame -> readback
// byte-equal -> lifecycle. Runs the real RaylibBackend.
//
//   * Stub config (no raylib): PASS, nothing to drive.
//   * GL raylib config: asserts the §3.3 Headless contract — open(Headless)
//     refuses Err::Unsupported without the memory platform — then skips.
//   * Memory platform (rlsw): the full smoke. No DISPLAY needed.
//
// RED STATE: needs GBARECOMP_RAYLIB_HEADLESS plumbing (PLATFORM=Memory for
// the fetched raylib) plus the PLATFORM_MEMORY gamepad guard in the backend.
// A headless-configured build must fail (undefined GLFW symbols) until the
// guard lands.
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

#if !defined(GBARECOMP_USE_RAYLIB)

int main() {
    std::printf("raylib_headless tests: PASS (stub build, no backend)\n");
    return 0;
}

#else

#include "raylib_backend.h"
#include "window_backend.h"

using gbarecomp::Caps;
using gbarecomp::Err;
using gbarecomp::InputSnapshot;
using gbarecomp::OpenParams;
using gbarecomp::RaylibBackend;

namespace {

constexpr int kBaseW = 240;
constexpr int kBaseH = 160;

void fill_synthetic(std::vector<std::uint8_t>& px) {
    px.resize(static_cast<std::size_t>(kBaseW) * kBaseH * 3u);
    for (int y = 0; y < kBaseH; ++y) {
        for (int x = 0; x < kBaseW; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kBaseW + x) * 3u;
            px[i] = static_cast<std::uint8_t>(x & 0xFF);
            px[i + 1] = static_cast<std::uint8_t>(y & 0xFF);
            px[i + 2] = static_cast<std::uint8_t>((x + y) & 0xFF);
        }
    }
}

#if !defined(PLATFORM_MEMORY)

void headless_contract_without_memory_platform() {
    RaylibBackend b;
    OpenParams p;
    p.mode = OpenParams::Mode::Headless;
    const auto r = b.open(p);
    CHECK(!r.ok() && r.code == Err::Unsupported && r.detail != nullptr);
    CHECK(!b.is_open());
}

#else

void headless_smoke() {
    RaylibBackend b;

    // Capabilities of the memory platform: headless present/readback, no
    // shaders (rlsw compiles the shader entry points out), no input
    // system, but the menu presentation exists.
    {
        const Caps c = b.caps();
        CHECK(c.headless);
        CHECK(!c.shaders);
        CHECK(c.menu);
        CHECK(!c.input);
    }

    CHECK(b.open(OpenParams{}).ok());

    // Synthetic frame round-trips byte-identical (fidelity rule: GPU-side
    // effects are invisible to readback; under rlsw there are none).
    std::vector<std::uint8_t> px;
    fill_synthetic(px);
    b.video().present(px.data(), true);
    {
        std::vector<std::uint8_t> out;
        int w = 0, h = 0;
        CHECK(b.video().readback(out, w, h));
        CHECK(w == kBaseW && h == kBaseH);
        CHECK(out == px);
    }

    // Fx values store even though nothing renders them (capability rule).
    {
        auto s = b.fx().settings();
        s.lcd = true;
        s.lcd_boost = 1.25f;
        b.fx().apply(s);
        CHECK(b.fx().settings().lcd);
        CHECK(b.fx().settings().lcd_boost == 1.25f);
    }
    // Readback is unaffected by the stored-but-unrendered FX.
    {
        std::vector<std::uint8_t> out;
        int w = 0, h = 0;
        CHECK(b.video().readback(out, w, h));
        CHECK(out == px);
    }

    // Pump without an input system: safe, quiet levels.
    {
        const auto ev = b.pump();
        CHECK(ev.keyinput == 0x03FF);
        CHECK(!ev.quit);
    }

    // Audio tolerates a missing device (runner without a sound server).
    {
        const auto r = b.audio().claim();
        CHECK(r.ok() || r.code == Err::AudioDevice);
        if (r.ok()) b.audio().release();
    }

    // Lifecycle: close is idempotent, reopen works, Headless mode opens.
    b.close();
    CHECK(!b.is_open());
    b.close();
    CHECK(b.open(OpenParams{}).ok());
    b.close();
    OpenParams headless;
    headless.mode = OpenParams::Mode::Headless;
    CHECK(b.open(headless).ok());
    b.close();
    CHECK(!b.is_open());
}

#endif  // !defined(PLATFORM_MEMORY)

}  // namespace

int main() {
#if !defined(PLATFORM_MEMORY)
#if defined(GBARECOMP_USE_RAYLIB)
    headless_contract_without_memory_platform();
    std::printf("raylib_headless tests: PASS (GL config, Headless refused)\n");
#endif
#else
    headless_smoke();
    std::printf("raylib_headless tests: PASS (tier=rlsw headless)\n");
#endif
    return 0;
}

#endif  // defined(GBARECOMP_USE_RAYLIB)
