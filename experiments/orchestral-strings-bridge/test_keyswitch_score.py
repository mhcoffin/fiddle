import json
import unittest
import xml.etree.ElementTree as ET

import keyswitch_score as k


class KeyswitchScoreTest(unittest.TestCase):
    def test_artifacts_current(self):
        for name, data in k.artifacts().items():
            self.assertEqual((k.HERE / name).read_bytes(), data)
        self.assertEqual(k.artifacts(), k.artifacts())

    def test_plain_three_bar_score(self):
        root = ET.fromstring(k.artifacts()[f"keyswitch-r{k.REVISION}.musicxml"])
        bars = root.findall("part/measure")
        self.assertEqual(len(bars), 3)
        for bar in bars:
            self.assertEqual(len(bar.findall("note")), 4)
            self.assertEqual(sum(int(n.findtext("duration")) for n in bar.findall("note")), 4)
        self.assertEqual(len(root.findall(".//words")), 12)
        self.assertEqual(len(root.findall(".//notations")), 0)
        self.assertEqual(len(root.findall(".//chord")), 0)
        self.assertEqual(len(root.findall(".//barline")), 1)
        self.assertEqual(bars[-1].findtext("barline/bar-style"), "light-heavy")

    def test_wire_mapping_and_expected_states(self):
        m = json.loads((k.HERE / "allocation.json").read_text())
        ex = k.expectations()
        self.assertEqual(ex["map_sha256"], m["map_sha256"])
        self.assertEqual(ex["map_entity_id"], m["entity_id"])
        dims = {d["id"]: d for d in m["dimensions"]}
        self.assertEqual((dims["strum"]["kind"], dims["strum"]["number"]), ("KS", 120))
        self.assertEqual(dims["strum"]["values"], {"pt.strumUp": 2, "pt.strumDown": 3})
        self.assertEqual(dims["strum"]["unspecified"], 1)
        xylo = dims["sons_xylophoniques"]
        self.assertEqual((xylo["kind"], xylo["number"], xylo["values"]["pt.sonsXylophoniques"]), ("KS", 8, 2))
        self.assertEqual((dims["vibrato"]["kind"], dims["vibrato"]["number"]), ("CC", 105))
        rows = ex["notes"]
        self.assertEqual(len(rows), 12)
        for i, row in enumerate(rows):
            self.assertEqual(row["pitch"], 72)
            self.assertEqual(row["quarter_offset"], i)
            pts = set(row["techniques"])
            owners = [pt for d in dims.values() for pt in d["values"] if pt in pts]
            self.assertEqual(set(owners), pts)
            for d in dims.values():
                self.assertLessEqual(len(pts & d["values"].keys()), 1)
        self.assertEqual(rows[4]["techniques"], ["pt.sonsXylophoniques"])
        self.assertEqual(set(rows[6]["techniques"]), {"pt.sonsXylophoniques", "pt.vibrato", "pt.strumUp"})
        self.assertEqual(set(rows[7]["techniques"]), {"pt.sonsXylophoniques", "pt.vibrato", "pt.strumDown"})
        self.assertEqual(rows[8]["techniques"], [])
        self.assertEqual(rows[9]["techniques"], ["pt.strumUp"])
        self.assertEqual(rows[10]["techniques"], ["pt.strumDown"])
        self.assertEqual(rows[11]["techniques"], [])


if __name__ == "__main__":
    unittest.main()
