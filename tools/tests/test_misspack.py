#!/usr/bin/env python3
"""Framework-side tests for misspack's pure helpers (stdlib unittest).

Covers misspack.prologue_scan: the thumb push-with-lr backscan on a
synthetic ROM — the function-start evidence behind a heal proposal.

Requires capstone (misspack dependency): `pip install capstone==5.0.7` —
the CI guard job installs that same pin.
"""
import pathlib
import struct
import sys
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

try:
    import misspack  # noqa: E402
except ModuleNotFoundError as exc:
    if exc.name == "capstone":
        raise SystemExit(
            "test_misspack needs capstone (misspack dependency): "
            "`pip install capstone==5.0.7`") from None
    raise


class PrologueScanTest(unittest.TestCase):
    def test_thumb_push_backscan_finds_prologue(self):
        rom = bytearray(0x400)
        struct.pack_into("<H", rom, 0x40, 0xB510)  # push {r4, lr}
        hits = misspack.prologue_scan(bytes(rom), misspack.ROM_BASE + 0x60,
                                      "thumb", limit=0x100)
        self.assertEqual(hits[0][0], misspack.ROM_BASE + 0x40)
        self.assertTrue(hits[0][1].startswith("push"))

    def test_no_prologue_returns_empty(self):
        hits = misspack.prologue_scan(bytes(0x400), misspack.ROM_BASE + 0x60,
                                      "thumb", limit=0x100)
        self.assertEqual(hits, [])


if __name__ == "__main__":
    unittest.main()
