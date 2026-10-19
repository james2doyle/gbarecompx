# packaging — portable release builds

Current raylib-era scripts:

| Path | What |
|---|---|
| `package_linux.sh` | Stage a portable Linux folder/tarball and a Steam-Deck-friendly AppImage |
| `package_macos.sh` | Stage a portable macOS folder/tarball (raylib is statically linked; no dylib bundling) |
| `package_android.sh` | Scaffold-level Gradle/APK wrapper (private and distributable personalities) |
| `package_release.ps1` | Windows/MinGW portable folder + zip, static runtime, PE import guard |
| Windows cross-build | The FFTA reference uses `tools/make_release.py --windows` (MinGW cross from Linux); this is consumer-owned |

The SDL-era Flatpak manifests, dylib-collection walks and the old
`package_linux.sh`/`package_macos.sh` were removed when raylib became the
product backend; the static-linked release is an executable plus staged
assets. Git history preserves the SDL versions if a port ever needs them.

## Linux / Steam Deck (AppImage)

`package_linux.sh` builds (or takes) an executable, stages the consumer's
payload, writes a portable folder/tarball, and optionally an AppImage:

```sh
gbarecompx/packaging/package_linux.sh \
    --target FFTARecomp --name FFTARecomp --version v1.7 \
    --binary build-compat/FFTARecomp \
    --payload game.toml --payload bios --payload mods \
    --payload recomp/launcher/boxart.png=assets/img/boxart.png \
    --icon recomp/launcher/appimage-icon.png
```

Output in `release-stage/`:

```
<name>-<version>-linux-<arch>/                 portable payload
<name>-<version>-linux-<arch>.tar.gz
<name>-<version>-linux-<arch>.AppImage
<name>-<version>.AppDir/                        with --appdir-only
```

**The writable-sidecar rule.** gbarecomp games conventionally resolve
`rom.cfg`, `config.ini`/`keybinds.ini`, and `saves/` beside the executable.
An AppImage mount is read-only, so the generated `AppRun` unpacks
`usr/bin/` to `$XDG_DATA_HOME/<app>/app` (default
`~/.local/share/<app>/app`) and runs there. Updates copy the new payload
over that directory **without deleting player files**. Pass
`--run-in-place` only when the game already stores its sidecars in XDG/user
directories.

On Steam Deck / Game Mode (`SteamDeck=1` in the environment), pass
`--steamdeck-args "--fullscreen=1 --resize-view"` (or whatever display
flags the game supports); `AppRun` prepends them. Keep an explicit
consumer-side opt-out if players may want windowed mode.

**glibc floor.** SteamOS does not provide a rolling distro's glibc. Build
the shipped executable in an old environment (Steam Runtime SDK or a pinned
Debian 12 container) and check it:

```sh
objdump -T build-compat/FFTARecomp | grep -o 'GLIBC_[0-9.]*' | sort -V | tail
```

The FFTA reference implementation is
[`tools/compat_build.py`](https://github.com/james2doyle/ffta-recomp/blob/main/tools/compat_build.py)
with [`tools/compat/Containerfile`](https://github.com/james2doyle/ffta-recomp/blob/main/tools/compat/Containerfile)
(Debian 12, glibc 2.36 floor; the produced binary requires 2.35). Do not
bundle glibc, the graphics stack, or libstdc++; they must come from the
host.

**AppImage checks.** The script pins appimagetool 1.9.1 and the AppImage type-2 runtime by SHA-256 and
supports `--appimagetool PATH`, `--appdir-only` (CI/staging, no download),
`--no-appimage`, `--no-tarball`, and `--clean`. It applies generic portable
hygiene (no symlinks; no `.gba/.agb/.sav/.srm/.apk`), and runs the
executable's `--help` startup smoke unless `--no-smoke`. Consumer policy
still owns the ROM/BIOS hash sweep, release signing, upload, and the strict
coverage smoke; the FFTA reference implementations are
[`tools/make_appimage.py`](https://github.com/james2doyle/ffta-recomp/blob/main/tools/make_appimage.py)
and
[`tools/publish_release.py`](https://github.com/james2doyle/ffta-recomp/blob/main/tools/publish_release.py).
After building, extract the AppImage (`--appimage-extract`, or
`--appimage-extract-and-run` where FUSE is unavailable), rerun the
consumer's private-material sweep, and boot the packaged AppImage to the
game's strict coverage line.

## macOS

`package_macos.sh` stages a flat portable folder and tarball:

```sh
gbarecompx/packaging/package_macos.sh \
    --target FFTARecomp --name FFTARecomp --version v1.7 \
    --payload game.toml --payload bios --payload mods
```

A `.app` bundle is deliberately not used: macOS validates everything under
`Contents/MacOS` as signed code, but gbarecomp resolves assets beside the
executable, so those assets cannot be ad-hoc signed there. The flat
directory runs signed or unsigned and keeps the runtime's sidecar layout.
raylib is linked statically, so the script only verifies that `otool -L`
shows system libraries; signing/notarization belongs to the consumer.

## Windows (MinGW)

`package_release.ps1` builds/stages a static MinGW release on Windows:

```powershell
.\gbarecompx\packaging\package_release.ps1 `
    -Target FFTARecomp -Name FFTARecomp -Version v1.7 `
    -Payload game.toml,bios,mods,assets
```

It configures `GBARECOMP_STATIC_RELEASE=ON` with
`-static -static-libgcc -static-libstdc++` and a deterministic PE
timestamp, stages the executable/payload into
`release-stage\<name>-<version>-windows-x64`, rejects third-party DLL
imports, and writes the zip. The FFTA reference instead cross-compiles
from Linux with `tools/make_release.py --windows` (same static contract,
plus the objdump and wine gates); both routes are current.

## Android (APK)

The Android scaffold is framework material (`platform/android/`): a
NativeActivity app whose manifest's `android.app.lib_name` (`main`) loads
the game's corpus. Gradle's `externalNativeBuild` drives the game's own
CMake through `platform/android/cpp/CMakeLists.txt`, which pins
`PLATFORM=Android`, points FetchContent at **one raylib checkout per ABI**
(the source dir is shared per ABI; different ABIs stay isolated), and
pulls the vendored native_app_glue for the lifecycle.

### `package_android.sh` — the scaffold-level build wrapper

```sh
gbarecompx/packaging/package_android.sh [--project DIR] [--abis LIST]
    [--jobs N] [--private-rom FILE] [--private-bios FILE]
    [--property K=V]... [--release] [--clean-repackage]
    [--freshness-input FILE]... [--post-build CMD] [--install]
```

It checks JAVA_HOME/ANDROID_HOME presence, verifies the corpus exists
**and is fresh against the regen stamp** (fail-loud on a stale corpus,
optionally against `--freshness-input` files the consumer declares),
passes the scaffold's own Gradle properties through (`-PgbaAbis`,
`-PgbaNativeJobs`, `-PprivateRom` — resolved to absolute paths, removing
the relative-to-`android/app` footgun — `-PprivateBios`, keystore
properties via `--property`), computes `versionName` from the checkout's
`git describe` (overridable), optionally cleans `app/build` before the
build, and reports each produced APK with size and sha256.
`--post-build CMD` (run from the consumer repo root with `APK_PATHS` set)
is the hook for a content guard or device gate; `--install` does
`adb install -r` of the APK matching the attached device's ABI.

What it deliberately does not do — payload policy, device gates, the
release allowlist stage, the private-material sweep, signing-key
management — belongs to the consumer's own wrapper (`build_apk.py` /
`make_release.py` in the FFTA repo).

### Packaging rules that hold for any consumer

- **Two build personalities.** A *private* build embeds the ROM
  (test-only: never committed, never distributed — the FFTA pipeline names
  it in the artifact, `...PRIVATE-rom-included.apk`); a *distributable*
  build ships without it and imports via the first-boot picker. The public
  path runs a private-material sweep and a content guard.
- **The corpus must be fresh.** An APK embeds the desktop-generated corpus
  (the Android build cannot produce it — the recompiler is a desktop
  tool). The preflight must check presence AND freshness against a regen
  stamp; mtimes cannot date a regeneration (byte-identical output keeps
  them).
- **Clean repackage before a distributable build.** Incremental
  repackaging can strand bytes of a prior build in the zip slack —
  observed twice: a 67 MB junk blob, then ~30 MB of stale entry data
  between the last entry and the signing block, invisible to
  `infolist()`-based checks. Remove the Gradle build dir first (native
  objects under `.cxx` survive) and run a content guard that rejects
  duplicate and unexpected large entries.
- **Never commit APKs.** Private builds embed the ROM. Keep outgoing
  builds in a gitignored archive with manifests + SHA256SUMS so device
  behavior can be compared across builds; all debug-key builds swap in
  place with `adb install -r` (saves preserved).
- **versionName comes from the checkout** (`git describe --tags` minus
  the leading `v`), so an installed APK always names its own commit — the
  stale-install guard compares against it.

The reference consumer's operational guide carries the per-game recipe
(exact Gradle invocations, JDK/SDK preflight, the build+guard+device-gate
wrapper, the release packager and its debug-signing caveat):
`android/README.md` in the FFTA repo.

## What is still true here

- **The ROM and the BIOS are never packaged.** A recompiled binary already
  embeds translated ROM code, so packaging is for building **your own**
  copy from **your own** dump. The launcher prompts for both on first
  run.
- **`generated/` is not committed anywhere in this ecosystem**; regenerate
  it locally (the consumer's change cycle) before any packaging step.
- `packaging/tests/test_packaging.sh` checks the current scripts: shell
  syntax, the Linux portable/AppDir staging, the writable AppRun contract,
  unsafe-name rejection, and the macOS script's argument parsing. It needs
  only bash; `desktop-file-validate` is used when present.
