#!/usr/bin/env bash
# clang-tidy tier for the owned runtime sources: src/runtime first-level
# .cpp files, EXCLUDING generated_bios/ (recompiler output — never
# hand-edited, same rule as the game's generated/) and vendor/ (raygui).
#
# Needs compile_commands.json: both CMakeLists export it
# (CMAKE_EXPORT_COMPILE_COMMANDS), so a configure of either build — the
# game repo's build/ (../build from here) or a fork-standalone build/ —
# provides it. Without a db this SKIPS (exit 0) so CI runners without a
# configured tree stay green; without clang-tidy it SKIPS too. Local runs
# are the strict tier.
set -uo pipefail
cd "$(dirname "$0")/.."

DB=""
for c in ../build/compile_commands.json build/compile_commands.json; do
    if [ -f "$c" ]; then DB="$c"; break; fi
done
if [ -z "$DB" ]; then
    echo "SKIP check_clang_tidy: no compile_commands.json (configure the "
    echo "  game build or a fork-standalone build; CMake exports it)"
    exit 0
fi
if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "SKIP check_clang_tidy: clang-tidy not installed"
    exit 0
fi

mapfile -t files < <(find src/runtime -maxdepth 1 -name '*.cpp' | sort)
if [ "${#files[@]}" -eq 0 ]; then
    echo "FAIL check_clang_tidy: no runtime TUs found"
    exit 2
fi

out="$(mktemp)"
trap 'rm -f "$out" "$out.filtered"' EXIT
printf '%s\0' "${files[@]}" |
    xargs -0 -P "$(nproc)" -n1 clang-tidy -p "$DB" --quiet >>"$out" 2>&1

# generated_bios headers can surface through includes; recompiler output is
# out of scope by the same rule as the tree itself. vendor/ (raygui.h) is
# upstream code we do not hand-edit — same exclusion.
grep -v -e "generated_bios" -e "src/vendor" "$out" >"$out.filtered" || true
warns=$(grep -c "warning:" "$out.filtered")
warns=${warns:-0}
if [ "$warns" -ne 0 ]; then
    grep "warning:" "$out.filtered" | head -50
    echo "FAIL check_clang_tidy: $warns warning(s) across ${#files[@]} runtime TUs"
    exit 1
fi
echo "PASS check_clang_tidy: 0 warnings across ${#files[@]} runtime TUs"
