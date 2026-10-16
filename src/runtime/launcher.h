// launcher.h — pre-boot launcher contract (framework).
//
// Part of gbarecompx so any game target can reuse it. main() depends only on
// this header: LauncherBackend (abstract), LauncherResult, LauncherConfig,
// create_launcher_backend() factory, and launcher_should_skip_screen().
// The concrete backend (raygui, launcher_raygui.cpp) is linked into
// gbarecomp_runtime when raylib is enabled; its types never leak past this
// header.
//
// Contract surface mirrors gbarecomp's WindowBackend V2 facade: one abstract
// host-owned surface, swappable implementations behind a single header.
//
// Skip rules (Constraint 7): --rom, --bios, --no-window, --frames,
// --load-state, --tcp each skip the screen; bare invocation shows it.

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace gbarecomp {

// Outcome of running the pre-boot launcher to completion.
struct LauncherResult {
    enum class Kind : int { Skip, Launch, Quit };

    Kind kind = Kind::Skip;
    // Populated when kind == Launch: validated rom / bios paths to pass
    // straight through to gbarecomp::run_game() via explicit --rom / --bios.
    std::string rom;
    std::string bios;
};

// Configuration handed to the launcher before it runs (defined below;
// forward-declared so LauncherBackend::run can reference it).
struct LauncherConfig;

// LauncherBackend (abstract interface).
struct LauncherBackend {
    virtual ~LauncherBackend() = default;
    virtual LauncherResult run(const LauncherConfig& cfg) = 0;
};

// Configuration handed to the launcher before it runs.
struct LauncherConfig {
    // Box-art asset path; the backend decides whether/how to render it.
    const char* boxart_path = nullptr;
    // When true, the launcher auto-opens the ROM picker on first boot
    // (no sidecar cache yet). When false, it starts disabled/empty.
    bool first_boot_pick = false;
    // Executable argv[0]; the cache root for the ROM sidecar (rom.cfg).
    const char* argv0 = nullptr;
    // Resolved BIOS path (staged emibios); surfaced unchanged in Launch.
    const char* staged_bios = nullptr;
};

// Returns true when the pre-boot launcher should be skipped and run_game()
// invoked directly (headless / explicit-path invocation). Mirrors the old
// recomp-ui seam's classification, ported to this contract.
bool launcher_should_skip_screen(int argc, char** argv);

// Factory: constructs the one backend compiled into this build (raygui when
// raylib is enabled). Returns nullptr when no backend is available.
std::unique_ptr<LauncherBackend> create_launcher_backend();

}  // namespace gbarecomp
