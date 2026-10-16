# GBARecomp (raylib/V2 fork)

> This tree is a noncommercial fork of
> [`mstan/gbarecomp`](https://github.com/mstan/gbarecomp) (frozen as
> `4cc306b`, then diverged). Fork modifications are under the same terms —
> see [License](#license). For upstream's project description, build guide,
> and game catalog, read upstream.

This README covers **only what differs from upstream**.

## Window backend: V2 seam + raylib 6

Upstream's SDL host layer is gone. The host window/audio/input surface is
the backend-neutral V2 seam (`src/runtime/window_backend.h`:
`Video`/`Audio`/`Input`/`Fx`/`Overlay`/`Menu` plus the facade); raylib 6 is
its first implementation (`src/runtime/raylib_backend.cpp`, fetched by
pinned SHA — see `THIRD_PARTY_ATTRIBUTION.md`). Capabilities, errors, and
threading are contracts in the header, not discovered by absence of effect.

What the raylib backend delivers that SDL2 could not:

| Feature | What it does | Status |
|---|---|---|
| LCD subpixel-grid shader | GPU-side RGB mask (period 3), pre-mask gain | Opt-in, default off; invisible to frame hashes (`GBARECOMP_LCD_SHADER` / `[video] lcd_shader`) |
| Bayer dither | Screen-space pass against BGR555 gradient banding | Own program + target, independent of the grid (`lcd_dither`) |
| Adaptive deband | Vertical ±2-tap pass in display LSB | Default off (`lcd_deband`) |
| Headless tier | rlsw software GL + `PLATFORM_MEMORY`: present/readback with no display server | `-DGBARECOMP_RAYLIB_HEADLESS=ON` (shaders compiled out there) |

## In-game settings menu

Upstream's runtime UI is not ported to this backend. In its place the
framework owns a toolkit-free generic menu: pure model + mutators
(`src/runtime/menu_model.h`), backend presentation, host wiring
(`src/runtime/host_menu.h`). Desktop trigger is `Delete` (toggles;
rebindable via `[KeyMap] Menu`); touch devices use the long-press ring.

## Android

Upstream's SDL application shell is retired. The scaffold is
raylib/`NativeActivity` (`platform/android/`); the app pauses/rebinds on
lifecycle events and never closes the window.

## Test tiers

Upstream's suite plus, per backend, display-free coverage that runs with no
display server:

| Tier | Command | Covers |
|---|---|---|
| Stub | `cmake -S . -B build-stub -DGBARECOMP_USE_RAYLIB=OFF`, `ctest` | Seam compile-check + pure tests |
| raylib (GL) | `cmake -S . -B build-raylib -DGBARECOMP_USE_RAYLIB=ON`, `ctest` | Full suite; windowed tests skip without a display |
| Headless | `cmake -S . -B build-raylib-headless -DGBARECOMP_USE_RAYLIB=ON -DGBARECOMP_RAYLIB_HEADLESS=ON`, `ctest` | Present/readback/lifecycle smoke under rlsw |
| GL under Xvfb | same GL build, `xvfb-run -a ctest` | Real open/present/close on llvmpipe |

## License

This fork is distributed under the **PolyForm Noncommercial License
1.0.0** — noncommercial use only; see [`LICENSE`](LICENSE). Fork
modifications are under the same terms. Nintendo GBA BIOS images, game
ROMs, saves, and extracted game assets remain copyrighted by their
respective owners and are not distributed by this project.
