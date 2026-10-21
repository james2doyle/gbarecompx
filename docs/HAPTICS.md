# Android haptics (fork-only)

> **Fork-only.** This is a new capability in GBARecompX. Upstream
> `mstan/gbarecomp` has neither a platform-vibrator sink nor primary-input
> haptic routing; this page documents the fork's addition.

## Status

| Piece | State |
|---|---|
| Android phone vibrator | **Live** (issue #30 Phase 1) |
| `host_primary_haptic` routing | **Live**; touchscreen routes to the phone |
| Controller motors | Seam ready; Android sink deferred (Phase 2 glue device-id capture) |
| Battle-damage trigger | **Live in the FFTA game repo** (issue #30 Phase 4, device-verified 2026-10-20) |
| User setting | Deferred (Phase 5) |

The FF pill's existing `host_haptic_pulse(15, 0.5)` is the first real
consumer: on Android it now buzzes the phone instead of reaching raylib's
logging stub.

## API

- `host_haptic_pulse(int duration_ms, float strength)` — existing system
  pulse hook; now wired to the Android vibrator.
- `host_primary_haptic(float strength, uint32_t duration_ms)` — routes a
  gameplay pulse to the controller when the pad is primary, otherwise to the
  system vibrator. No-op without an open window; duration is clamped.
- `HostWindow::controller_is_primary()` / `Input::controller_is_primary()` —
  true only when the pad's last activity is strictly newer than the
  touchscreen's.
- `Caps::haptics` is true on Android (the sink is wired); `Caps::rumble`
  stays false until the controller-motor sink lands.

## Android path

`host_haptic_pulse` / `host_primary_haptic` → `RaylibBackend::InputImpl` →
`android_haptics_pulse` (JNI) → `GbaGameActivity.vibrate(int, float)` →
`Vibrator` / `VibratorManager`.

- API 31+: `VibratorManager.getDefaultVibrator()`; older releases use
  `Context.VIBRATOR_SERVICE`.
- Amplitude: 1..255 when `hasAmplitudeControl()` is true, otherwise
  `VibrationEffect.DEFAULT_AMPLITUDE`.
- Attributes: API 33+ `VibrationAttributes.USAGE_MEDIA`; API 26-32
  `AudioAttributes.USAGE_GAME`. A bare `vibrate(effect)` is `USAGE_TOUCH`,
  which OEMs that disable touch haptics can drop silently.
- The `VIBRATE` permission already ships; the app must be in the foreground.
- No vibrator, no JNI path, or a platform exception is a quiet no-op.

## Primary routing

The backend stamps both modalities from one monotonic clock once per input
pump. Any mapped pad button or deadzoned stick/trigger reading refreshes the
pad stamp; any live touch refreshes the touch stamp. The pad wins only when
strictly newer, so simultaneous activity and the default state keep the
touchscreen primary. Desktop reports `false`, preserving its historical
pad-motor haptic path.

## Verification

- `input_translate_tests` — pure `pad_is_primary` cases (newer pad, newer
  touch, tie, wrap).
- `primary_haptic_tests` — fake-backend sink counters; skips loudly in the
  no-backend build.
- `host_window_closed_tests` — closed-window fallback.
- The consumer's full gate: `tools/check.py` 17/17, attract hash unchanged.
- Device: the FF pill pulses the phone vibrator (user-confirmed 2026-10-20).

## Damage haptics (consumer design, FFTA game repo)

The FFTA game repo (issue #30 Phase 4) drives `host_primary_haptic` from
battle damage; the design lessons are framework-relevant because every
consumer will face the same shape:

- **Damage applies through more than one code path.** FFTA uses a shared HP
  delta helper for the melee/fight family AND a direct HP store for
  enemy-unit ability damage (`strh r1,[unit+0x1C]`) — a hook on the helper
  alone silently misses whole attack classes. Map every apply path before
  trusting a single seam.
- **Display sync beats a fixed delay.** The HP apply happens at damage
  calculation; the damage number appears at the animation's impact — the
  gap varies per animation (measured ~22 to ~240 frames). The game hooks
  the number's popup initializer and releases the pulse one frame later,
  with a grace deadline so a pulse is never lost.
- **Savestate loads must clear armed state.** The mods reset callback does
  not run on TCP/observer loads; the game's frame policy watches
  `ExtendedViewFrameInfo::state_epoch` to reset. Stale "last popup frame"
  state otherwise releases a fresh pulse immediately after arming.

Device-verified 2026-10-20: landed hits log `entry → damage → popup →
PULSE fire → haptics-sink ms=120`, pulse one frame after the popup; misses
spawn a value-0 popup and are silent (no arm). The consumer's BRINGUP entry
carries the full evidence trail (trap PCs, frames, two-load regression).

## Not in upstream

Upstream `mstan/gbarecomp` has neither the `android_haptics_pulse` JNI path
nor `host_primary_haptic`. It does model the cartridge rumble GPIO (for
titles such as WarioWare) with host vibration fallbacks; what is new here is
the Android platform-vibrator sink and the primary-modality routing.
Controller-motor output is not claimed yet: controller-primary routing
currently ends in a quiet no-op on Android until the glue device-id capture
and controller sink land (Phase 2).

## References

- Android `VibratorManager` — API 31+ default/per-motor access:
  <https://developer.android.com/reference/android/os/VibratorManager>
- `CombinedVibration` / `startParallel` — API 31+ multi-vibrator effects:
  <https://developer.android.com/reference/android/os/CombinedVibration>
- `VibrationEffect.createOneShot` — amplitude range 1..255:
  <https://developer.android.com/reference/android/os/VibrationEffect>
- `InputDevice.getVibrator()` (API 16, deprecated 31) and
  `getVibratorManager()` (API 31+):
  <https://developer.android.com/reference/android/view/InputDevice>
- `Vibrator.hasAmplitudeControl()` (API 26) and the vibration-attribute
  overloads:
  <https://developer.android.com/reference/android/os/Vibrator>
- Android haptics guidance (attributes, amplitude control):
  <https://developer.android.com/develop/ui/views/haptics/haptics-apis>
- SDL's Android rumble mapping (two vibrators = low/high; one vibrator =
  `low*0.6 + high*0.4`), the convention reused for the future controller
  sink:
  <https://github.com/libsdl-org/SDL/commit/f879411627b0c880c98486e1cec8eb1633e419bd>
