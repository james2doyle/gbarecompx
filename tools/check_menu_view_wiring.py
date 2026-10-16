#!/usr/bin/env python3
"""Guard: the settings menu's View row must be wired OUTSIDE the dead
recomp-ui gate.

The View row (Native / Adaptive / Fixed) is framework content: the menu
class appends it when the host calls `set_view_mode_endpoint(...)`. That
call lived inside `#if defined(GBARECOMP_RUNTIME_UI)` in runtime.cpp — a
recomp-ui-era guard that no build defines since recomp-ui retired — so
the row silently vanished from every product build while the menu-class
unit tests (which set the endpoint themselves) stayed green. This check
pins the product wiring: the endpoint call must exist, must NOT sit
behind the GBARECOMP_RUNTIME_UI conditional, and must pass the real
availability expression (the widescreen resize opt-in), not a stub.

Self-test (--self-test): runs the scanner against synthetic gated and
ungated snippets and asserts both verdicts, so the checker cannot rot
into a no-op.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RUNTIME_CPP = REPO / "src" / "runtime" / "runtime.cpp"

GATE_MACRO = "GBARECOMP_RUNTIME_UI"
ENDPOINT = "set_view_mode_endpoint("
ENDPOINT_ARGS_SNAPSHOT = (
    # The availability argument as the product passes it — a stub call with
    # available=true would satisfy presence but not the contract.
    "opts.resize_driven_view && opts.max_resize_view_width > 240"
)


def _gate_cond(keyword: str, rest: str) -> bool:
    """Evaluate one conditional's condition AS THE PRODUCT BUILD DOES.

    The product never defines GBARECOMP_RUNTIME_UI (recomp-ui is retired;
    GBARECOMP_RUNTIME_UI_ROOT is pinned empty), so `defined(GATE)` is false
    and `!defined(GATE)` is true. Unrelated conditions are assumed taken —
    the check only needs to know whether the endpoint sits inside THIS
    macro's dead branch, and assuming taken errs toward flagging.
    """
    if keyword == "ifdef":
        return False  # defined(X): false in the product
    if keyword == "ifndef":
        return True  # !defined(X): true in the product
    if re.search(rf"!\s*defined\(\s*{GATE_MACRO}\s*\)", rest):
        return True
    if re.search(rf"\bdefined\(\s*{GATE_MACRO}\s*\)", rest):
        return False
    return True


def _active_gates(source: str) -> dict[int, bool]:
    """Line number -> is the line compiled in the product configuration.

    Tracks #if/#ifdef/#ifndef/#elif/#else/#endif nesting with the product
    semantics above (a level is active when its enclosing chain is active
    and its current branch is taken).
    """
    # Per level: (enclosing_active, any_branch_taken, current_branch_active)
    stack: list[list[bool]] = []  # [enclosing_active, taken, active]
    active: dict[int, bool] = {}

    def cur() -> bool:
        return all(level[0] and level[2] for level in stack)

    for lineno, raw in enumerate(source.splitlines(), 1):
        stripped = raw.strip()
        if stripped.startswith("#"):
            m = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)",
                         stripped)
            if m:
                kw, rest = m.group(1), m.group(2)
                if kw in ("if", "ifdef", "ifndef"):
                    cond = _gate_cond(kw, rest)
                    stack.append([cur(), cond, cond])
                elif kw == "elif":
                    if stack:
                        level = stack[-1]
                        if level[1]:  # an earlier branch was taken
                            level[2] = False
                        else:
                            level[2] = _gate_cond("if", rest)
                            level[1] = level[2]
                elif kw == "else":
                    if stack:
                        level = stack[-1]
                        level[2] = not level[1]
                        level[1] = True
                elif kw == "endif":
                    if stack:
                        stack.pop()
        active[lineno] = cur()
    return active


def check(source: str) -> tuple[bool, str]:
    hits = [n for n, line in enumerate(source.splitlines(), 1)
            if ENDPOINT in line]
    if not hits:
        return False, "set_view_mode_endpoint call not found in runtime.cpp"
    active = _active_gates(source)
    lines = source.splitlines()
    ok_sites = []
    for n in hits:
        if active[n]:
            ok_sites.append(n)
    if not ok_sites:
        return False, (
            "set_view_mode_endpoint is gated behind "
            f"{GATE_MACRO} (line(s) {hits}) — the View row compiles out")
    # The real site must pass the widescreen availability expression.
    # Collect the call's text through its balanced parens.
    for n in ok_sites:
        call = "\n".join(lines[n - 1:])
        depth = 0
        end = len(call)
        for i, ch in enumerate(call):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    end = i
                    break
        args = call[:end]
        if ENDPOINT_ARGS_SNAPSHOT not in args:
            return False, (
                f"set_view_mode_endpoint (line {n}) lost the widescreen "
                "availability expression — the row would show in the wrong "
                "configurations")
        return True, f"View row wired ungated (line {n}), real availability arg"
    return True, "ok"


SELF_GATED = """#include "x.h"
void f(HostMenu& m, Opts& opts) {
#if defined(GBARECOMP_RUNTIME_UI)
    m.set_view_mode_endpoint(g, s, &ctx,
        opts.resize_driven_view && opts.max_resize_view_width > 240);
#endif
}
"""

SELF_UNGATED = """#include "x.h"
void f(HostMenu& m, Opts& opts) {
    m.set_view_mode_endpoint(g, s, &ctx,
        opts.resize_driven_view && opts.max_resize_view_width > 240);
}
"""

SELF_STUB = """#include "x.h"
void f(HostMenu& m) {
    m.set_view_mode_endpoint(g, s, &ctx, true);
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
    ok, msg = check(SELF_STUB)
    if ok:
        print(f"FAIL self-test: stub availability passed ({msg})")
        failures += 1
    if failures:
        return 1
    print("menu-view-wiring self-test: OK")
    return 0


def main() -> int:
    if "--self-test" in sys.argv[1:]:
        return self_test()
    ok, msg = check(RUNTIME_CPP.read_text())
    print(f"{'PASS' if ok else 'FAIL'} menu-view-wiring: {msg}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
