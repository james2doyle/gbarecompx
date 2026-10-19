#!/usr/bin/env bash
# package_macos.sh — package a gbarecomp game for macOS.
#
# Current pipeline (raylib is statically linked): stage the executable plus
# consumer-declared payload into a flat, portable directory and tar it. No
# dylib bundling or SDL shims are needed.
#
#   gbarecompx/packaging/package_macos.sh \
#       --target FFTARecomp --name FFTARecomp --version v1.7 \
#       --payload game.toml --payload bios --payload mods --payload assets
#
# Output:
#   release-stage/<name>-<version>-macos-<arch>/
#   release-stage/<name>-<version>-macos-<arch>.tar.gz
#
# A flat directory is deliberate: a .app bundle validates everything under
# Contents/MacOS as signed code, but a gbarecomp game resolves assets beside
# the executable, so assets placed there cannot be ad-hoc signed. The flat
# directory runs signed or unsigned and keeps the runtime's sidecar layout.
#
# Policy stays in the consumer: ROM/BIOS content pins, signing/notarization,
# upload, and the strict coverage smoke. This script applies the portable
# hygiene rules (no symlinks, no ROM/save/APK extensions) and can run the
# executable's --help startup smoke.
set -euo pipefail

TARGET=""; APP_NAME=""; VERSION=""; ROOT="$(pwd)"; BUILD_DIR="build-release"
BINARY=""; ARCH="$(uname -m)"
PAYLOADS=(); PAYLOAD_DIRS=(); TARBALL=1; CLEAN=0; SMOKE=1
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
CONFIGURE_ARGS=()

die() { echo "package_macos: $*" >&2; exit 1; }

usage() {
    sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
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
  --arch <arch>          architecture label (default: uname -m)
  --jobs <n>             build parallelism (default: hw.ncpu)
  --no-tarball           keep the folder only
  --no-smoke             skip the --help startup smoke
  --clean                remove the portable folder after packaging
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
        --arch)           ARCH="$2"; shift 2 ;;
        --jobs)           JOBS="$2"; shift 2 ;;
        --no-tarball)     TARBALL=0; shift ;;
        --no-smoke)       SMOKE=0; shift ;;
        --clean)          CLEAN=1; shift ;;
        -D*)              CONFIGURE_ARGS+=("$1"); shift ;;
        -h|--help)        usage; exit 0 ;;
        *) die "unknown argument: $1 (see --help)" ;;
    esac
done

[ "$(uname -s)" = "Darwin" ] || die "package_macos.sh only runs on macOS"
[ -n "$TARGET" ] || [ -n "$BINARY" ] || die "--target or --binary is required"
[ -n "$APP_NAME" ] || APP_NAME="${TARGET:-$(basename "$BINARY")}"
case "$APP_NAME" in
    ""|[.-]*|*/*|*\\*|*$'\n'*|*$'\r'*)
        die "--name must be a visible filename component without slashes" ;;
esac

ROOT="$(cd -- "$ROOT" && pwd -P)"
cd -- "$ROOT"
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

FOLDER="$APP_NAME-$VERSION-macos-$ARCH"
OUT="$RELEASE_STAGE/$FOLDER"
echo "==> stage $OUT"
rm -rf -- "$OUT"
mkdir -p -- "$OUT"
cp "$BINARY" "$OUT/$EXE_NAME"
chmod 755 "$OUT/$EXE_NAME"

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
    mkdir -p -- "$(dirname "$OUT/$dst")"
    if [ -d "$src" ]; then
        cp -R -- "$src" "$OUT/$dst"
    else
        cp -- "$src" "$OUT/$dst"
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
    cp -R -- "$d/." "$OUT/"
    echo "    + $d/ contents"
done

if find "$OUT" -type l | grep -q .; then
    die "payload contains a symlink (use regular files)"
fi
if find "$OUT" -type f | grep -Eiq '\.(gba|agb|sav|srm|apk)$'; then
    die "payload contains a ROM/save/APK extension; use the consumer's allowlist"
fi

cat > "$OUT/README.txt" <<TXT
$APP_NAME $VERSION — macOS ($ARCH)

Run ./$EXE_NAME from this directory. No ROM or BIOS is included: supply your
own legally dumped cartridge; the launch screen prompts for it and remembers
the pick beside this executable.

If macOS quarantines the download:
    xattr -dr com.apple.quarantine "$FOLDER"
TXT

# raylib is linked statically; only system frameworks/libraries should remain.
if command -v otool >/dev/null 2>&1; then
    LEFT="$(otool -L "$OUT/$EXE_NAME" | tail -n +2 | awk '{print $1}' |
            grep -vE '^(/usr/lib|/System|@executable_path)' || true)"
    if [ -n "$LEFT" ]; then
        die "non-system dependencies remain (the consumer must stage them):
$LEFT"
    fi
fi

if [ "$SMOKE" -eq 1 ]; then
    printf '==> startup smoke: '
    if ( cd "$OUT" && "./$EXE_NAME" --help >/dev/null 2>&1 ); then
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
if [ "$CLEAN" -eq 1 ]; then
    rm -rf -- "$OUT"
fi

echo
echo "Built:"
[ "$TARBALL" -eq 1 ] && echo "  release-stage/$FOLDER.tar.gz"
echo "  release-stage/$FOLDER"
