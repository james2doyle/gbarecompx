#!/usr/bin/env python3
"""check_launch_screen_wiring.py — source-shape guards for the pre-boot
launcher wiring (framework invariants).

Checks (device-free source-shape checks, all over fork content):

1. mobile_platform.cpp must append --no-launcher (the mobile posture: the
   setup activity is the Android pre-boot surface).
2. mobile_platform.cpp must consume debug-args.txt one-shot (a forced flag
   otherwise persists across relaunches).
3. run_game must tolerate --no-launcher (mobile boots die on "unknown
   argument" otherwise — 2026-10-16 device finding: PLAY appeared dead
   because the game activity exited instantly on the unknown flag).
4. GbaSetupActivity retired-view hygiene: the BIOS card was dropped in the
   launch-screen redesign; every code path touching the retired views must
   null-guard them (an unguarded deref in setBusy crashed the SAF result
   delivery on device, 2026-10-16).

The game-side half of the skip contract (main.cpp must classify the MUTATED
args vector, not the raw argv) is guarded game-side in
tools/android_static_check.py — src/main.cpp is game content.

Usage (any cwd — all inputs are fork content):
  python3 tools/check_launch_screen_wiring.py [--self-test]

--self-test runs the checkers against synthetic broken/fixed fixtures and
asserts both verdicts, so the checker cannot rot into a no-op.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def check_skip_wiring(mobile: str, runtime: str) -> tuple[bool, list[str]]:
    failures = []
    if '"--no-launcher"' not in mobile:
        failures.append(
            "mobile_platform.cpp must append --no-launcher (the mobile "
            "posture: the setup activity is the Android pre-boot surface)")
    if 'std::remove("debug-args.txt")' not in mobile:
        failures.append(
            "mobile_platform.cpp must consume debug-args.txt one-shot (a "
            "forced flag otherwise persists across relaunches)")
    if 's == "--no-launcher"' not in runtime:
        failures.append(
            "run_game parser must tolerate --no-launcher (mobile boots die "
            "on unknown argument otherwise)")
    return (not failures, failures)


def check_retired_view_hygiene(setup_java: str) -> tuple[bool, list[str]]:
    failures = []
    # The literal unguarded form must not appear outside the null-guard:
    # count unguarded occurrences with a negative lookbehind.
    unguarded = re.findall(
        r"(?<!if \(biosButton != null\) )biosButton\.setEnabled", setup_java)
    if unguarded:
        failures.append(
            "setBusy must not dereference the retired biosButton unguarded "
            "(an unguarded deref crashed the SAF result delivery on device, "
            "2026-10-16)")
    if "if (biosButton != null) biosButton.setEnabled(!busy)" not in setup_java:
        failures.append("setBusy must null-guard the retired biosButton")
    return (not failures, failures)


# ---- self-test fixtures -----------------------------------------------------

GOOD_MOBILE = 'args.emplace_back("--no-launcher");\n...std::remove("debug-args.txt");\n'
BAD_MOBILE = 'args.emplace_back("--rom");\n'
GOOD_RUNTIME = 'if (s == "--no-launcher") {\n            continue;\n        }\n'
BAD_RUNTIME = 'if (s == "--no-bios-hle") {\n            continue;\n        }\n'
GOOD_SETUP = 'if (biosButton != null) biosButton.setEnabled(!busy);\n'
BAD_SETUP = 'biosButton.setEnabled(!busy);\n'


def self_test() -> int:
    failures = 0

    ok, msgs = check_skip_wiring(GOOD_MOBILE, GOOD_RUNTIME)
    if not ok:
        print("FAIL self-test: good fixtures rejected:", msgs)
        failures += 1
    ok, _ = check_skip_wiring(BAD_MOBILE, GOOD_RUNTIME)
    if ok:
        print("FAIL self-test: missing --no-launcher append passed")
        failures += 1
    ok, _ = check_skip_wiring(GOOD_MOBILE, BAD_RUNTIME)
    if ok:
        print("FAIL self-test: missing parser tolerance passed")
        failures += 1

    ok, _ = check_retired_view_hygiene(GOOD_SETUP)
    if not ok:
        print("FAIL self-test: guarded setBusy rejected")
        failures += 1
    ok, _ = check_retired_view_hygiene(BAD_SETUP)
    if ok:
        print("FAIL self-test: unguarded setBusy passed")
        failures += 1

    if failures:
        return 1
    print("launch-screen-wiring self-test: all fixture verdicts correct")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--self-test", action="store_true",
                    help="run the checker against synthetic fixtures")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    # Real run: all inputs are fork content, so any cwd works.
    mobile = (REPO / "src" / "runtime" / "mobile_platform.cpp").read_text()
    runtime = (REPO / "src" / "runtime" / "runtime.cpp").read_text()
    setup_java = (REPO / "platform" / "android" / "java" / "org" / "gbarecomp"
                  / "GbaSetupActivity.java").read_text()

    failures = 0
    ok, msgs = check_skip_wiring(mobile, runtime)
    for m in msgs:
        print("FAIL launch-screen wiring: " + m)
    if not ok:
        failures += 1
    else:
        print("PASS launch-screen skip wiring (--no-launcher append, "
              "one-shot debug args, parser tolerance)")

    ok, msgs = check_retired_view_hygiene(setup_java)
    for m in msgs:
        print("FAIL retired-view hygiene: " + m)
    if not ok:
        failures += 1
    else:
        print("PASS setup screen retired-view hygiene (no NPE)")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
