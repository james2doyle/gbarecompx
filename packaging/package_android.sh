#!/usr/bin/env bash
# package_android.sh — the scaffold-level Android (APK) build wrapper.
#
# The bash, all-platform counterpart of the retired
# platform/android/tools/build-apk.ps1. It drives a consumer game's
# android/ Gradle project — which applies the framework scaffold
# (gbarecomp-app.gradle) — through the assembleDebug/assembleRelease path,
# forwarding the scaffold's own Gradle properties, and prints the produced
# APKs (path, size, sha256).
#
# Deliberately NOT in scope (consumer policy, in the consumer's own
# wrapper): payload content decisions, device gates, the release allowlist
# stage, the private-material sweep, and signing-key management (release
# keys pass through as -P properties).
#
# Usage:
#   packaging/package_android.sh [--project DIR] [--abis LIST] [--jobs N]
#       [--private-rom FILE] [--private-bios FILE] [--property K=V]...
#       [--release] [--clean-repackage] [--freshness-input FILE]...
#       [--post-build CMD] [--install]
#
#   --project DIR      the consumer's android/ Gradle project (with its
#                      gradlew). Default: the android/ dir beside the
#                      framework checkout (../../android from this script).
#   --abis LIST        -PgbaAbis (default arm64-v8a; comma list; x86_64 for
#                      emulator-only iteration — each ABI is a full native
#                      compile of the corpus).
#   --jobs N           -PgbaNativeJobs (Gradle caps the native job pool).
#   --private-rom F    -PprivateRom: a PRIVATE test build embeds the ROM —
#                      never committed, never distributed. The path is
#                      resolved to absolute here (the Gradle property
#                      resolves relative to android/app, a footgun this
#                      wrapper removes).
#   --private-bios F   -PprivateBios, same resolution.
#   --property K=V     extra -P passthrough (keystore params, gbaGame
#                      overrides — repeatable).
#   --release          assembleRelease instead of assembleDebug.
#   --clean-repackage  remove app/build before Gradle: incremental
#                      repackaging can strand bytes of a prior build in the
#                      zip slack (observed: a 67 MB blob, then ~30 MB of
#                      stale entry data invisible to infolist()). Native
#                      objects under app/.cxx survive.
#   --freshness-input FILE   repeatable: fail if FILE is newer than the
#                      regen stamp (generated/.regen_stamp) — the stale-
#                      corpus preflight; the consumer's wrapper supplies
#                      its real input set.
#   --post-build CMD   run after the build, from the consumer repo root,
#                      with APK_PATHS (space-separated) in the environment
#                      — the hook for a content guard or device gate.
#   --install          adb install -r of the APK matching the attached
#                      device's ABI (ANDROID_SERIAL is honored by adb).
#
# Required environment: JAVA_HOME and ANDROID_HOME (the consumer's
# preflight owns version policy; this wrapper only checks presence).

set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_ROOT="$(cd "$SELF_DIR/.." && pwd)"

fail() { echo "package-android: FAIL: $*" >&2; exit 1; }
log()  { echo "package-android: $*"; }

PROJECT_DIR=""
ABIS="arm64-v8a"
JOBS=""
PRIVATE_ROM=""
PRIVATE_BIOS=""
RELEASE=0
CLEAN_REPACKAGE=0
FRESHNESS_INPUTS=()
POST_BUILD=""
INSTALL=0
EXTRA_PROPS=()

usage() { sed -n '2,40p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --project) PROJECT_DIR="$2"; shift 2 ;;
        --abis) ABIS="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --private-rom) PRIVATE_ROM="$2"; shift 2 ;;
        --private-bios) PRIVATE_BIOS="$2"; shift 2 ;;
        --property) EXTRA_PROPS+=("$2"); shift 2 ;;
        --release) RELEASE=1; shift ;;
        --clean-repackage) CLEAN_REPACKAGE=1; shift ;;
        --freshness-input) FRESHNESS_INPUTS+=("$2"); shift 2 ;;
        --post-build) POST_BUILD="$2"; shift 2 ;;
        --install) INSTALL=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) fail "unknown argument: $1 (see --help)" ;;
    esac
done

# ── resolve the consumer project ─────────────────────────────────────────
if [ -z "$PROJECT_DIR" ]; then
    # The conventional layout: the framework checkout sits beside the
    # consumer's android/ dir, both under the consumer repo root.
    PROJECT_DIR="$FRAMEWORK_ROOT/../android"
fi
PROJECT_DIR="$(cd "$PROJECT_DIR" 2>/dev/null && pwd)" ||
    fail "consumer android project not found (use --project)"
REPO_ROOT="$(cd "$PROJECT_DIR/.." && pwd)"
log "project: $PROJECT_DIR"
log "repo root: $REPO_ROOT"

[ -x "$PROJECT_DIR/gradlew" ] ||
    fail "no executable gradlew in $PROJECT_DIR — not a Gradle project?"

# ── environment: presence only; version policy is the consumer's ─────────
[ -n "${JAVA_HOME:-}" ] || fail "JAVA_HOME is not set"
[ -x "$JAVA_HOME/bin/java" ] || fail "JAVA_HOME/bin/java is missing"
[ -n "${ANDROID_HOME:-}" ] || fail "ANDROID_HOME is not set"
[ -d "$ANDROID_HOME" ] || fail "ANDROID_HOME ($ANDROID_HOME) is not a directory"

# ── corpus: an APK embeds the desktop-generated corpus; the Android build
#    cannot produce it (the recompiler is a desktop tool). Absent corpus or
#    absent stamp = an APK that is stale by construction.
GEN_DIR="$REPO_ROOT/generated"
STAMP="$GEN_DIR/.regen_stamp"
[ -f "$GEN_DIR/dispatch_table.cpp" ] ||
    fail "no generated/ corpus at $GEN_DIR — run the desktop regeneration step (tools/cycle.py) first"
[ -f "$STAMP" ] ||
    fail "generated/ has no regen stamp — run the desktop regeneration step (tools/cycle.py) first"

# Freshness vs the consumer's declared inputs: a build input newer than the
# stamp means the corpus predates the change (the stale-corpus class).
for f in "${FRESHNESS_INPUTS[@]:-}"; do
    [ -n "$f" ] || continue
    input="$REPO_ROOT/$f"
    [ -f "$input" ] || fail "freshness input not found: $input"
    if [ "$input" -nt "$STAMP" ]; then
        fail "corpus is stale: $f is newer than the regen stamp — " \
             "re-run the consumer's desktop regeneration (cycle.py)"
    fi
done
log "corpus present + fresh (regen stamp)"

# ── Gradle properties (the scaffold owns every parameter) ────────────────
PROPS=(-PgbaAbis="$ABIS")
[ -n "$JOBS" ] && PROPS+=("-PgbaNativeJobs=$JOBS")
abs_path() { cd "$(dirname "$1")" && pwd >/dev/null 2>&1 &&
    echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }
if [ -n "$PRIVATE_ROM" ]; then
    [ -f "$PRIVATE_ROM" ] || fail "private ROM not found: $PRIVATE_ROM"
    PROPS+=("-PprivateRom=$(abs_path "$PRIVATE_ROM")")
fi
if [ -n "$PRIVATE_BIOS" ]; then
    [ -f "$PRIVATE_BIOS" ] || fail "private BIOS not found: $PRIVATE_BIOS"
    PROPS+=("-PprivateBios=$(abs_path "$PRIVATE_BIOS")")
fi
for p in "${EXTRA_PROPS[@]:-}"; do
    [ -n "$p" ] || continue
    case "$p" in *=*) PROPS+=("-P$p") ;; *) fail "--property expects K=V" ;; esac
done

# versionName comes from the checkout (the scaffold's identity block
# otherwise falls back to its static "0.1.0", which makes every build look
# stale against a device's version readout). The consumer can override with
# --property gbaVersionName=...
if ! printf '%s\n' "${PROPS[@]:-}" | grep -q "gbaVersionName="; then
    if VERSION="$(git -C "$REPO_ROOT" describe --tags 2>/dev/null)"; then
        PROPS+=("-PgbaVersionName=${VERSION#v}")
        log "versionName: ${VERSION#v} (from the checkout; override with --property gbaVersionName=...)"
    else
        log "versionName: not a git checkout — the scaffold's identity block applies"
    fi
fi

VARIANT="debug"
TASK=":app:assembleDebug"
if [ "$RELEASE" -eq 1 ]; then
    VARIANT="release"
    TASK=":app:assembleRelease"
fi

# ── clean repackage (the zip-slack junk class) ───────────────────────────
if [ "$CLEAN_REPACKAGE" -eq 1 ]; then
    rm -rf "$PROJECT_DIR/app/build"
    log "clean repackage: app/build removed (app/.cxx native objects survive)"
fi

# ── build ─────────────────────────────────────────────────────────────────
export JAVA_HOME
log "gradle: $TASK (abis=$ABIS)"
( cd "$PROJECT_DIR" && ./gradlew "$TASK" "${PROPS[@]}" )

# ── discover + report ─────────────────────────────────────────────────────
OUT_DIR="$PROJECT_DIR/app/build/outputs/apk/$VARIANT"
shopt -s nullglob
APKS=("$OUT_DIR"/*.apk)
shopt -u nullglob
[ ${#APKS[@]} -gt 0 ] || fail "gradle reported success but no APK under $OUT_DIR"

sha256_of() { if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
for apk in "${APKS[@]}"; do
    log "apk: $apk ($(du -h "$apk" | cut -f1), sha256 $(sha256_of "$apk"))"
done

# ── the consumer's hook (content guard / device gate) ────────────────────
if [ -n "$POST_BUILD" ]; then
    ( cd "$REPO_ROOT" && APK_PATHS="${APKS[*]}" bash -c "$POST_BUILD" ) ||
        fail "post-build hook failed"
fi

# ── install (optional): the APK matching the attached device's ABI ───────
if [ "$INSTALL" -eq 1 ]; then
    command -v adb >/dev/null 2>&1 || fail "--install needs adb on PATH"
    DEVICE_ABI="$(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
    log "device ABI: ${DEVICE_ABI:-unknown}"
    PICKED=""
    for apk in "${APKS[@]}"; do
        case "$(basename "$apk")" in
            *"$DEVICE_ABI"*) PICKED="$apk"; break ;;
        esac
    done
    [ -n "$PICKED" ] || PICKED="${APKS[0]}"
    log "install: adb install -r $PICKED"
    adb install -r "$PICKED"
fi

log "done"
