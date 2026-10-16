# GBARecomp

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
