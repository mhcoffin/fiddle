import json
import unittest
import xml.etree.ElementTree as ET

import overlap_score as o


class OverlapScoreTest(unittest.TestCase):
    def test_artifacts_current(self):
        self.assertEqual(o.artifacts(), o.artifacts())
        for name, data in o.artifacts().items():
            self.assertEqual((o.HERE / name).read_bytes(), data)

    def test_marker_free_musicxml_timelines(self):
        root = ET.fromstring(o.scaffold())
        self.assertEqual(len(root.findall(".//words")), 0)
        self.assertEqual(len(root.findall(".//notations")), 0)
        bars = root.findall("part/measure")
        self.assertEqual(len(bars), 5)
        sounding = []
        semitones = dict(C=0, D=2, E=4, F=5)
        for b, measure in enumerate(bars, 1):
            cursor = 0
            totals = {1: 0, 2: 0}
            for node in measure:
                if node.tag == "backup":
                    cursor -= int(node.findtext("duration"))
                    self.assertEqual(cursor, 0)
                elif node.tag == "note":
                    duration = int(node.findtext("duration"))
                    voice = int(node.findtext("voice"))
                    self.assertEqual(cursor, totals[voice])
                    self.assertLessEqual(cursor + duration, 4)
                    if node.find("pitch") is not None:
                        pitch = 12 * (int(node.findtext("pitch/octave")) + 1) + semitones[node.findtext("pitch/step")]
                        sounding.append((b, cursor + 1, voice, pitch, duration))
                    cursor += duration
                    totals[voice] += duration
            self.assertEqual(totals, {1: 4, 2: 4})
        expected = o.expectations()["notes"]
        self.assertEqual(sounding, [(n["bar"], n["beat"], n["voice"], n["pitch"], n["written_duration_quarters"]) for n in expected])
        self.assertEqual(len(sounding), 17)

    def test_controls_and_overlap_preconditions(self):
        ex = o.expectations()
        notes = {n["note"]: n for n in ex["notes"]}
        for control in ex["controls"]:
            old, new = notes[control["old_note"]], notes[control["next_note"]]
            self.assertLessEqual(old["quarter_offset"] + old["written_duration_quarters"], new["quarter_offset"])
        for probe in ex["overlap_probes"]:
            old = notes[probe["old_note"]]
            end = old["quarter_offset"] + old["written_duration_quarters"]
            for n in probe["overlapping_notes"]:
                self.assertLess(notes[n]["quarter_offset"], end)
                self.assertGreater(notes[n]["quarter_offset"], old["quarter_offset"])
            self.assertLessEqual(end, notes[probe["post_release_note"]]["quarter_offset"])

    def test_pt_ownership_and_map_binding(self):
        m = json.loads((o.HERE / "allocation.json").read_text())
        ex = o.expectations()
        self.assertEqual(ex["map_entity_id"], m["entity_id"])
        self.assertEqual(ex["map_sha256"], m["map_sha256"])
        owned = {pt for d in m["dimensions"] for pt in d["values"]}
        for n in ex["notes"]:
            self.assertLessEqual(set(n["techniques"]), owned)
        for control, overlap in ((0, 4), (8, 12)):
            self.assertEqual([n["techniques"] for n in ex["notes"][control:control + 4]],
                             [n["techniques"] for n in ex["notes"][overlap:overlap + 4]])
        self.assertEqual(ex["notes"][-1]["techniques"], [])


if __name__ == "__main__":
    unittest.main()
