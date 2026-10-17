#!/usr/bin/env python3
"""Guard: the menu's save-state rows must be wired into the PRODUCT loop.

The menu class (host_menu.cpp) appends the State rows — Slot / Save state /
Load state — and queues ops that the host loop drains into ev.save_slot /
ev.load_slot, the same vocabulary the assist scripts write, so exactly one
executor runs at one dispatch boundary. The imgui-era menu had this exact
feature (system.state_slot + the F1..F9 hotkeys) and it silently vanished
when the presentation was replaced — the same loss class
check_menu_view_wiring.py exists for. The headless suites pin the menu
class itself (rows, labels, actions, the drain helper); this check pins
the product-side wiring in runtime.cpp, where no headless test reaches,
and every runtime.cpp anchor must sit OUTSIDE the dead GBARECOMP_RUNTIME_UI
gate (the product never defines it; a line inside that gate compiles out).

Self-test (--self-test): synthetic gated/ungated/missing snippets assert
both verdicts, so the checker cannot rot into a no-op.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_menu_view_wiring as view_guard  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
RUNTIME_CPP = REPO / "src" / "runtime" / "runtime.cpp"
HOST_MENU_H_PATH = REPO / "src" / "runtime" / "host_menu.h"
HOST_MENU_CPP = REPO / "src" / "runtime" / "host_menu.cpp"
MENU_SETTINGS_H_PATH = REPO / "src" / "runtime" / "menu_settings.h"

# runtime.cpp anchors: each (needle, why). Must be present AND ungated.
RUNTIME_UNGATED = [
    ("host_menu.set_state_slot(",
     "the menu's Slot row is never seeded from the persisted setting"),
    ("drain_menu_state_ops(host_menu, ev)",
     "menu-queued state ops never reach the executor — the rows would "
     "close the menu and do nothing"),
    ('"State saved"', "the save confirmation toast wording"),
    ('"State loaded"', "the load confirmation toast wording"),
    ("ms.state_slot = ctx->host->state_slot();",
     "the chosen slot is never persisted to config.ini"),
]

# Presence anchors elsewhere (no conditional compilation in these files).
HOST_MENU_H_ANCHORS = [
    ("take_pending_state_ops", "the queue/take API"),
    ("set_state_slot", "the slot seeding API"),
    ("drain_menu_state_ops", "the events drain helper"),
]
HOST_MENU_CPP_ANCHORS = [
    ('"Slot"', "the Slot choice row"),
    ('"Save state"', "the save action row"),
    ('"Load state"', "the load action row"),
    ("pending_save_slot_ = state_slot_",
     "the save action no longer queues its slot op"),
    ("pending_load_slot_ = state_slot_",
     "the load action no longer queues its slot op"),
]
MENU_SETTINGS_H_ANCHORS = [
    ('ci_key(key, "state_slot")', "the state_slot config key no longer parses"),
    ('f << "state_slot = "', "the state_slot config key is no longer saved"),
]


def check(source: str) -> tuple[bool, str]:
    """The drain call must exist, ungated, in the product loop."""
    hits = [n for n, line in enumerate(source.splitlines(), 1)
            if "drain_menu_state_ops(host_menu, ev)" in line]
    if not hits:
        return False, ("drain_menu_state_ops(host_menu, ev) call not found "
                       "in runtime.cpp — the State rows queue ops nothing "
                       "executes")
    active = view_guard._active_gates(source)
    for n in hits:
        if active[n]:
            return True, f"state drain wired ungated (line {n})"
    return False, (f"drain_menu_state_ops is gated behind "
                   f"{view_guard.GATE_MACRO} (line(s) {hits}) — it compiles "
                   "out of every product build")


def check_all() -> tuple[bool, str]:
    failures: list[str] = []

    runtime_src = RUNTIME_CPP.read_text()
    active = view_guard._active_gates(runtime_src)
    lines = runtime_src.splitlines()
    for needle, why in RUNTIME_UNGATED:
        hits = [n for n, line in enumerate(lines, 1) if needle in line]
        if not hits:
            failures.append(f"runtime.cpp: missing {needle!r} — {why}")
            continue
        gated = [n for n in hits if not active[n]]
        if len(gated) == len(hits):
            failures.append(
                f"runtime.cpp: {needle!r} only exists behind "
                f"{view_guard.GATE_MACRO} (line(s) {hits}) — {why}")

    for path, anchors in [
        (HOST_MENU_H_PATH, HOST_MENU_H_ANCHORS),
        (HOST_MENU_CPP, HOST_MENU_CPP_ANCHORS),
        (MENU_SETTINGS_H_PATH, MENU_SETTINGS_H_ANCHORS),
    ]:
        src = path.read_text()
        for needle, why in anchors:
            if needle not in src:
                failures.append(f"{path.name}: missing {needle!r} — {why}")

    if failures:
        return False, "; ".join(failures)
    ok, msg = check(runtime_src)
    if not ok:
        return False, msg
    return True, ("State rows wired: seed, drain, toasts, persistence, "
                  "menu class, settings keys — all ungated")


SELF_GATED = """void loop() {
#if defined(GBARECOMP_RUNTIME_UI)
    drain_menu_state_ops(host_menu, ev);
#endif
}
"""
SELF_UNGATED = """void loop() {
    drain_menu_state_ops(host_menu, ev);
}
"""
SELF_MISSING = """void loop() {
    host_menu.frame(backend, ev, pointer);
}
"""


def self_test() -> int:
    failures = 0
    ok, msg = check(SELF_GATED)
    if ok:
        print(f"FAIL self-test: gated snippet passed ({msg})")
        failures += 1
    ok, msg = check(SELF_UNGATED)
    if not ok:
        print(f"FAIL self-test: ungated snippet failed ({msg})")
        failures += 1
    ok, msg = check(SELF_MISSING)
    if ok:
        print(f"FAIL self-test: missing-drain snippet passed ({msg})")
        failures += 1
    if failures:
        return 1
    print("menu-state-wiring self-test: OK")
    return 0


def main() -> int:
    if "--self-test" in sys.argv[1:]:
        return self_test()
    ok, msg = check_all()
    print(f"{'PASS' if ok else 'FAIL'} menu-state-wiring: {msg}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
