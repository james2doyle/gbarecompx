#!/usr/bin/env bash
# Packaging script checks: syntax + the current Linux staging/AppDir contract.
# Runs on Linux (full) and macOS (syntax + macOS help); no PyYAML, Flatpak, or
# appimagetool download is involved.
set -euo pipefail

PACKAGING_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
CC_BIN="${CC:-cc}"

for script in \
    "$PACKAGING_ROOT/package_linux.sh" \
    "$PACKAGING_ROOT/package_macos.sh" \
    "$PACKAGING_ROOT/package_android.sh"; do
    bash -n "$script"
done

"$PACKAGING_ROOT/package_macos.sh" --help >/dev/null
"$PACKAGING_ROOT/package_linux.sh" --help >/dev/null
"$PACKAGING_ROOT/package_android.sh" --help >/dev/null
test -f "$PACKAGING_ROOT/package_release.ps1"

if [ "$(uname -s)" != "Linux" ]; then
    echo "packaging script checks passed (non-Linux: syntax/help only)"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf -- "$TMP"' EXIT
SOURCE="$TMP/game tree"
mkdir -p -- "$SOURCE/payload/nested"
printf 'payload data\n' > "$SOURCE/payload/data.txt"
printf 'nested data\n' > "$SOURCE/payload/nested/inner.txt"
cat > "$SOURCE/tiny.c" <<'C'
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc > 1) {
        printf("usage: tiny [--help]\n");
    }
    return 0;
}
C
"$CC_BIN" -O2 -o "$SOURCE/tiny" "$SOURCE/tiny.c"
chmod 755 "$SOURCE/tiny"

# A 1x1 PNG; staging does not run appimagetool, so the icon only needs to be
# a regular PNG file.
printf '%s' 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAFgwJ/lYQ5WQAAAABJRU5ErkJggg==' |
    base64 -d > "$SOURCE/icon.png"

"$PACKAGING_ROOT/package_linux.sh" \
    --binary "$SOURCE/tiny" --name Tiny --version v0 --root "$SOURCE" \
    --payload "$SOURCE/payload" --icon "$SOURCE/icon.png" \
    --appdir-only --no-tarball --no-smoke >/dev/null

APPDIR="$SOURCE/release-stage/Tiny-v0.AppDir"
test -x "$APPDIR/usr/bin/tiny"
test -f "$APPDIR/usr/bin/payload/data.txt"
test -f "$APPDIR/usr/bin/payload/nested/inner.txt"
test -f "$APPDIR/tiny.desktop"
test -f "$APPDIR/.DirIcon"
grep -q 'XDG_DATA_HOME' "$APPDIR/AppRun"
grep -q 'cp -a "$APPDIR/usr/bin/." "$DEST/"' "$APPDIR/AppRun"
grep -q 'exec "$DEST/tiny" "$@"' "$APPDIR/AppRun"
if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate "$APPDIR/tiny.desktop"
fi

# --run-in-place keeps the executable on the mount (for XDG-aware runtimes).
"$PACKAGING_ROOT/package_linux.sh" \
    --binary "$SOURCE/tiny" --name TinyInPlace --version v0 --root "$SOURCE" \
    --icon "$SOURCE/icon.png" --appdir-only --no-tarball --no-smoke \
    --run-in-place --steamdeck-args "--fullscreen=1" >/dev/null
INPLACE="$SOURCE/release-stage/TinyInPlace-v0.AppDir/AppRun"
grep -q 'cd "$APPDIR/usr/bin"' "$INPLACE"
grep -q 'exec "./tiny" "$@"' "$INPLACE"
grep -q "STEAMDECK_ARGS='--fullscreen=1'" "$INPLACE"

# Unsafe distribution names are rejected before staging/removal.
if "$PACKAGING_ROOT/package_linux.sh" \
        --binary "$SOURCE/tiny" --name '../outside' --root "$SOURCE" \
        --no-appimage --no-smoke >/dev/null 2>&1; then
    echo "package_linux.sh accepted an unsafe --name" >&2
    exit 1
fi

# Portable-only path: flat folder + tarball, no AppDir.
"$PACKAGING_ROOT/package_linux.sh" \
    --binary "$SOURCE/tiny" --name TinyPortable --version v1 --root "$SOURCE" \
    --payload "$SOURCE/payload/data.txt" --no-appimage --no-smoke >/dev/null
ARCH="$(uname -m)"
test -x "$SOURCE/release-stage/TinyPortable-v1-linux-$ARCH/tiny"
test -f "$SOURCE/release-stage/TinyPortable-v1-linux-$ARCH/data.txt"
test -f "$SOURCE/release-stage/TinyPortable-v1-linux-$ARCH.tar.gz"

# --payload-dir copies directory contents into the payload root.
"$PACKAGING_ROOT/package_linux.sh" \
    --binary "$SOURCE/tiny" --name TinyDir --version v2 --root "$SOURCE" \
    --payload-dir "$SOURCE/payload" --no-appimage --no-smoke >/dev/null
test -f "$SOURCE/release-stage/TinyDir-v2-linux-$ARCH/data.txt"
test -f "$SOURCE/release-stage/TinyDir-v2-linux-$ARCH/nested/inner.txt"

echo "packaging script checks passed"
