"""Score test specs, MusicXML and expected results."""
import json
import unittest
import xml.etree.ElementTree as ET

import generate
import scoretest


def spec(music, tempo=100, **extra):
    return dict(id="t", title="T", tempo=tempo, parts=[dict(id="vln", instrument="violin", music=music)], **extra)


def sounding(xml):
    root = ET.fromstring(xml)
    return [n for n in root.iter("note") if n.find("rest") is None
            and not any(t.get("type") == "stop" for t in n.iter("tie"))]


class Generated(unittest.TestCase):
    def test_generated_files_are_current(self):
        for name, data in generate.artifacts().items():
            self.assertEqual((generate.GENERATED / name).read_bytes(), data, name)

    def test_every_test_has_matching_musicxml_and_expectations(self):
        index = json.loads((generate.GENERATED / "index.json").read_text())
        self.assertGreaterEqual(len(index["tests"]), 8)
        for test in index["tests"]:
            xml = (generate.GENERATED / test["musicxml"]).read_bytes()
            expected = json.loads((generate.GENERATED / test["expected"]).read_text())
            self.assertEqual(len(sounding(xml)), len(expected["notes"]), test["id"])
            sounds = [s.text for s in ET.fromstring(xml).iter("instrument-sound")]
            self.assertEqual(len(sounds), len(expected["parts"]))


class Timing(unittest.TestCase):
    def test_onsets_durations_and_ties(self):
        notes = scoretest.sounding_notes(spec("!mf C5/h D5/h~ | D5/q E5/e r/e F5/h"))
        self.assertEqual([n["onset_ms"] for n in notes], [0, 1200, 3000, 3600])
        self.assertEqual([n["duration_ms"] for n in notes], [1200, 1800, 300, 1200])
        self.assertEqual([(n["bar"], n["beat"]) for n in notes], [(1, 1), (1, 3), (2, 2), (2, 3)])

    def test_tempo_map(self):
        notes = scoretest.sounding_notes(spec("!mf C5/w | T=120 D5/h T=60 E5/h", tempo=60))
        self.assertEqual([n["onset_ms"] for n in notes], [0, 4000, 5000])
        self.assertEqual([n["duration_ms"] for n in notes], [4000, 1000, 2000])

    def test_bad_specs_are_rejected(self):
        for music in ["!mf C5/w D5/q", "!mf C5/h. D5/h | E5/q", "C5/w", "!mf C5/h~ D5/h",
                      "!mf C5/w | !zz D5/w", "!mf C5/q+wobble r/h."]:
            with self.assertRaises(scoretest.SpecError, msg=music):
                scoretest.sounding_notes(spec(music))


class Expectations(unittest.TestCase):
    def test_articulations_and_slurs(self):
        notes = scoretest.sounding_notes(spec("!mf ( C5/q+stacc D5/q E5/q ) F5/q+marc"))
        self.assertEqual(notes[0]["techniques"]["includes"], ["pt.legato", "pt.staccato"])
        self.assertIn("pt.marcato", notes[0]["techniques"]["excludes"])
        self.assertEqual(notes[2]["techniques"]["includes"], [])  # last slurred note: legato unchecked
        self.assertNotIn("pt.legato", notes[2]["techniques"]["excludes"])
        self.assertEqual(notes[3]["techniques"]["includes"], ["pt.marcato"])
        self.assertIn("pt.legato", notes[3]["techniques"]["excludes"])

    def test_dynamics_and_hairpins(self):
        notes = scoretest.sounding_notes(spec("!p < C5/q D5/q E5/q ] !f F5/q | > G5/w~ | G5/w ] !pp"))
        self.assertEqual(notes[0]["dynamic"], dict(level=-1, during=dict(direction="up", to=1)))
        self.assertEqual(notes[1]["dynamic"]["between"], [-1, 1])
        self.assertEqual(notes[3]["dynamic"], dict(level=1))
        self.assertEqual(notes[4]["dynamic"], dict(level=1, during=dict(direction="down", to=-2)))

    def test_two_parts_interleave_by_onset(self):
        s = dict(id="t", title="T", tempo=100, parts=[
            dict(id="a", instrument="violin", music="!mf C5/h D5/h"),
            dict(id="b", instrument="flute", music="!mf E5/q F5/q G5/h")])
        notes = scoretest.sounding_notes(s)
        self.assertEqual([(n["part"], n["onset_ms"]) for n in notes],
                         [("a", 0), ("b", 0), ("b", 600), ("a", 1200), ("b", 1200)])
        expected = scoretest.expected(s, b"{}")
        self.assertEqual([p["instrument"] for p in expected["parts"]],
                         ["instrument.strings.violin", "instrument.wind.flute"])


if __name__ == "__main__":
    unittest.main()
