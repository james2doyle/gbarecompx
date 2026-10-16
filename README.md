# GBARecompX

Static recompilation of Game Boy Advance games to native code: the game's
actual ARM instructions — and its BIOS — are compiled once, ahead of time,
and executed directly on your machine. No interpreter in the hot path, no
high-level approximations: the game runs its own real code, at full speed,
on desktop and Android.

This tree is the actively developed fork of
[mstan/gbarecomp](https://github.com/mstan/gbarecomp) that powers the
Final Fantasy Tactics Advance recompilation. It is noncommercial and
inherits upstream's license terms (see [License](#license)).

## Why it is valuable

- **The real code, really executed.** Every guest function the game runs
  is lowered from the actual ROM bytes; the GBA BIOS is recompiled and
  dispatched the same way — SWIs and IRQs included. There is no
  hand-written BIOS shortcut hiding under the accuracy.
- **Included clean-room BIOS.** The default launch BIOS is **emibios**, a
  clean-room GBA BIOS reimplementation (LGPL-3.0-or-later) that consumer
  games ship with their build — so a game boots with no copyrighted
  Nintendo dump anywhere. A retail dump remains an accepted alternative
  for reference and verification work.
- **Honest about what it doesn't know.** Code the compiler cannot lower
  yet aborts loudly instead of guessing. Code the game executes beyond
  the precompiled set self-heals: an interpreter bridge runs it and
  recompiles it on the fly, loudly, so coverage is always a measured
  fact — never an assumption.
- **Verified, not assumed.** A lockstep mGBA oracle runs the same ROM
  and the framework diffs machine state frame by frame and cycle by
  cycle. Attract-mode golden hashes, widescreen smoke fixtures, and
  strict replay routes pin every executed path; CI runs the full gate on
  every push.
- **Enhancements without behavior change.** Widescreen view modes
  (native, adaptive, fixed 16:9), LCD subpixel-grid and ghosting
  shaders, an in-game settings menu that persists per game,
  fast-forward and rewind assist, and a mod-package loader — all opt-in
  and safe to the frame hashes by construction.
- **A real player experience.** A **raygui pre-boot launcher** fronts the
  game: Play, select a ROM (drag-and-drop or the native file dialog),
  first-boot picking, gamepad navigation, optional box art. In game, the
  settings overlay carries the view modes, LCD effects, audio and assist
  toggles — all persisted.
- **Runs where players are.** raylib 6 on desktop with packaged releases
  (portable zip, static Windows executable, public Android APK), and an
  Android scaffold with touch controls and a complete
  suspend/resume/relaunch lifecycle.

## What replaced upstream's SDL2 host and recomp-ui

Upstream hosted games through SDL2 and drove menus through recomp-ui (a
separate UI checkout with its own renderer and font stack). Both are gone
here, replaced by lighter things the framework owns outright.

**SDL2 host → the raylib window backend:**

| Capability | The SDL2 era | The raylib era |
|---|---|---|
| Host layer | SDL2 window/audio/input glue; SDL2 dev packages on every build machine | One backend-neutral seam; raylib 6 fetched by CMake at configure time; no system GUI packages anywhere |
| Display effects | CPU blits; CRT-style filters at best | GPU-side LCD subpixel-grid shader (one cell per game pixel, scale-aware, anti-aliased), Bayer dither against BGR555 banding, adaptive deband — each an independent pass, off by default, invisible to frame hashes |
| Headless / CI | A display server required for anything visual | Software-GL headless tier: present/readback with no display server at all |
| Android | The SDLActivity shell and its class-loader quirks | A direct NativeActivity scaffold: touch controls, suspend/resume, no SDL |
| Contracts | Discovered by absence of effect | Capabilities, errors and threading are contracts in one interface |

**recomp-ui → the raygui launcher + the in-game menu:**

| Capability | The recomp-ui era | The raygui era |
|---|---|---|
| Footprint | A separate UI submodule; SDL2, freetype and harfbuzz cross-compiled for every platform; its own renderer to keep in sync | One raygui header (zlib) + tinyfiledialogs for native dialogs; everything links into the single game binary |
| Pre-boot experience | None — CLI flags or a config file | A launch screen: Play, select ROM (drag-and-drop included), first-boot picker, gamepad navigation, optional box art |
| In-game settings | The recomp-ui menu surface, carried per backend | The framework's own generic menu: every row applied live and persisted per game, including the widescreen View modes |
| Mods surface | recomp-ui's Mods UI | The launcher-seam option surface, hidden unless the game supplies a catalog |

## How a consumer game uses it

A game repo pins this framework as a submodule and describes itself in a
small TOML config — identity pins and audit seeds, each with an evidence
note. It inherits the whole harness: the recompiler, the runtime host,
the verification oracle, the playtest→heal-loop tools, and the
regression gates. The FFTA recompilation is the reference consumer.

## Building and testing

Configure with CMake (raylib is fetched at configure time), build the
recompiler and the runtime, and run the CTest suite. Display-free tiers
cover every configuration — a stub build (compile-check), the full raylib
build, a software-GL headless build — plus the real windowed suite under
Xvfb in CI. The protocol tools (oracle diff and lockstep probes, the heal
loop, ring and trace queries) ship in `tools/` with their own tests; a
`guard` CI job polices structural invariants. The `docs/` directory
carries the deep material: architecture, the timing model, the oracle
protocol, multiplayer, mod packages, the accuracy burndown.

## License

This fork is distributed under the **PolyForm Noncommercial License
1.0.0** — noncommercial use only; see [`LICENSE`](LICENSE). Fork
modifications are under the same terms. Nintendo GBA BIOS images, game
ROMs, saves, and extracted game assets remain copyrighted by their
respective owners and are not distributed by this project. Component
credits and licenses: `THIRD_PARTY_ATTRIBUTION.md`.
