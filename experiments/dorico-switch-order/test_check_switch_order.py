"""check_switch_order.py against reduced real captures (fixtures/, extracted
with --extract from the 2026 captures named after them) and edits of them."""
import copy
import json
import subprocess
import sys
import unittest
from pathlib import Path

import check_switch_order as check

HERE = Path(__file__).resolve().parent


def fixture(name):
    data = json.loads((HERE / "fixtures" / name).read_text())
    return data["events"], data["sample_rate"]


def at_ms(events, sample_rate, ms, cc, value):
    """Index of the CC event at this time (ms from the first musical note)."""
    start = min(e[0] for e in events if e[1] == check.NOTE_ON and e[4] > 0 and 21 <= e[3] <= 114)
    for i, e in enumerate(events):
        if e[1] == check.CONTROL_CHANGE and e[3] == cc and e[4] == value \
                and abs((e[0] - start) * 1000 / sample_rate - ms) < 0.05:
            return i
    raise AssertionError(f"no CC{cc}={value} at {ms} ms")


class Verdicts(unittest.TestCase):
    def test_reported_capture_shows_on_then_off(self):
        events, rate = fixture("r2-shared-overlap-2026-09-25.json")
        result = check.analyze(events, rate)
        self.assertEqual(result["verdict"], "STILL PRESENT")
        [(off, on)] = result["offs"]
        self.assertAlmostEqual((off - on) * 1000 / rate, 16.01, places=2)
        self.assertEqual(len(result["missing"]), 5)
        self.assertEqual([(old, new) for _, old, new in result["base_changes"]], [(2, 4), (4, 2)])

    def test_beta_capture_still_shows_on_then_off(self):
        events, rate = fixture("r2-shared-overlap-beta-2026-10-23.json")
        result = check.analyze(events, rate)
        self.assertEqual(result["verdict"], "STILL PRESENT")
        [(off, on)] = result["offs"]
        self.assertEqual(off - on, 690)

    def test_without_the_late_off_it_is_fixed(self):
        events, rate = fixture("r2-shared-overlap-2026-09-25.json")
        del events[at_ms(events, rate, 3000.0, 112, 0)]
        result = check.analyze(events, rate)
        self.assertEqual(result["verdict"], "FIXED", result["lines"])
        self.assertEqual(result["missing"], [])

    def test_off_before_the_restated_on_is_harmless(self):
        # Correct order: the old combination's Off, then the new On.
        events, rate = fixture("r2-shared-overlap-2026-09-25.json")
        late_off = events.pop(at_ms(events, rate, 3000.0, 112, 0))
        on = at_ms(events, rate, 2983.99, 112, 1)
        events.insert(on, [events[on][0]] + late_off[1:])
        result = check.analyze(events, rate)
        self.assertEqual(result["verdict"], "FIXED", result["lines"])
        self.assertEqual(len(result["repaired"]), 1)

    def test_without_a_base_change_nothing_is_proved(self):
        events, rate = fixture("single-base-d1-2026-10-15.json")
        self.assertEqual(check.analyze(events, rate)["verdict"], "INCONCLUSIVE")

    def test_the_clear_for_nat_is_expected(self):
        events, rate = fixture("single-base-d1-2026-10-15.json")
        result = check.analyze(copy.deepcopy(events), rate)
        self.assertEqual(result["offs"], [])
        self.assertTrue(any("CC112=0" in line and "final" in line for line in result["lines"]))


class Files(unittest.TestCase):
    def test_score_is_current(self):
        subprocess.run([sys.executable, str(HERE / "make_score.py"), "--check"], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
