#!/usr/bin/env python3
"""Framework-side tests for cache_harvest (stdlib unittest).

Covers the recomp_cache unit-file name parsing and the game.toml address
set reader (the harvest feeds the playtest → healing loop).
"""
import pathlib
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import cache_harvest  # noqa: E402


class CacheHarvestTest(unittest.TestCase):
    def test_collect_parses_unit_filenames(self):
        with tempfile.TemporaryDirectory() as td:
            repo = pathlib.Path(td)
            d = repo / "recomp_cache" / "x" / "y"
            d.mkdir(parents=True)
            (d / "005F0000_DEADBEEF_t.c").write_text("")
            (d / "005F0000_DEADBEEF_t.log").write_text("")
            (d / "805F0000_FEEDFACE_a.c").write_text("")
            (d / "not-a-unit.txt").write_text("")
            units = cache_harvest.collect(repo)
            self.assertEqual(units, {0x005F0000: "t", 0x805F0000: "a"})

    def test_game_addrs_reads_game_toml(self):
        with tempfile.TemporaryDirectory() as td:
            repo = pathlib.Path(td)
            (repo / "game.toml").write_text(
                '[[extra_func]]\naddr = 0x005F0000\nmode = "thumb"\n')
            self.assertEqual(cache_harvest.game_addrs(repo), {0x005F0000})


if __name__ == "__main__":
    unittest.main()
