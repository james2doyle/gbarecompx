// launcher_raygui_test.cpp — the launcher's DRAW LOOP, driven headless.
//
// The launcher TU (launcher_raygui.cpp) runs for real against the
// recording raylib stub (launcher_raylib_stub.cpp + stub_raylib/raylib.h):
// run() is steppable frame by frame (scripted input, recorded draws), so
// what the loop DRAWS and how it reacts to input are assertable without a
// display — the tier that was compile-checked only, and the exact gap the
// menu engine's contract suite closed for menus.
//
// Scenarios: the first frame's full draw (title, status, buttons, panel,
// checkbox, footnote), the skip path opening NO window, Escape quitting,
// the focus ring moving Tab→SELECT ROM, mouse click on PLAY launching,
// Enter on focused PLAY launching.
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG; see BRINGUP
// "vacuous test" finding.
#include "launcher.h"
#include "launcher_raylib_stub.h"
#include "launcher_skip.h"
#include "launcher_style.h"

#include "raylib.h"  // the stub_raylib/ fake — KEY_*/MOUSE_* constants

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace fs = std::filesystem;

using gbarecomp::launcher_stub::script;
using gbarecomp::launcher_stub::texts;
using gbarecomp::launcher_stub::text_runs;
using gbarecomp::launcher_stub::rects;
using gbarecomp::launcher_stub::rings;
using gbarecomp::launcher_stub::clears;
using gbarecomp::launcher_stub::window_opens;
using gbarecomp::launcher_stub::window_closes;
using gbarecomp::launcher_stub::reset_all;

namespace {

bool has_text(int frame, const std::string& needle) {
    for (const auto& t : text_runs(frame))
        if (t == needle) return true;
    return false;
}

std::uint32_t rgb(std::uint32_t v) { return v; }

// A fixture cache root: argv0 points INTO it, so the sidecar state files
// (rom.cfg, skip_launcher_on_boot) live here.
struct Fixture {
    std::string dir;
    std::string rom;  // a real dummy ROM file the permissive spec accepts

    explicit Fixture(const std::string& name) {
        dir = "/tmp/opencode/launcher_fixture_" + name;
        fs::create_directories(dir);
        rom = dir + "/cartridge.gba";
        std::ofstream r(rom, std::ios::binary);
        r << "GBA DUMMY ROM BYTES";
    }
    void write(const std::string& file, const std::string& content) {
        std::ofstream f(dir + "/" + file);
        f << content;
    }
    const char* argv0() const {
        static std::string a;
        a = dir + "/ffta_recomp";
        return a.c_str();
    }
};

gbarecomp::LauncherConfig make_cfg(const Fixture& fx) {
    gbarecomp::LauncherConfig cfg{};
    cfg.argv0 = fx.argv0();
    cfg.staged_bios = "/bios/emibios_gba_bios.bin";
    cfg.boxart_path = "";  // no art → the placeholder panel
    cfg.first_boot_pick = false;
    return cfg;
}

}  // namespace

int main() {
    using namespace gbarecomp::launcher_style;

    // ── Scenario A: the skip path opens NO window ────────────────────────
    {
        Fixture fx("skip");
        fx.write("rom.cfg", fx.rom);
        fx.write(gbarecomp::launcher_skip::kStateFilename, "1");
        reset_all();
        auto backend = gbarecomp::create_launcher_backend();
        const gbarecomp::LauncherResult r = backend->run(make_cfg(fx));
        CHECK(r.kind == gbarecomp::LauncherResult::Kind::Launch);
        CHECK(r.rom == fx.rom);
        CHECK(std::string(r.bios) == "/bios/emibios_gba_bios.bin");
        // The whole point of the skip: no window, no frame, no draws.
        CHECK(window_opens() == 0);
        CHECK(window_closes() == 0);
        CHECK(clears().empty());
        CHECK(texts().empty());
    }

    // ── Scenario B: first frame draws the whole screen (not ready) ───────
    {
        Fixture fx("draw");
        fx.write(gbarecomp::launcher_skip::kStateFilename, "0");  // show screen
        reset_all();
        script().should_close = {false, true};  // end after frame 0
        auto backend = gbarecomp::create_launcher_backend();
        const gbarecomp::LauncherResult r = backend->run(make_cfg(fx));
        CHECK(r.kind == gbarecomp::LauncherResult::Kind::Quit);
        CHECK(window_opens() == 1);
        CHECK(window_closes() == 1);
        // The page background and the divider.
        CHECK(!clears().empty() && clears()[0].color == rgb(kBackground));
        // Title row, status prompt, button labels, footnote — all present
        // on frame 0.
        CHECK(has_text(0, kGameTitle));
        CHECK(has_text(0, "Choose your clean cartridge dump (.gba)."));
        CHECK(has_text(0, kPlayLabel));
        CHECK(has_text(0, kSelectLabel));
        CHECK(has_text(0, std::string(gbarecomp::launcher_skip::kSkipLabel)));
        // The footnote wraps at the column (line boundaries are the wrap's
        // business, not the assertion's).
        {
            bool foot1 = false, foot2 = false;
            for (const auto& s : text_runs(0)) {
                if (s.find("Your files stay on this machine") !=
                    std::string::npos)
                    foot1 = true;
                if (s.find("clean-room BIOS") != std::string::npos)
                    foot2 = true;
            }
            CHECK(foot1 && foot2);
        }
        // The status prompt is drawn in the amber; the title in white.
        bool amber_prompt = false, white_title = false;
        for (const auto& t : texts()) {
            if (t.frame != 0) continue;
            if (t.color == rgb(kAmber)) amber_prompt = true;
            if (t.color == rgb(kTitleWhite)) white_title = true;
        }
        CHECK(amber_prompt && white_title);
        // Two rounded button fills; PLAY is DIMMED while not ready, SELECT
        // ROM is full accent.
        int play_fills = 0, select_fills = 0, dimmed = 0, accent = 0;
        for (const auto& rc : rects()) {
            if (rc.frame != 0) continue;
            if (rc.w == 220.0f && rc.h == 62.0f) {
                ++play_fills;
                if (rc.color == rgb(kAccentDisabled)) ++dimmed;
            }
            if (rc.w == 220.0f && rc.h == 48.0f) {
                ++select_fills;
                if (rc.color == rgb(kAccent)) ++accent;
            }
        }
        CHECK(play_fills == 1 && select_fills == 1);
        CHECK(dimmed == 1 && accent == 1);
        // Focus starts on PLAY: its ring is visible on frame 0.
        bool play_ring = false;
        for (const auto& rg : rings()) {
            if (rg.frame == 0 && rg.w == 220.0f && rg.h == 62.0f &&
                rg.color == rgb(kTitlePlaceholder))
                play_ring = true;
        }
        CHECK(play_ring);
        // The placeholder panel (rounded card) is drawn.
        bool panel = false;
        for (const auto& rc : rects()) {
            if (rc.frame == 0 && rc.w > 300.0f && rc.h > 300.0f &&
                rc.color == rgb(kPanel))
                panel = true;
        }
        CHECK(panel);
    }

    // ── Scenario C: Escape quits ─────────────────────────────────────────
    {
        Fixture fx("esc");
        fx.write(gbarecomp::launcher_skip::kStateFilename, "0");
        reset_all();
        script().should_close = {false, true};
        script().keys_pressed = {{KEY_ESCAPE}, {}};
        auto backend = gbarecomp::create_launcher_backend();
        const gbarecomp::LauncherResult r = backend->run(make_cfg(fx));
        CHECK(r.kind == gbarecomp::LauncherResult::Kind::Quit);
        CHECK(window_closes() == 1);
    }

    // ── Scenario D: Tab moves the focus ring PLAY → SELECT ROM ───────────
    {
        Fixture fx("focus");
        fx.write(gbarecomp::launcher_skip::kStateFilename, "0");
        reset_all();
        script().should_close = {false, false, true};
        // Input polls at the top of a pass: frame 0 draws with PLAY
        // focused, the Tab lands before frame 1's draw.
        script().keys_pressed = {{}, {KEY_TAB}, {}};
        auto backend = gbarecomp::create_launcher_backend();
        backend->run(make_cfg(fx));
        auto ring_at = [&](int frame, float w, float h) {
            for (const auto& rg : rings())
                if (rg.frame == frame && rg.w == w && rg.h == h)
                    return true;
            return false;
        };
        CHECK(ring_at(0, 220.0f, 62.0f));   // frame 0: PLAY focused
        CHECK(!ring_at(0, 220.0f, 48.0f));
        CHECK(ring_at(1, 220.0f, 48.0f));   // frame 1: SELECT ROM focused
        CHECK(!ring_at(1, 220.0f, 62.0f));
    }

    // ── Scenario E: a click on PLAY (ready) launches ─────────────────────
    {
        Fixture fx("click");
        fx.write("rom.cfg", fx.rom);  // ready
        fx.write(gbarecomp::launcher_skip::kStateFilename, "0");
        reset_all();
        script().should_close = {false, true};
        script().mouse_position = {{490.0f, 181.0f}, {}};  // PLAY center
        script().mouse_buttons_pressed = {{MOUSE_LEFT_BUTTON}, {}};
        auto backend = gbarecomp::create_launcher_backend();
        auto cfg = make_cfg(fx);
        cfg.first_boot_pick = true;  // the game side always sets this on a
                                     // shown screen — the cache auto-picks
        const gbarecomp::LauncherResult r = backend->run(cfg);
        CHECK(r.kind == gbarecomp::LauncherResult::Kind::Launch);
        CHECK(r.rom == fx.rom);
    }

    // ── Scenario F: Enter on focused PLAY (ready) launches ───────────────
    {
        Fixture fx("enter");
        fx.write("rom.cfg", fx.rom);
        fx.write(gbarecomp::launcher_skip::kStateFilename, "0");
        reset_all();
        script().should_close = {false, true};
        script().keys_pressed = {{KEY_ENTER}, {}};
        auto backend = gbarecomp::create_launcher_backend();
        auto cfg = make_cfg(fx);
        cfg.first_boot_pick = true;
        const gbarecomp::LauncherResult r = backend->run(cfg);
        CHECK(r.kind == gbarecomp::LauncherResult::Kind::Launch);
        CHECK(r.rom == fx.rom);
        // The ready screen drew the resolved path in the verified green.
        bool green_path = false;
        for (const auto& t : texts())
            if (t.frame == 0 && t.color == rgb(kReadyGreen)) green_path = true;
        CHECK(green_path);
    }

    std::printf("launcher_raygui_test: PASS (skip opens no window, first-"
                "frame screen, Escape quit, focus ring, click/Enter launch)\n");
    return 0;
}
