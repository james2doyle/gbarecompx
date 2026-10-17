# packaging — portable release builds

> **STATUS (2026-10-17): LEGACY — SDL-era, unverified against the current
> product.** These scripts were written when every game exe was a plain
> SDL2 program. Phase 4b retired SDL2 and Phase 4c made raylib the product
> backend, fetched by pinned SHA and **linked statically** — so the
> dylib-bundling machinery below (`otool`/`ldd` walks, the
> sdl2-compat/SDL3 dlopen hunt, `$ORIGIN` rpaths for shim libraries) has
> nothing left to bundle, and the Flatpak manifests still build an SDL2
> module that no current target links. Nothing here has been run against a
> raylib build; treat every command in this directory as unverified until
> someone re-validates it.

## The live release pipeline

The reference consumer's packaging does not use this directory. The FFTA
recomp ships through its own deterministic, guarded pipeline —
`tools/make_release.py` in the consumer repo — which produces the Linux
portable zip, the public Android APK, and the static MinGW Windows
executable from a single allowlist stage (private-material sweep included,
content-guarded). A new consumer game should copy that shape rather than
revive these scripts by default: with raylib linked statically, a release
is an executable plus assets, and most of the complexity below exists to
solve dynamic-SDL problems that no longer occur.

## What is still true here

- **The ROM and the BIOS are never packaged.** A recompiled binary already
  embeds translated ROM code, so packaging is for building **your own**
  copy from **your own** dump. The launcher prompts for both on first
  run.
- **`generated/` is not committed anywhere in this ecosystem**; regenerate
  it locally (the consumer's change cycle) before any packaging step.
- `packaging/tests/test_generation.sh` still checks the script mechanics
  (shell syntax, metadata generation from a source path containing `&`,
  both Flatpak YAML forms, metainfo XML, desktop-entry validation, and
  that ROM/BIOS plus arbitrary `build-*` directories are excluded). Needs
  PyYAML and `desktop-file-validate` on Linux.

## The scripts, as they stand

| Path | Era | State |
|---|---|---|
| `package_macos.sh` | SDL | Was verified booting a real ROM (SDL build); untouched since |
| `package_linux.sh` | SDL | Was **untested** even in the SDL era ("counterpart to the macOS script") |
| `flatpak/` (manifests + `build_flatpak.sh`) | SDL | Was verified on a real Deck (SDL build); the manifests fetch SDL2 2.30.9 as a module |
| Windows | — | Lives in the consumer's release pipeline (see above), not here |

## If you revive this for raylib

The work is smaller than the history: drop the SDL2 module from both
Flatpak manifests (raylib needs only GL/EGL and the X11 client libs the
freedesktop runtime already ships — or add raylib as a module if you want
it built in the sandbox), delete the dylib-bundling walks from the
macOS/Linux scripts (a statically-linked exe ships alone), re-point the
example targets and flags at a current consumer, and then re-verify on
real hardware — the Deck verification predates the backend swap by two
backend generations.
