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
        self.assertEqual(notes[2]["techniques"]["includes"], ["pt.legato"])  # the slur's last note too
        self.assertEqual(notes[3]["techniques"]["includes"], ["pt.marcato"])
        self.assertIn("pt.legato", notes[3]["techniques"]["excludes"])

    def test_tenuto_staccato_is_one_mark(self):
        xml = scoretest.musicxml(spec("!mf C5/q+ten+stacc D5/q+stacc+acc E5/h"))
        arts = [[c.tag for c in n.find("notations/articulations")]
                for n in ET.fromstring(xml).iter("note") if n.find("notations/articulations") is not None]
        self.assertEqual(arts, [["detached-legato"], ["staccato", "accent"]])

    def test_dynamics_and_hairpins(self):
        notes = scoretest.sounding_notes(spec("!p < C5/q D5/q E5/q ] !f F5/q | > G5/w~ | G5/w ] !pp"))
        # Levels follow a straight line over the hairpin's time span.
        self.assertEqual(notes[0]["dynamic"], dict(level=-1, during=dict(direction="up", to=1, end_level=-0.333)))
        self.assertEqual(notes[1]["dynamic"]["between"], [-1, 1])
        self.assertEqual(notes[1]["dynamic"]["level"], -0.333)
        self.assertEqual(notes[2]["dynamic"]["during"]["end_level"], 1.0)
        self.assertEqual(notes[3]["dynamic"], dict(level=1))
        self.assertEqual(notes[4]["dynamic"], dict(level=1, during=dict(direction="down", to=-2, end_level=-2.0)))

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

    def test_horn_is_written_a_fifth_above_sounding(self):
        s = dict(id="t", title="T", tempo=100, parts=[
            dict(id="hn", instrument="horn", music="!mf F3/q Bb3/q B3/q F#4/q")])
        xml = ET.fromstring(scoretest.musicxml(s))
        self.assertEqual(xml.find(".//key/fifths").text, "1")
        self.assertEqual([xml.find(f".//transpose/{t}").text for t in ("diatonic", "chromatic")], ["-4", "-7"])
        written = [(n.findtext("pitch/step"), n.findtext("pitch/alter"), n.findtext("pitch/octave"),
                    n.findtext("accidental")) for n in sounding(scoretest.musicxml(s))]
        # Sounding F3, Bb3, B3, F#4 -> written C4, F4 (natural in G), F#4 (in the key), C#5.
        self.assertEqual(written, [("C", None, "4", None), ("F", None, "4", "natural"),
                                   ("F", "1", "4", None), ("C", "1", "5", "sharp")])
        # Expectations stay at sounding pitch.
        self.assertEqual([n["pitch"] for n in scoretest.sounding_notes(s)], [53, 58, 59, 66])
        self.assertEqual(scoretest.expected(s, b"{}")["parts"][0]["instrument"], "instrument.brass.horn.f")

    def test_unpitched_hits_rolls_and_codes(self):
        s = dict(id="t", title="T", tempo=100, parts=[
            dict(id="sn", instrument="snare", music="!mf x/q x/q+acc x/h+trem | !p x/q+xhead x/q+ghost x/h+buzz")])
        notes = scoretest.sounding_notes(s)
        # Every unpitched note plays on the bridge's one percussion-map pitch (36).
        self.assertEqual([n["pitch"] for n in notes], [36] * 6)
        self.assertEqual(notes[2]["techniques"]["includes"], ["pt.tremolo"])
        self.assertIn("pt.buzzRoll", notes[2]["techniques"]["excludes"])
        self.assertEqual(notes[1]["techniques"]["includes"], ["pt.accent"])
        xml = ET.fromstring(scoretest.musicxml(s))
        self.assertEqual(xml.find(".//clef/sign").text, "percussion")
        self.assertEqual(xml.find(".//note/unpitched/display-step").text, "C")
        self.assertIsNone(xml.find(".//note/pitch"))
        tremolos = xml.findall(".//ornaments/tremolo")
        self.assertEqual([(t.get("type"), t.text) for t in tremolos], [("single", "3"), ("unmeasured", "0")])
        heads = xml.findall(".//notehead")
        self.assertEqual([(h.text, h.get("parentheses")) for h in heads], [("x", None), ("normal", "yes")])
        self.assertTrue(scoretest.expected(s, b"{}")["parts"][0]["unpitched"])
        with self.assertRaises(scoretest.SpecError):
            scoretest.sounding_notes(spec("!mf x/q"))  # x heads need an unpitched instrument
        with self.assertRaises(scoretest.SpecError):
            scoretest.sounding_notes(dict(s, parts=[dict(s["parts"][0], music="!mf C5/q")]))

    def test_percussion_marks(self):
        s = dict(id="t", title="T", tempo=100, parts=[
            dict(id="sn", instrument="snare",
                 music="!mf x/q+sidestick x/q+rim x/q+snaresoff x/q | x/q+xhead x/q+snareson x/h+buzz"),
            dict(id="hh", instrument="hi-hat", music="!mf x/q x/q+open x/q+closed x/q | r/w"),
            dict(id="tri", instrument="triangle", music="!mf x/q+xhead r/q r/h | r/w")])
        notes = {(n["part"], n["bar"], n["beat"]): n["techniques"] for n in scoretest.sounding_notes(s)}
        # Side stick is a notehead set by hand; an imported x head expects nothing.
        self.assertEqual(notes["sn", 1, 1.0]["includes"], ["pt.sideStick"])
        self.assertIn("pt.sideStick", notes["sn", 1, 2.0]["excludes"])
        self.assertEqual(notes["tri", 1, 1.0]["includes"], [])
        # Attribute marks apply to their note; the next note expects them gone.
        self.assertEqual(notes["sn", 1, 2.0]["includes"], ["pt.rim"])
        self.assertIn("pt.rim", notes["sn", 1, 3.0]["excludes"])
        # Snares off holds until snares on, which then holds too.
        self.assertEqual(notes["sn", 1, 4.0]["includes"], ["pt.snareOff"])
        self.assertEqual(notes["sn", 2, 1.0]["includes"], ["pt.snareOff"])
        self.assertEqual(notes["sn", 2, 2.0]["includes"], ["pt.snareOn"])
        self.assertEqual(notes["sn", 2, 3.0]["includes"], ["pt.buzzRoll", "pt.snareOn"])
        self.assertIn("pt.snareOff", notes["sn", 2, 3.0]["excludes"])
        # A plain hi-hat note may carry Closed, Dorico's natural equivalent.
        self.assertNotIn("pt.closed", notes["hh", 1, 1.0]["excludes"])
        self.assertIn("pt.open2", notes["hh", 1, 3.0]["excludes"])
        self.assertEqual(notes["hh", 1, 3.0]["includes"], ["pt.closed"])
        steps = scoretest.by_hand(s)
        self.assertEqual([(x["part"], x["bar"], x["beat"], x["mark"]) for x in steps],
                         [("sn", 1, 1.0, "sidestick"), ("sn", 1, 2.0, "rim"), ("hh", 1, 2.0, "open"), ("sn", 1, 3.0, "snaresoff"),
                          ("hh", 1, 3.0, "closed"), ("sn", 2, 2.0, "snareson"), ("sn", 2, 3.0, "buzz")])
        self.assertEqual(steps[1]["text"], "Bar 1 beat 2, Snare Drum: Playing Techniques panel ▸ Unpitched Percussion ▸ On Rim")
        self.assertEqual(scoretest.expected(s, b"{}")["by_hand"], steps)
        cues = [w.text for w in ET.fromstring(scoretest.musicxml(s)).iter("words")]
        self.assertEqual(cues, ["add: Side stick notehead", "add: On Rim", "add: Snares Off", "add: Snares On",
                                "add: Buzz roll, if the import dropped it (z on the stem)",
                                "add: Open (o)", "add: Closed / Rim Shot (+)"])
        self.assertNotIn("by_hand", scoretest.expected(spec("!mf C5/w"), b"{}"))
        with self.assertRaises(scoretest.SpecError):
            scoretest.sounding_notes(spec("!mf C5/q+rim r/h."))  # percussion marks need an unpitched part
        with self.assertRaises(scoretest.SpecError):
            scoretest.sounding_notes(dict(s, parts=[dict(s["parts"][0], music="!mf x/q+snaresoff+snareson r/h.")]))

    def test_notehead_cues_name_the_notehead(self):
        self.assertEqual(scoretest.cue_text("sidestick"), "add: Side stick notehead")
        self.assertEqual(scoretest.cue_text("rimshot"), "add: Rim shot notehead")
        self.assertEqual(scoretest.cue_text("crossstick"), "add: Cross stick notehead")

    def test_percussion_code_rule(self):
        # r1's entries (Natural and the roll kinds) all share pitch 36; the
        # best-subset rule still picks the roll entry, as Dorico does.
        entries = scoretest.percussion_codes()
        self.assertEqual({pitch for pitch, _ in entries}, {36})
        self.assertIn(frozenset(["pt.tremolo"]), {ids for _, ids in entries})
        for techniques in ([], ["pt.accent"], ["pt.roll", "pt.accent"], ["pt.rim"], ["pt.rim", "pt.tremolo"]):
            self.assertEqual(scoretest.percussion_code(techniques), 36)


if __name__ == "__main__":
    unittest.main()
