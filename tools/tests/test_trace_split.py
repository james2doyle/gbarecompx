#!/usr/bin/env python3
"""Framework-side tests for the trace-tool (stdlib unittest).

Covers trace_split: backward-jump segmentation + header preservation on
the `# gbarecomp-keyinput-v1` trace format.

Run: python3 -m unittest discover -s tools/tests -p 'test_*.py'
(the CI guard job does exactly this).
"""
import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))


class TraceSplitTest(unittest.TestCase):
    def test_backward_jump_splits_and_headers_preserved(self):
        with tempfile.TemporaryDirectory() as td:
            t = pathlib.Path(td) / "trace.csv"
            t.write_text("# gbarecomp-keyinput-v1\n# frame,keyinput_active_low\n"
                         "0,0x03FF\n10,0x03F7\n20,0x03FF\n5,0x03FE\n15,0x03FF\n")
            r = subprocess.run(
                [sys.executable, "tools/trace_split.py", "--trace", str(t),
                 "--out-dir", td, "--prefix", "seg"],
                cwd=str(REPO), capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            seg1 = (pathlib.Path(td) / "seg1.csv").read_text()
            seg2 = (pathlib.Path(td) / "seg2.csv").read_text()
            self.assertIn("# gbarecomp-keyinput-v1", seg1)
            self.assertIn("20,0x03FF", seg1)
            self.assertNotIn("5,0x03FE", seg1)
            self.assertIn("5,0x03FE", seg2)
            self.assertIn("15,0x03FF", seg2)
            self.assertNotIn("20,0x03FF", seg2)

    def test_monotonic_single_segment(self):
        with tempfile.TemporaryDirectory() as td:
            t = pathlib.Path(td) / "trace.csv"
            t.write_text("0,0x03FF\n5,0x03F7\n")
            r = subprocess.run(
                [sys.executable, "tools/trace_split.py", "--trace", str(t),
                 "--out-dir", td, "--prefix", "m"],
                cwd=str(REPO), capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertTrue((pathlib.Path(td) / "m1.csv").exists())
            self.assertFalse((pathlib.Path(td) / "m2.csv").exists())


if __name__ == "__main__":
    unittest.main()
