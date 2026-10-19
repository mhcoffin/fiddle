import json
import unittest
import xml.etree.ElementTree as ET

import reliability


class ReliabilityTest(unittest.TestCase):
    def test_score_rhythm_and_chords(self):
        root = ET.fromstring(reliability.scaffold())
        bars = root.findall("./part/measure")
        self.assertEqual(len(bars), 7)
        self.assertEqual(len(root.findall(".//note")), 38)
        for bar in bars:
            self.assertEqual(sum(int(n.findtext("duration")) for n in bar.findall("note")
                                 if n.find("chord") is None), 4)
        self.assertEqual(root.find(".//sound").get("tempo"), "120")
        self.assertEqual(root.findtext(".//midi-channel"), "2")

    def test_score_matches_expected_pitches_and_onsets(self):
        actual = []
        for bar in ET.fromstring(reliability.scaffold()).findall("./part/measure"):
            beat = 0
            for note in bar.findall("note"):
                if note.find("chord") is None:
                    beat += 1
                actual.append((int(bar.get("number")), beat,
                               {"C": 72, "E": 76, "G": 79}[note.findtext("pitch/step")]))
        self.assertEqual(actual, [(n["bar"], n["beat"], n["pitch"]) for n in reliability.expectations()])

    def test_states_are_in_existing_decoder(self):
        decoder = json.loads((reliability.HERE / "generated/high-vibrato.decoder.json").read_text())
        ids = set(decoder["key_dimensions"][0]["values"].values())
        ids.update(k["technique"] for k in decoder["key_switches"])
        self.assertTrue(all(set(n["techniques"]) <= ids for n in reliability.expectations()))

    def test_restoration_expectations(self):
        lookup = {(n["bar"], n["beat"]): n for n in reliability.expectations()}
        self.assertEqual(lookup[5, 2]["techniques"], ["pt.nonVibrato"])
        self.assertEqual(lookup[5, 3]["techniques"], ["pt.nonVibrato"])
        self.assertEqual(lookup[5, 4]["techniques"], ["pt.vibrato"])
        self.assertEqual(lookup[7, 3]["techniques"], ["pt.flautando", "pt.vibrato"])
        self.assertEqual(lookup[7, 4]["techniques"], ["pt.vibrato"])
        self.assertEqual(sum(n["restoration_probe"] for n in reliability.expectations()), 2)

    def test_artifacts_current(self):
        for name, data in reliability.artifacts().items():
            self.assertEqual((reliability.HERE / "generated" / name).read_bytes(), data)

    def test_mid_pass_counts(self):
        notes = reliability.expectations()
        self.assertEqual(sum((n["bar"], n["beat"]) >= (2, 3) for n in notes), 30)
        self.assertEqual(sum((n["bar"], n["beat"]) >= (5, 3) for n in notes), 18)


if __name__ == "__main__":
    unittest.main()
