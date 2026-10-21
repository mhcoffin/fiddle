"""Branch check score and comparison, against synthetic captures."""
import json
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

import check_score
import compare

EXPECTED = json.loads((check_score.HERE / check_score.EXPECTED_FILE).read_text())
NAMES = compare.NAMES
MAP = """<kScoreLibrary><expressionMapDefinitions><entities>
<ExpressionMapDefinition><name>Lib</name>
  <playingTechniqueCombinations><playingTechniqueCombination>
    <volumeType><type>kCC</type><param1>11</param1></volumeType>
    <switchOnActions><switchOnAction><type>kKeySwitch</type><param1>24</param1><param2>100</param2></switchOnAction></switchOnActions>
  </playingTechniqueCombination></playingTechniqueCombinations>
</ExpressionMapDefinition></entities></expressionMapDefinitions></kScoreLibrary>"""


def stream(change=None):
    """(ms, kind, channel, number, value) for the score: a keyswitch and
    CC11 before each note, 500 ms apart, 300 ms long."""
    events = []
    for note in EXPECTED["notes"]:
        t = 1000 + 500 * note["index"]
        velocity = 80
        if change and note["index"] == 3:
            velocity = change.get("velocity", velocity)
        events += [(t - 10, "on", 1, 24, 100), (t, "cc", 1, 11, 70),
                   (t, "on", 1, note["pitch"], velocity), (t + 300, "off", 1, note["pitch"], 0)]
    if change and "extra_switch" in change:
        events.append((1000 + 500 * 5 - 5, "cc", 1, 20, 1))
    return events


def write_probe(path, events, rate=48000):
    kinds = {"on": 0, "off": 1, "cc": 3, "pc": 6}
    rows = [dict(kind="header", schema=1, capture_only=True, sample_rate=rate)]
    count = 0

    def block(playing, items):
        nonlocal count
        rows.append(dict(kind="block", block=count, playing=playing, inputs=len(items),
                         input_dropped=0, input_invalid=0))
        for sequence, (ms, kind, channel, number, value) in enumerate(items):
            rows.append(dict(kind="input", block=count, sequence=sequence, channel=channel,
                             type=kinds[kind], data1=number, data2=value, raw_value=value / 127,
                             host_samples=round(ms * rate / 1000), valid=True))
        count += 1

    block(False, [])
    for e in sorted(events):
        block(True, [e])
    block(False, [])
    rows.append(dict(kind="end", capture_dropped=0, blocks=count))
    path.write_text("".join(json.dumps(r) + "\n" for r in rows))


def write_fiddle(path, events, offset=1234.0):
    kinds = {"on": "NoteOn", "off": "NoteOff", "cc": "CC", "pc": "PC"}
    lines = []
    for ms, kind, channel, number, value in events:
        param = NAMES[number % 12] + str(number // 12 - 1) if kind in ("on", "off") else str(number)
        lines.append(f"{(ms + offset) / 1000:.3f}s\t{kinds[kind]}\tCh{channel}\t{param}\t{value}")
    path.write_text("\n".join(lines))


class Score(unittest.TestCase):
    def test_artifacts_are_current(self):
        for name, data in check_score.artifacts().items():
            self.assertEqual((check_score.HERE / name).read_bytes(), data, name)

    def test_sounding_notes_match_expectations(self):
        root = ET.fromstring(check_score.score())
        sounding = [n for n in root.iter("note") if n.find("rest") is None
                    and not any(t.get("type") == "stop" for t in n.iter("tie"))]
        self.assertEqual(len(sounding), len(EXPECTED["notes"]))
        self.assertEqual(EXPECTED["techniques"],
                         {"6": "pizz.", "7": "arco", "8": "con sord.", "9": "senza sord."})


class Compare(unittest.TestCase):
    def run_compare(self, fiddle_events):
        with tempfile.TemporaryDirectory() as directory:
            d = Path(directory)
            (d / "map.doricolib").write_text(MAP)
            write_probe(d / "probe.jsonl", stream())
            write_fiddle(d / "fiddle.midi-dump", fiddle_events)
            keyswitches, controllers = compare.map_facts(d / "map.doricolib")
            reference = compare.notes(compare.probe_events(d / "probe.jsonl", len(EXPECTED["notes"]),
                                                           keyswitches), keyswitches, controllers)
            candidate = compare.notes(compare.fiddle_events(d / "fiddle.midi-dump"), keyswitches, controllers)
            return compare.compare(reference, candidate, EXPECTED)

    def test_map_facts(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "map.doricolib"
            path.write_text(MAP)
            self.assertEqual(compare.map_facts(path), ({24}, {1, 11}))

    def test_identical_streams_match(self):
        rows, counts = self.run_compare(stream())
        self.assertFalse(counts)
        self.assertTrue(all(not r["problems"] for r in rows))

    def test_differences_are_reported(self):
        rows, counts = self.run_compare(stream({"velocity": 90, "extra_switch": True}))
        self.assertEqual(counts["velocity"], 1)
        self.assertEqual(counts["switches"], 1)
        self.assertIn("velocity 80->90", rows[3]["problems"])

    def test_note_names(self):
        self.assertEqual(compare.note_number("C4"), 60)
        self.assertEqual(compare.note_number("C#-1"), 1)
        self.assertEqual(compare.note_number("74"), 74)


if __name__ == "__main__":
    unittest.main()
