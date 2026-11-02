"""run_report.py against the sample run record (fixtures/sample-run.json,
written by ScoreTestTest --write-fixture)."""
import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import run_report

FIXTURE = Path(__file__).resolve().parent / "fixtures" / "sample-run.json"


class Report(unittest.TestCase):
    def setUp(self):
        self.record = json.loads(FIXTURE.read_text())

    def test_failed_and_extra_notes_are_explained(self):
        text = run_report.report(self.record)
        self.assertIn("Run basic-rhythm-20261021-143200: Basic rhythm (basic-rhythm) FAILED", text)
        self.assertIn("17 of 18 notes passed; 1 failed, 0 missing, 1 extra", text)
        self.assertIn("vln (instrument.strings.violin) <- port 1 channel 2", text)
        self.assertIn("#3 vln bar 3 beat 1 F5/q: FAIL", text)
        self.assertIn("FAILED onset: expected 4800.0, received 4812.0 (+12 ms)", text)
        self.assertIn("received F#6 at 500.0 ms", text)
        self.assertIn("identity check matched", text)

    def test_midi_around_a_note_is_on_the_score_clock(self):
        text = run_report.report(self.record, notes=[3], window_ms=10)
        midi = text[text.index("MIDI within 10"):]
        self.assertIn("4812.000  port 1 ch 2  note on F5 (77) vel 100", midi)
        self.assertIn("bridge command: CC102 = 0", midi)
        self.assertNotIn("4790.000", midi)

    def test_runs_are_found_by_id_and_listed(self):
        folder = tempfile.mkdtemp()
        try:
            shutil.copy(FIXTURE, Path(folder) / "basic-rhythm-20261021-143200.json")
            with mock.patch.dict(os.environ, {"FIDDLE_SCORE_TEST_RUNS_DIR": folder}):
                path, record = run_report.load("basic-rhythm-20261021-143200")
                self.assertEqual(record["run_id"], "basic-rhythm-20261021-143200")
                self.assertEqual([r["run_id"] for r in run_report.list_runs("basic-rhythm")],
                                 ["basic-rhythm-20261021-143200"])
                self.assertEqual(run_report.list_runs("slurs"), [])
                out = io.StringIO()
                with contextlib.redirect_stdout(out), mock.patch.object(sys, "argv", ["run_report.py", "--list"]):
                    run_report.main()
                self.assertIn("basic-rhythm-20261021-143200  FAIL  17/18 passed", out.getvalue())
                with self.assertRaises(SystemExit):
                    run_report.load("no-such-run")
        finally:
            shutil.rmtree(folder)


if __name__ == "__main__":
    unittest.main()
