# raylib-Android spike — archive (not built by this tree)

Source-only capture of the on-device spike that proved the V2 raylib backend
on Android. Kept because it is the **Phase 4f scaffold base** and the original
lived only in volatile `/tmp` (`/tmp/opencode/raytest-android/`, BRINGUP
2026-10-14 night 12). Build outputs, logs, the Gradle wrapper jar, and
`local.properties` are deliberately not included.

## What it proved (rungs 1–3)

- **Rung 1:** `gbarecomp_raylib_backend` cross-compiles standalone for
  arm64-v8a (API 34, NDK 28.2) against raylib-Android (`PLATFORM=Android`,
  GLES2).
- **Rung 2:** minimal NativeActivity APK drives the real backend through
  open → present → pump → close, **12/12 PASS** over logcat (drawable 480×320,
  CPU readback exact).
- **Rung 3:** audio — real device opened (AAudio), 440 Hz sine at emu cadence
  for 600 frames, clean close.

## Platform law (do not relearn)

raylib-Android cannot close→reopen in one activity: `InitPlatform()` blocks on
an `INIT_WINDOW` the OS never re-sends. The Android port pauses/rebinds and
never calls `CloseWindow` (the V2 seam encodes this in `caps.reopen`).

## Promotion — plan Phase 4f

This wiring replaces the SDL shell (`platform/android/third_party/SDL`,
`SDLActivity`). The Java shell is adapted, not discarded: `GbaSetupActivity`
(SAF picker) and `GbaNative` stay; the game activity moves off `SDLActivity`
to NativeActivity. The vendored `../native_app_glue/` carries the 256 MiB
glue-thread stack this needs (see its README).

Machine paths were parameterized: `FW_ROOT` resolves to the tree root; pass
`-DRAYLIB_SRC=<raylib 6 source tree>` when building the spike. `local.properties`
(`sdk.dir`) is machine-local — recreate it.
