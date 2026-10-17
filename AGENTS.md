# AGENTS.md — GBARecompX (agent instructions)

The actively developed, public fork of
[mstan/gbarecomp](https://github.com/mstan/gbarecomp). The Final Fantasy
Tactics Advance recompilation (`james2doyle/ffta-recomp`) is the reference
consumer; this repo is the framework any gbarecomp-style game builds on.

## Rule sources (read what the task needs — do not redefine them here)

| Topic | File |
|---|---|
| Correctness principles, HLE policy, coverage honesty | `PRINCIPLES.md` |
| Debugging protocol, frame ring, oracle discipline | `DEBUG.md` |
| TCP debug surface | `TCP.md` |
| Upstream-inherited agent rules (MinishCap-era wording; the principles are current) | `CLAUDE.md` |
| Consumer-facing config schema | `docs/TOML_SCHEMA.md` |

## Ownership (what lives here vs in a consumer game)

This repo owns: `src/armv4t` (the recompiler), `src/gba` + `src/runtime`
(the runtime, window backends, the launcher, touch/mobile layers, the
per-game-config schema and its parsing), `tools/` (the protocol and
evidence tools), `docs/` — **and the tests for all of the above**.

A consumer game owns: its `game.toml` + symbol tables, its `src/ffta_*`-
style host code, the BIOS copy, the Android payload/packaging — and the
tests for those only.

**Tests live with the code they test** (2026-10-17 split). A consumer
project testing framework headers in its own CI is a placement bug: it
hides framework regressions from the framework's gate until a consumer
bumps. Framework changes land with their tests HERE, in
`tests/runtime/`, following the layers below.

## The test layers (new backend/runtime work lands with all applicable layers)

1. **Pure functions** — mapping tables, deadzones, decision logic;
   differentially tested against a reference implementation where one
   exists (MMPX: the vendored C99 oracle, byte-for-byte over random
   images).
2. **A fake raylib API** — backend decisions pinned at the call level
   (uniform uploads, program lifecycle, present-chain ordering,
   config-enabled lazy compiles). Deterministic and driver-independent;
   runs in every tier; cannot flake.
3. **The real framebuffer** — `raylib_present_test.cpp` opens a real GL
   context and pins pixels. It carries its own environment guardrails:
   a readback-liveness gate (different frames must read differently —
   Mesa llvmpipe made stale reads look like a dead feature), a settle
   pass after every window resize, and diff counts printed on every
   run. CI runs it under Xvfb+llvmpipe; harden any new pin against that
   renderer the same way.

CI: four jobs per push (stub / raylib+Xvfb / guard / headless-rlsw) —
all green or the release waits; the badge in `README.md` is the state.
Config-adaptive tests assert BOTH build personalities (the backend
factory: product backend with raylib ON, `nullptr` without it).

## Input capture policy (Android)

`src/runtime/android_gamepad.h` + the glue-level hook trust the KEYCODE,
not the per-event source bits: devices that flag gamepad buttons as
keyboard events (the raylib #5387 device class) keep full controller
support, and KeyCharacterMap fallback synthesis (the device keymap turns
an unhandled BUTTON_B into BACK — the "menu on B" symptom) is consumed
at the hook before raylib or the dispatcher can act on it. Extend the
tables and their tests there; do not re-route gamepad handling through
raylib's Android backend.

## Public-repo hygiene

Summaries + attribution only, never verbatim third-party text; keep
`THIRD_PARTY_ATTRIBUTION.md` current on every vendoring or
algorithm-level port (license + what was ported + what was not); no
ROM-derived bytes anywhere in this repo, ever.
