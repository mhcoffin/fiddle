import hashlib
import json
import unittest
import xml.etree.ElementTree as ET

import shared_overlap_score as s


class SharedOverlapScoreTest(unittest.TestCase):
    def test_artifacts_current(self):
        for name, data in s.artifacts().items():
            self.assertEqual((s.HERE / name).read_bytes(), data)

    def test_marker_free_score_and_timelines(self):
        root = ET.fromstring(s.scaffold())
        self.assertEqual(root.findall(".//words"), [])
        self.assertEqual(root.findall(".//notations"), [])
        self.assertEqual(root.findtext(".//per-minute"), "80")
        self.assertEqual(len(root.findall("part/measure")), 3)
        sounding = []
        for measure in root.findall("part/measure"):
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
                        pitch = 12 * (int(node.findtext("pitch/octave")) + 1) + dict(C=0, D=2, E=4, F=5, G=7)[node.findtext("pitch/step")]
                        sounding.append((int(measure.get("number")), cursor + 1, voice, pitch, duration))
                    cursor += duration
                    totals[voice] += duration
            self.assertEqual(totals, {1: 4, 2: 4})
        expected = s.expectations()["notes"]
        self.assertEqual(sorted(sounding), sorted((n["bar"], n["beat"], n["voice"], n["pitch"], n["written_duration_quarters"]) for n in expected))
        self.assertEqual(len(sounding), 12)

    def test_shared_pt_timeline_and_geometry(self):
        ex = s.expectations()
        notes = {n["note"]: n for n in ex["notes"]}
        self.assertIn("not asserted", ex["simultaneous_note_order"])
        for bar in (1, 2, 3):
            group = [n for n in notes.values() if n["bar"] == bar]
            self.assertEqual({tuple(n["techniques"]) for n in group},
                             {("pt.flautando",)} if bar < 3 else {()})
            self.assertEqual({n["voice"] for n in group if n["beat"] == 1}, {1, 2})
        for control in ex["controls"]:
            old, new = notes[control["old_note"]], notes[control["next_note"]]
            self.assertLessEqual(old["quarter_offset"] + old["written_duration_quarters"], new["quarter_offset"])
        for probe in ex["overlap_probes"]:
            old = notes[probe["old_note"]]
            end = old["quarter_offset"] + old["written_duration_quarters"]
            for number in probe["overlapping_notes"]:
                new = notes[number]
                self.assertLess(old["quarter_offset"], new["quarter_offset"])
                self.assertLess(new["quarter_offset"], end)
                self.assertEqual(old["techniques"], new["techniques"])
                self.assertNotEqual(old["pitch"], new["pitch"])
            self.assertLessEqual(end, notes[probe["post_release_note"]]["quarter_offset"])

    def test_map_binding_and_technique_ownership(self):
        allocation = json.loads((s.HERE / "allocation.json").read_text())
        ex = s.expectations()
        self.assertEqual(ex["map_entity_id"], allocation["entity_id"])
        self.assertEqual(ex["map_sha256"], allocation["map_sha256"])
        self.assertEqual(ex["map_sha256"], hashlib.sha256((s.HERE / s.MAP_FILE).read_bytes()).hexdigest())
        owned = {pt for d in allocation["dimensions"] for pt in d["values"]}
        for n in ex["notes"]:
            self.assertLessEqual(set(n["techniques"]), owned)


if __name__ == "__main__":
    unittest.main()
