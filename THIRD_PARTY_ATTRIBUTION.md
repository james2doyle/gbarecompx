# Third-Party Attribution

Portions of this project are ported from other open-source projects,
used with permission and under their licenses. Facts (struct offsets,
byte signatures, documented hardware/driver behavior) and, where noted,
ported logic are credited below. License texts of permissively-licensed
upstreams are reproduced in their repositories.

## JRickey/gba-recomp

- **Upstream:** https://github.com/JRickey/gba-recomp
- **Author:** Jrickey
- **License:** MIT OR Apache-2.0 (used with the author's permission)

| Our file | Upstream source | What was ported |
|---|---|---|
| `src/gba/gba_m4a.{h,cpp}` | `crates/gba-core/src/mp2k.rs` | MP2K ("m4a") SDK driver detection (SoundMain literal-pool signature + CRC fallback) and the SoundInfo / SoundChannel / WaveData guest-struct offset map. Re-implemented in C++; the live-state dump path is original to this project. |
| `src/runtime/color_lut.{h,cpp}` | `crates/screen/src/{color,profile,lut}.rs` | Screen-color simulation: CIE colorimetry core, measured per-revision panel models, and the BGR555→RGBA8 LUT build. Re-implemented in C++; the present-time RGB888-input apply path is ours. |
| `src/gba/gba_rtc.{h,cpp}` | `crates/gba-core/src/{rtc,hostclock}.rs` | Cartridge GPIO port + S-3511A RTC (serial state machine, BCD date/time, Seiko signature detection) and the civil↔linear host-clock helpers (Hinnant day algorithms, public domain). Re-implemented in C++ (Win32 `GetLocalTime` / POSIX `localtime_r`). |
| `src/gba/audio_shadow.{h,cpp}` | `crates/gba-core/src/shadow.rs` | Engine-agnostic HLE-shadow differential verifier: envelope-correlation self-check vs the canon FIFO stream, probation auto-gain, prove/strike/pause-and-reprobe. Re-implemented in C++. (Permitted under PRINCIPLES.md "Verified-enhancement HLE".) |
| `src/gba/mp2k_shadow.{h,cpp}` | `crates/gba-core/src/mp2k.rs` | MP2K HLE shadow mixer: float voice re-render (envelope mirror, PCM/DPCM sampling, intra-tick gain interpolation, reverb ring) driving the differential verifier. Re-implemented in C++. |

Each ported file carries an attribution header pointing here. Ported
code remains under the upstream's MIT OR Apache-2.0 terms; this notice
and those headers satisfy the attribution requirement.

## mGBA

- **Upstream:** https://github.com/mgba-emu/mgba (cloned locally for the
  oracle target; `third_party/mgba/` is gitignored and NOT tracked here)
- **Author:** Jeffrey Pfau and contributors
- **License:** MPL-2.0

| Our file | Upstream source | What was ported |
|---|---|---|
| `src/runtime/bios_hle.{h,cpp}` | `src/gba/bios.c` | GBA BIOS SWI High-Level Emulation: the Div/Sqrt/ArcTan/ArcTan2 fixed-point routines and stall formulas, the LZ77 / Huffman / run-length / diff-unfilter decompressors, BitUnPack, the (float) affine-matrix builders, and MidiKey2Freq. Re-implemented in C++ against this project's `g_cpu` + bus bridge. HLE is opt-in; LLE (the recompiled real BIOS) remains the default and the correctness oracle (PRINCIPLES.md "verified-enhancement HLE"). |

`src/runtime/bios_hle.{h,cpp}` are derived from mGBA's `src/gba/bios.c` and
are therefore Covered Software under the MPL-2.0: **those two files are
licensed MPL-2.0, not PolyForm Noncommercial**, and each carries the Exhibit A
notice. The full license text is in
[`third_party/MPL-2.0.txt`](third_party/MPL-2.0.txt).

MPL-2.0 is file-scoped weak copyleft, so this does not affect the license of
the rest of gbarecomp: section 3.3 expressly permits distributing a Larger
Work under other terms provided the Covered Software stays under the MPL. The
source-availability requirement (section 3.2) is met by those two files being
present in this public repository — they *are* the Source Code Form of the
modified Covered Software. An earlier revision of this notice instead claimed
the obligation was satisfied by a vendored copy at
`third_party/mgba/src/gba/bios.c`; that path is gitignored and has never been
tracked here, so the claim was wrong. Nothing else in this repository is
MPL-covered.

libmgba itself is linked only into the opt-in oracle binary
(`-DGBARECOMP_BUILD_ORACLE=ON`), never into a shipping native build.

## Android Open Source Project — native_app_glue

- **Upstream:** https://android.googlesource.com/platform/frameworks/native/+/master/libs/native_app_glue
  (via the Android NDK 28.2.13676358, `sources/android/native_app_glue/`)
- **License:** Apache-2.0
- **Vendored as:** `platform/android/native_app_glue/android_native_app_glue.{c,h}`
  (the Android scaffold's NativeActivity entry — every Android game build)
- **Modification:** one hunk — the app thread is created with a 256 MiB
  stack (`pthread_attr_setstacksize`, matching `kMobileGameStackBytes`),
  because raylib is glue-thread-bound and the game runs on that thread.
  See `platform/android/native_app_glue/README.md`.

## raylib

- **Upstream:** https://github.com/raysan5/raylib (6.0,
  `dbc56a87da87d973a9c5baa4e7438a9d20121d28`)
- **Author:** Ramon Santamaria and contributors
- **License:** zlib/libpng
- **Used as:** the window/audio/input backend (`src/runtime/raylib_backend.cpp`
  via `src/runtime/raylib_api.h`), fetched by pinned SHA at configure time
  (`FetchContent`; also usable from a parent-provided target or the headless
  `PLATFORM_MEMORY` build). Not vendored; no modifications.

## tinyfiledialogs

- **Upstream:** https://tinyfiledialogs.sourceforge.net / git:
  `git://git.code.sf.net/p/tinyfiledialogs/code` (mirror:
  https://github.com/nodef/tinyfiledialogs.c)
- **Version:** v3.21.2 (Oct 25, 2025)
- **Author:** Guillaume Vareille (ysengrin.com)
- **License:** zlib
- **Vendored as:** `third_party/tinyfiledialogs/tinyfiledialogs.{c,h}`
- **SHA-256:** tinyfiledialogs.c `99a5d176292b55e9c0c5322efa0e538a818e64450d640114ab97bba0e8f29f83`,
  tinyfiledialogs.h `4d90f0843cd241a14d514b03a28bc4f1317297a671d4b541c8c4dedc93642f37`
- **Used as:** native file dialogs for the asset picker
  (`src/runtime/asset_picker.cpp`), Phase 2. Console-input fallback is disabled
  at runtime via the `tinyfd_query` probe (never blocks on stdin).
- **Modifications:** none.

## raygui

- **Upstream:** https://github.com/raysan5/raygui (5.1-dev on master;
  v5.0 released 2026-07-20)
- **Author:** Ramon Santamaria (@raysan5)
- **License:** zlib
- **Vendored as:** `src/vendor/raygui/raygui.h` (single header) +
  `src/vendor/raygui/LICENSE`
- **SHA-256:** raygui.h `8b19249c713db7bc75e9c1e4ff19b9b10b0eaad16988d6a0dc65c6108a210625`
- **Used as:** the raygui launcher backend
  (`src/runtime/launcher_raygui.cpp` — delivered); guarded by the
  launcher link tests.
- **Modifications:** none.


## MMPX

- **Upstream:** McGuire & Gagiu, "MMPX Style-Preserving Pixel Art
  Magnification", Journal of Computer Graphics Techniques (JCGT) vol. 10
  no. 2, 2021 — https://jcgt.org/published/0010/02/04/ (open access;
  reference code MIT). The vendored C99 form is credited in-file to
  ITotalJustice's transcription of the authors' code.
- **License:** MIT
- **Used as:** `src/runtime/mmpx.h` — an algorithm-level C++ mirror + GLSL
  transliteration (both dialects) of the rule cascade, our own code: no
  libretro slang text was carried. `tests/runtime/support/
  mmpx_reference_c99.c` — a vendored, **test-only** copy of the
  reference C99 implementation, used as a byte-for-byte differential
  oracle.
- **Modifications:** the oracle's `clamp()` min/max comparison is
  corrected to the documented intent (upper-clamp never fired upstream —
  out-of-bounds reads on malformed input); DEVIATION note in-file. Our
  GLSL assumes opaque input (FFTA's framebuffer) — equality is RGB-only;
  the mirror stays alpha-faithful and the differential test runs opaque
  data, where the two agree exactly.


## emibios

- **Upstream:** https://github.com/coolbho3k/emibios
- **Author:** coolbho3k (clean-room GBA BIOS reimplementation)
- **License:** LGPL-3.0-or-later
- **Used as:** the DEFAULT launch BIOS (2026-10 fork decision): the
  runtime's default SHA-1 gate (`kEmibiosSha1`), the asset picker's
  fallback expectation, and the netplay lockstep requirement. The binary
  is NOT in this repo — each consumer game ships its own committed copy
  (e.g. the FFTA recomp's `bios/emibios_gba_bios.bin` + recompile TOML).
  See `bios/README.md`.
- **Modifications:** none in this tree.

## Capstone

- **Upstream:** https://www.capstone-engine.org/
- **License:** BSD-3-Clause (redistributed under the upstream license; not
  vendored — a tooling dependency, installed per environment)
- **Used as:** the disassembly engine behind `tools/disarm.py` and
  `tools/misspack.py` (the evidence workflow: every address claim in a
  consumer's game.toml carries a disassembly note these tools produce).

## m4a_detect.py

- **Lineage:** Bregalad & loveemu — `sappy_detector.c` ("GBA Mus Ripper";
  forks: berg8793/gba-mus-ripper, CaptainSwag101/gba-mus-ripper)
- **License:** credit-based (upstream states: "GBAMusRipper is free and
  open source software. Anyone is free to redistribute it ... but just
  give me (Bregalad) credit." No formal upstream license text exists.)
- **Used as:** `tools/m4a_detect.py` — a faithful Python port of the
  Sappy/m4a engine-detection logic; credit is given in the file header.
