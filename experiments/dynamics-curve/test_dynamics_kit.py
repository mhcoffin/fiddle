"""Kit artifacts and analysis, checked against synthetic MIDI Probe captures
made from a known curve."""
import json
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

import analyze
import kit

EXPECTED = json.loads((kit.HERE / kit.EXPECTED_FILE).read_text())
TRUE_LEVELS = dict(zip(kit.LADDER, [-6, -5, -4, -3, -2, -1, -0.5, 0.5, 1, 2, 3, 4, 5, 6]))


def synthetic_capture(path, form="root", extra_aborted_pass=False):
    """A closed capture: one pass per kit pass, separated by stopped blocks."""
    rows = [dict(kind="header", schema=1, capture_only=True, sample_rate=48000)]
    blocks = []

    def block(playing, events=()):
        index = len(blocks)
        blocks.append(index)
        rows.append(dict(kind="block", block=index, host_samples=index * 512, playing=playing,
                         inputs=len(events), input_dropped=0, input_invalid=0))
        for sequence, (channel, kind, data1, raw) in enumerate(events):
            rows.append(dict(kind="input", block=index, sequence=sequence, channel=channel,
                             type=kind, data1=data1, data2=max(1, round(raw * 127)),
                             raw_value=raw, host_samples=index * 512, valid=True))

    def play(setting, notes):
        for note in notes:
            level = TRUE_LEVELS.get(note["dynamic"], 0.5)
            articulation = note["articulation"]
            if articulation:
                level += setting[articulation]
            value = analyze.model(level, setting, form)
            block(True, [(1, analyze.CONTROL_CHANGE, 1, value), (1, analyze.NOTE_ON, note["pitch"], value),
                         (2, analyze.NOTE_ON, note["pitch"], value)])
            if note["kind"] == "hairpin":
                for step in range(1, 5):
                    block(True, [(1, analyze.CONTROL_CHANGE, 1, value + step * 0.05)])
        block(False)

    block(False)
    if extra_aborted_pass:
        play(EXPECTED["passes"][0], EXPECTED["notes"][:5])
    for setting in EXPECTED["passes"]:
        play(setting, EXPECTED["notes"])
    rows.append(dict(kind="end", capture_dropped=0, blocks=len(blocks)))
    path.write_text("".join(json.dumps(r) + "\n" for r in rows))


class Kit(unittest.TestCase):
    def test_artifacts_are_current(self):
        for name, data in kit.artifacts().items():
            self.assertEqual((kit.HERE / name).read_bytes(), data, name)

    def test_score_and_expectations_agree(self):
        root = ET.fromstring(kit.score())
        for part in root.iter("part"):
            sounding = [n for n in part.iter("note")
                        if n.find("rest") is None and not any(t.get("type") == "stop" for t in n.iter("tie"))]
            self.assertEqual(len(sounding), len(EXPECTED["notes"]))
        self.assertEqual([r["dynamic"] for r in EXPECTED["notes"][:42:3]], kit.LADDER)

    def test_maps_differ_only_in_volume_type(self):
        maps = ET.fromstring(kit.expression_maps()).iter("ExpressionMapDefinition")
        volumes = [m.find(".//volumeType/type").text for m in maps]
        self.assertEqual(volumes, ["kCC", "kNoteVelocity"])


class Analysis(unittest.TestCase):
    def run_analysis(self, **options):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.jsonl"
            synthetic_capture(path, **options)
            _, blocks = analyze.read_capture(path)
            return analyze.analyze(blocks, EXPECTED)

    def test_recovers_levels_and_curve_form(self):
        for form in analyze.FORMS:
            result = self.run_analysis(form=form)
            for key in ("cc1", "velocity"):
                fit = result["fit"][key]
                self.assertEqual(fit["best_form"], form)
                for mark, level in TRUE_LEVELS.items():
                    self.assertAlmostEqual(fit["levels"][mark], level, places=6)

    def test_articulation_level_deltas(self):
        result = self.run_analysis()
        for p in result["passes"]:
            for art in ("accent", "marcato", "tenuto"):
                for key in ("cc1", "velocity"):
                    self.assertAlmostEqual(p["articulations"][art][key], p["setting"][art], places=6)

    def test_hairpin_track_and_incomplete_pass_skipped(self):
        result = self.run_analysis(extra_aborted_pass=True)
        self.assertEqual([p["capture_pass"] for p in result["passes"]], [2, 3, 4, 5])
        self.assertEqual(result["passes"][0]["hairpin"]["points"], 4)

    def test_override_formats(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "export.doricolib"
            path.write_text("<kScoreLibrary><optionsOverride><option>dynamicsOptions.accentDynamicDelta"
                            "</option><value>double: 0.6</value></optionsOverride></kScoreLibrary>")
            self.assertEqual(analyze.override_formats(path),
                             {"dynamicsOptions.accentDynamicDelta": "double: 0.6"})


if __name__ == "__main__":
    unittest.main()
