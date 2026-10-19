import json
import unittest
import xml.etree.ElementTree as ET

import smoke_score as s


class SmokeScoreTest(unittest.TestCase):
    def test_generated_artifacts_are_current(self):
        self.assertEqual(s.artifacts(), s.artifacts())
        for name, data in s.artifacts().items():
            self.assertEqual((s.HERE / name).read_bytes(), data)

    def test_score_has_sixteen_plain_quarters_and_instruction_text(self):
        root = ET.fromstring(s.scaffold(marks=s.MARKS))
        bars = root.findall("part/measure")
        self.assertEqual(len(bars), 4)
        self.assertEqual(len(root.findall(".//note")), 16)
        for bar in bars:
            notes = bar.findall("note")
            self.assertEqual(len(notes), 4)
            for note in notes:
                self.assertEqual(note.findtext("pitch/step"), "C")
                self.assertEqual(note.findtext("pitch/octave"), "5")
                self.assertEqual(note.findtext("duration"), "1")
                self.assertIsNone(note.find("notations"))
        self.assertEqual(len(root.findall(".//words")), len(s.MARKS))

    def test_new_scaffolds_omit_instructional_text_by_default(self):
        root = ET.fromstring(s.scaffold(title="New marker-free test", bars=3))
        self.assertEqual(len(root.findall(".//words")), 0)
        self.assertEqual(len(root.findall(".//note")), 12)
        self.assertEqual(len(root.findall(".//metronome")), 1)

    def test_expectations_bind_to_current_map_and_have_no_mutex_conflicts(self):
        expected = s.expectations()
        allocation = json.loads((s.HERE / "allocation.json").read_text())
        self.assertEqual(expected["map_entity_id"], allocation["entity_id"])
        self.assertEqual(expected["map_sha256"], allocation["map_sha256"])
        rows = expected["notes"]
        self.assertEqual(len(rows), 16)
        owned = {pt for d in allocation["dimensions"] for pt in d["values"]}
        for i, row in enumerate(rows):
            self.assertEqual(row["quarter_offset"], i)
            self.assertEqual(row["pitch"], 72)
            pts = set(row["techniques"])
            self.assertLessEqual(pts, owned)
            for d in allocation["dimensions"]:
                self.assertLessEqual(len(pts & d["values"].keys()), 1)
        self.assertEqual(rows[4]["techniques"], [])
        self.assertEqual(rows[5]["techniques"], [])
        self.assertEqual(set(rows[12]["techniques"]),
                         {"pt.fouette", "pt.flautando", "pt.sulTasto", "pt.nonVibrato"})
        self.assertEqual(set(rows[13]["techniques"]),
                         {"pt.detache", "pt.flautando", "pt.sulTasto", "pt.nonVibrato"})
        self.assertEqual(rows[14]["techniques"], ["pt.nonVibrato"])
        self.assertEqual(rows[15]["techniques"], ["pt.vibrato"])


if __name__ == "__main__":
    unittest.main()
