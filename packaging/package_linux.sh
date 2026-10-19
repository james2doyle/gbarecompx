#!/usr/bin/env bash
# package_linux.sh — package a gbarecomp game for Linux and Steam Deck.
#
# Current pipeline (raylib is statically linked; there are no SDL libraries to
# bundle): stage the executable + consumer-declared payload, emit a portable
# folder/tarball, and optionally build a Steam-Deck-friendly AppImage.
#
#   gbarecompx/packaging/package_linux.sh \
#       --target FFTARecomp --name FFTARecomp --version v1.7 \
#       --binary build-compat/FFTARecomp \
#       --payload game.toml --payload bios --payload mods --payload assets \
#       --icon recomp/launcher/appimage-icon.png
#
# Output (release-stage/):
#   <name>-<version>-linux-<arch>/                 portable payload
#   <name>-<version>-linux-<arch>.tar.gz
#   <name>-<version>-linux-<arch>.AppImage          unless --no-appimage
#   <name>-<version>.AppDir/                        kept with --appdir-only
#
# Sidecars: gbarecomp games conventionally write rom.cfg, config.ini /
# keybinds.ini and saves/ beside the executable. An AppImage mount is
# read-only, so AppRun unpacks usr/bin/ to
# $XDG_DATA_HOME/<app>/app and copies updates over it without deleting player
# files. Pass --run-in-place only when the game already stores sidecars in
# XDG/user directories.
#
# Policy stays in the consumer: ROM/BIOS content pins, release signing, upload,
# desktop metadata and the strict coverage smoke. This script applies the
# portable hygiene rules (no symlinks, no ROM/save/APK extensions) and can be
# told to run the executable's --help startup smoke.
set -euo pipefail

TARGET=""; APP_NAME=""; VERSION=""; ROOT="$(pwd)"; BUILD_DIR="build-release"
BINARY=""; ICON=""; DESKTOP=""; ARCH="$(uname -m)"
PAYLOADS=(); PAYLOAD_DIRS=(); APPIMAGE=1; TARBALL=1; APPDIR_ONLY=0; CLEAN=0
RUN_IN_PLACE=0; STEAMDECK_ARGS=""; SMOKE=1; JOBS="$(nproc 2>/dev/null || echo 4)"
APPIMAGETOOL="${APPIMAGETOOL:-}"
APPIMAGE_RUNTIME="${APPIMAGE_RUNTIME:-}"
CONFIGURE_ARGS=()

APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage"
APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
APPIMAGE_RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"
APPIMAGE_RUNTIME_SHA256="156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074"

die() { echo "package_linux: $*" >&2; exit 1; }

usage() {
    sed -n '2,36p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    cat <<'USAGE'

Options:
  --target <name>        CMake target to build (or use --binary)
  --binary <path>        use an already-built executable
  --name <name>          distribution name (default: target or binary name)
  --version <v>          version string (default: git describe)
  --root <dir>           game repo root (default: cwd)
  --build-dir <dir>      build directory (default: build-release)
  --payload <src[=dst]>  copy a file/dir into the payload (repeatable)
  --payload-dir <dir>    copy a directory's contents into the payload root
  --icon <png>           AppImage/desktop icon (required for AppImage)
  --desktop <file>       use an existing .desktop (default: generated)
  --arch <arch>          architecture label (default: uname -m)
  --jobs <n>             build parallelism (default: nproc)
  --appimagetool <path>  use a local appimagetool instead of the pinned one
  --runtime-file <path>  use a local type-2 runtime instead of the pinned one
  --no-appimage          portable folder/tarball only
  --no-tarball           AppImage only
  --appdir-only          build/stage the AppDir, do not run appimagetool
  --run-in-place         run from the mount (game must use XDG sidecars)
  --steamdeck-args "..."  arguments added when SteamDeck=1 (default: none)
  --no-smoke             skip the --help startup smoke
  --clean                remove the portable folder/AppDir after packaging
  -D<var>=<value>        passed through to the CMake configure step
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --target)         TARGET="$2"; shift 2 ;;
        --binary)         BINARY="$2"; shift 2 ;;
        --name)           APP_NAME="$2"; shift 2 ;;
        --version)        VERSION="$2"; shift 2 ;;
        --root)           ROOT="$2"; shift 2 ;;
        --build-dir)      BUILD_DIR="$2"; shift 2 ;;
        --payload)        PAYLOADS+=("$2"); shift 2 ;;
        --payload-dir)    PAYLOAD_DIRS+=("$2"); shift 2 ;;
        --icon)           ICON="$2"; shift 2 ;;
        --desktop)        DESKTOP="$2"; shift 2 ;;
        --arch)           ARCH="$2"; shift 2 ;;
        --jobs)           JOBS="$2"; shift 2 ;;
        --appimagetool)   APPIMAGETOOL="$2"; shift 2 ;;
        --runtime-file)   APPIMAGE_RUNTIME="$2"; shift 2 ;;
        --no-appimage)    APPIMAGE=0; shift ;;
        --no-tarball)     TARBALL=0; shift ;;
        --appdir-only)    APPDIR_ONLY=1; shift ;;
        --run-in-place)   RUN_IN_PLACE=1; shift ;;
        --steamdeck-args) STEAMDECK_ARGS="$2"; shift 2 ;;
        --no-smoke)       SMOKE=0; shift ;;
        --clean)          CLEAN=1; shift ;;
        -D*)              CONFIGURE_ARGS+=("$1"); shift ;;
        -h|--help)        usage; exit 0 ;;
        *) die "unknown argument: $1 (see --help)" ;;
    esac
done

[ "$(uname -s)" = "Linux" ] || die "package_linux.sh only runs on Linux"
[ -n "$TARGET" ] || [ -n "$BINARY" ] || die "--target or --binary is required"
[ -n "$APP_NAME" ] || APP_NAME="${TARGET:-$(basename "$BINARY")}"
EXE_NAME="$(basename "${BINARY:-$TARGET}")"

case "$APP_NAME" in
    ""|[.-]*|*/*|*\\*|*$'\n'*|*$'\r'*)
        die "--name must be a visible filename component without slashes" ;;
esac

ROOT="$(cd -- "$ROOT" && pwd -P)"
cd -- "$ROOT"
case "$STEAMDECK_ARGS" in
    *[\'\\\`\$]*) die "--steamdeck-args must be plain words (no quotes, backslashes, \$, or backticks)" ;;
esac
case "$BUILD_DIR" in
    /*) BUILD_PATH="$BUILD_DIR" ;;
    *)  BUILD_PATH="$ROOT/$BUILD_DIR" ;;
esac
mkdir -p -- "$ROOT/release-stage"
RELEASE_STAGE="$(cd -- "$ROOT/release-stage" && pwd -P)"

if [ -z "$VERSION" ]; then
    VERSION="$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo dev)"
fi
VERSION="$(printf '%s' "$VERSION" | tr '/ ' '--')"
[ -n "$VERSION" ] || VERSION="dev"

# ---- build or use the supplied executable --------------------------------
if [ -z "$BINARY" ]; then
    echo "==> configure ($BUILD_DIR)"
    cmake -S "$ROOT" -B "$BUILD_PATH" -DCMAKE_BUILD_TYPE=Release \
        "${CONFIGURE_ARGS[@]+"${CONFIGURE_ARGS[@]}"}"
    echo "==> build $TARGET"
    cmake --build "$BUILD_PATH" --target "$TARGET" --parallel "$JOBS"
    BINARY="$BUILD_PATH/$TARGET"
fi
case "$BINARY" in
    /*) ;;
    *)  BINARY="$ROOT/$BINARY" ;;
esac
[ -x "$BINARY" ] || die "executable not found or not executable: $BINARY"
EXE_NAME="$(basename "$BINARY")"

FOLDER="$APP_NAME-$VERSION-linux-$ARCH"
PAYLOAD_DIR="$RELEASE_STAGE/$FOLDER"
echo "==> stage $PAYLOAD_DIR"
rm -rf -- "$PAYLOAD_DIR"
mkdir -p -- "$PAYLOAD_DIR"
cp "$BINARY" "$PAYLOAD_DIR/$EXE_NAME"
chmod 755 "$PAYLOAD_DIR/$EXE_NAME"

copy_payload() {
    local spec="$1" src dst
    src="${spec%%=*}"
    if [ "$src" = "$spec" ]; then
        dst="$(basename "$src")"
    else
        dst="${spec#*=}"
    fi
    [ -n "$dst" ] || die "empty payload destination: $spec"
    case "$src" in
        /*) ;;
        *) src="$ROOT/$src" ;;
    esac
    [ -e "$src" ] || die "payload source not found: $src"
    mkdir -p -- "$(dirname "$PAYLOAD_DIR/$dst")"
    if [ -d "$src" ]; then
        cp -R -- "$src" "$PAYLOAD_DIR/$dst"
    else
        cp -- "$src" "$PAYLOAD_DIR/$dst"
    fi
    echo "    + $dst"
}
for p in "${PAYLOADS[@]+"${PAYLOADS[@]}"}"; do
    copy_payload "$p"
done
for d in "${PAYLOAD_DIRS[@]+"${PAYLOAD_DIRS[@]}"}"; do
    case "$d" in
        /*) ;;
        *) d="$ROOT/$d" ;;
    esac
    [ -d "$d" ] || die "payload directory not found: $d"
    cp -R -- "$d/." "$PAYLOAD_DIR/"
    echo "    + $d/ contents"
done

# Portable hygiene: symlinks and user/game data never enter a public artifact.
if find "$PAYLOAD_DIR" -type l | grep -q .; then
    die "payload contains a symlink (use regular files)"
fi
if find "$PAYLOAD_DIR" -type f | grep -Eiq '\.(gba|agb|sav|srm|apk)$'; then
    die "payload contains a ROM/save/APK extension; use the consumer's allowlist"
fi

cat > "$PAYLOAD_DIR/README.txt" <<TXT
$APP_NAME $VERSION — Linux ($ARCH)

Run ./$EXE_NAME from this directory. No ROM or BIOS is included: supply your
own legally dumped cartridge; the launch screen prompts for it and remembers
the pick beside this executable.

The BIOS is the clean-room build when the game ships one (see the game's
README). Settings, keybinds, and saves are written beside the executable;
extract to a writable directory.
TXT

if [ "$SMOKE" -eq 1 ]; then
    printf '==> startup smoke: '
    if ( cd "$PAYLOAD_DIR" && "./$EXE_NAME" --help >/dev/null 2>&1 ); then
        echo "OK"
    else
        die "the payload did not survive the --help startup smoke (use --no-smoke to skip)"
    fi
fi

if [ "$TARBALL" -eq 1 ]; then
    echo "==> tarball"
    ( cd "$RELEASE_STAGE" && tar -czf "$FOLDER.tar.gz" -- "$FOLDER" )
    echo "    release-stage/$FOLDER.tar.gz"
fi

if [ "$APPIMAGE" -eq 0 ] && [ "$APPDIR_ONLY" -eq 0 ]; then
    [ "$TARBALL" -eq 1 ] || die "nothing to do: both AppImage and tarball disabled"
    echo
    echo "Built release-stage/$FOLDER"
    exit 0
fi

# ---- AppDir + AppImage ----------------------------------------------------
[ -n "$ICON" ] || die "--icon is required for an AppImage/AppDir"
case "$ICON" in
    /*) ;;
    *) ICON="$ROOT/$ICON" ;;
esac
[ -f "$ICON" ] || die "icon not found: $ICON"
[ "$ARCH" = "x86_64" ] || die "the pinned appimagetool is x86_64; use --no-appimage for $ARCH"

ICON_NAME="$(printf '%s' "$APP_NAME" | tr '[:upper:]' '[:lower:]' | \
             tr -cs 'a-z0-9' '-')"
ICON_NAME="${ICON_NAME#-}"; ICON_NAME="${ICON_NAME%-}"
[ -n "$ICON_NAME" ] || ICON_NAME="app"
DESKTOP_NAME="$ICON_NAME.desktop"

APPDIR="$RELEASE_STAGE/$APP_NAME-$VERSION.AppDir"
echo "==> AppDir $APPDIR"
rm -rf -- "$APPDIR"
mkdir -p -- "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
    "$APPDIR/usr/share/icons/hicolor/512x512/apps"
cp -a "$PAYLOAD_DIR/." "$APPDIR/usr/bin/"
cp "$ICON" "$APPDIR/$ICON_NAME.png"
cp "$ICON" "$APPDIR/.DirIcon"
cp "$ICON" "$APPDIR/usr/share/icons/hicolor/512x512/apps/$ICON_NAME.png"

if [ -z "$DESKTOP" ]; then
    cat > "$APPDIR/$DESKTOP_NAME" <<DESKTOP
[Desktop Entry]
Type=Application
Name=$APP_NAME
Comment=$APP_NAME — gbarecomp
Exec=$EXE_NAME
Icon=$ICON_NAME
Terminal=false
Categories=Game;
StartupWMClass=$EXE_NAME
X-AppImage-Version=$VERSION
DESKTOP
else
    case "$DESKTOP" in
        /*) ;;
        *) DESKTOP="$ROOT/$DESKTOP" ;;
    esac
    [ -f "$DESKTOP" ] || die "desktop file not found: $DESKTOP"
    cp "$DESKTOP" "$APPDIR/$DESKTOP_NAME"
fi
cp "$APPDIR/$DESKTOP_NAME" "$APPDIR/usr/share/applications/"

APP_ID="$(sha256sum "$PAYLOAD_DIR/$EXE_NAME" | cut -c1-12)"
SLUG="$(printf '%s' "$APP_NAME" | tr '[:upper:]' '[:lower:]' | \
        tr -cs 'a-z0-9' '-')"
SLUG="${SLUG#-}"; SLUG="${SLUG%-}"
[ -n "$SLUG" ] || SLUG="app"

cat > "$APPDIR/AppRun" <<APPRUN
#!/bin/sh
# Generated by gbarecompx/packaging/package_linux.sh.
set -eu
if [ -z "\${APPDIR:-}" ]; then
    APPDIR=\$(CDPATH= cd -- "\$(dirname -- "\$0")" && pwd)
fi
APPRUN
if [ "$RUN_IN_PLACE" -eq 1 ]; then
    cat >> "$APPDIR/AppRun" <<APPRUN
STEAMDECK_ARGS='$STEAMDECK_ARGS'
if [ "\${SteamDeck:-0}" = "1" ] && [ -n "\$STEAMDECK_ARGS" ]; then
    set -- \$STEAMDECK_ARGS "\$@"
fi
cd "\$APPDIR/usr/bin"
exec "./$EXE_NAME" "\$@"
APPRUN
else
    cat >> "$APPDIR/AppRun" <<APPRUN
APP_ID='$APP_ID'
if [ -n "\${XDG_DATA_HOME:-}" ]; then
    DATA_ROOT="\$XDG_DATA_HOME/$SLUG"
elif [ -n "\${HOME:-}" ]; then
    DATA_ROOT="\$HOME/.local/share/$SLUG"
else
    DATA_ROOT="\${TMPDIR:-/tmp}/$SLUG-\$(id -u)"
fi
DEST="\$DATA_ROOT/app"
first_run=0
if [ ! -x "\$DEST/$EXE_NAME" ]; then first_run=1; fi
if [ "\$first_run" -eq 1 ] || \\
   [ "\$(cat "\$DEST/.appimage-id" 2>/dev/null || true)" != "\$APP_ID" ]; then
    mkdir -p "\$DEST"
    # Copy the new payload over the old one; player files stay.
    cp -a "\$APPDIR/usr/bin/." "\$DEST/"
    printf '%s\\n' "\$APP_ID" > "\$DEST/.appimage-id"
    if [ "\$first_run" -eq 1 ]; then
        echo "$APP_NAME: unpacked to \$DEST" >&2
    fi
fi
STEAMDECK_ARGS='$STEAMDECK_ARGS'
if [ "\${SteamDeck:-0}" = "1" ] && [ -n "\$STEAMDECK_ARGS" ]; then
    set -- \$STEAMDECK_ARGS "\$@"
fi
cd "\$DEST"
exec "\$DEST/$EXE_NAME" "\$@"
APPRUN
fi
chmod 755 "$APPDIR/AppRun"

if [ "$APPDIR_ONLY" -eq 1 ]; then
    echo "==> AppDir only: $APPDIR"
    echo "Built release-stage/$APP_NAME-$VERSION.AppDir"
    exit 0
fi

CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/gbarecomp"
if [ -z "$APPIMAGETOOL" ]; then
    APPIMAGETOOL="$CACHE/appimagetool-1.9.1-x86_64.AppImage"
    if [ ! -x "$APPIMAGETOOL" ] || \
       ! printf '%s  %s\n' "$APPIMAGETOOL_SHA256" "$APPIMAGETOOL" | \
         sha256sum -c - >/dev/null 2>&1; then
        mkdir -p "$CACHE"
        echo "==> fetch appimagetool 1.9.1"
        curl -fL --retry 3 -o "$APPIMAGETOOL.part" "$APPIMAGETOOL_URL"
        printf '%s  %s\n' "$APPIMAGETOOL_SHA256" "$APPIMAGETOOL.part" | \
            sha256sum -c -
        mv "$APPIMAGETOOL.part" "$APPIMAGETOOL"
        chmod 755 "$APPIMAGETOOL"
    fi
else
    case "$APPIMAGETOOL" in
        /*) ;;
        *) APPIMAGETOOL="$ROOT/$APPIMAGETOOL" ;;
    esac
fi
[ -x "$APPIMAGETOOL" ] || die "appimagetool not executable: $APPIMAGETOOL"

# appimagetool downloads a moving type2-runtime from the `continuous` tag when
# --runtime-file is omitted. Pin the runtime so release bytes cannot change
# under us.
if [ -z "$APPIMAGE_RUNTIME" ]; then
    APPIMAGE_RUNTIME="$CACHE/runtime-x86_64"
    if [ ! -f "$APPIMAGE_RUNTIME" ] || \
       ! printf '%s  %s\n' "$APPIMAGE_RUNTIME_SHA256" "$APPIMAGE_RUNTIME" | \
         sha256sum -c - >/dev/null 2>&1; then
        mkdir -p "$CACHE"
        echo "==> fetch AppImage type-2 runtime"
        curl -fL --retry 3 -o "$APPIMAGE_RUNTIME.part" "$APPIMAGE_RUNTIME_URL"
        printf '%s  %s\n' "$APPIMAGE_RUNTIME_SHA256" "$APPIMAGE_RUNTIME.part" | \
            sha256sum -c -
        mv "$APPIMAGE_RUNTIME.part" "$APPIMAGE_RUNTIME"
        chmod 644 "$APPIMAGE_RUNTIME"
    fi
else
    case "$APPIMAGE_RUNTIME" in
        /*) ;;
        *) APPIMAGE_RUNTIME="$ROOT/$APPIMAGE_RUNTIME" ;;
    esac
fi
[ -f "$APPIMAGE_RUNTIME" ] || die "AppImage runtime not found: $APPIMAGE_RUNTIME"

OUT="$RELEASE_STAGE/$APP_NAME-$VERSION-linux-$ARCH.AppImage"
EPOCH="$(git -C "$ROOT" log -1 --format=%ct 2>/dev/null || echo 0)"
echo "==> AppImage $OUT"
ARCH="$ARCH" APPIMAGE_EXTRACT_AND_RUN=1 SOURCE_DATE_EPOCH="$EPOCH" \
    "$APPIMAGETOOL" --no-appstream --runtime-file "$APPIMAGE_RUNTIME" \
    "$APPDIR" "$OUT"
[ -s "$OUT" ] || die "appimagetool produced no AppImage"

if [ "$CLEAN" -eq 1 ]; then
    rm -rf -- "$PAYLOAD_DIR" "$APPDIR"
fi

echo
echo "Built:"
[ "$TARBALL" -eq 1 ] && echo "  release-stage/$FOLDER.tar.gz"
echo "  release-stage/$APP_NAME-$VERSION-linux-$ARCH.AppImage"
