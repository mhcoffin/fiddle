"""Fretted profile contracts: shared strings wire, schema 2, inventory, coverage."""
import json
import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

import generate_fretted as generator
from profiles import COMMON_IDS, EXTENDED, FRETTED as spec, STEMS, engine, reference

ROOT = Path(__file__).resolve().parents[2]
MAP = "Fiddle-Fretted-r1.doricolib"


class Fretted(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.outputs = engine.build(spec)
        cls.manifest = json.loads(cls.outputs["allocation.json"])
        cls.dims = {d["id"]: d for d in cls.manifest["dimensions"]}
        cls.root = ET.fromstring(cls.outputs[MAP])

    def test_artifacts_are_current(self):
        for name, data in self.outputs.items():
            self.assertEqual((generator.HERE / "fretted-r1" / name).read_bytes(), data, name)

    def test_identity(self):
        self.assertEqual(self.root.findtext(".//ExpressionMapDefinition/entityID"), "xmap.user.Fiddle.Fretted.r1")
        groups = [g.findtext("groupID") for g in self.root.iter("mutualExclusionGroup")]
        self.assertTrue(groups and all(g.startswith("ptmg.user.Fiddle.Fretted.r1.") for g in groups))

    def test_shared_dimensions_keep_strings_wire(self):
        for key in COMMON_IDS:
            self.assertEqual((self.dims[key]["kind"], self.dims[key]["number"], self.dims[key]["values"]),
                             (reference[key]["kind"], reference[key]["number"], reference[key]["values"]))

    def test_extended_dimensions_keep_strings_values(self):
        for key, (extra, _) in EXTENDED.items():
            dim = self.dims[key]
            self.assertEqual((dim["kind"], dim["number"]), (reference[key]["kind"], reference[key]["number"]))
            for pt, value in reference[key]["values"].items():
                self.assertEqual(dim["values"][pt], value)
            self.assertEqual(set(dim["values"]) - set(reference[key]["values"]), {"pt." + pt for pt in extra.split()})

    def test_guitar_techniques(self):
        owned = {pt for d in self.dims.values() for pt in d["values"]}
        for pt in ("pt.bend", "pt.preBend", "pt.palmMuting", "pt.strumUp", "pt.leftHandTap", "pt.slide",
                   "pt.vibratoBarDip", "pt.pinchHarmonic", "pt.harpHarmonic", "pt.guitarPizzicato",
                   "pt.wahWahGuitar", "pt.distortion", "pt.laissezVibrer", "pt.tremolo", "pt.legato"):
            self.assertIn(pt, owned)
        for pt in ("pt.colLegnoBattuto", "pt.upbow", "pt.sulPonticello", "pt.lhPizzicato", "pt.pince"):
            self.assertNotIn(pt, owned)

    def test_schema_two_and_pinned_durations(self):
        self.assertEqual(self.manifest["wire_schema"], 2)
        self.assertTrue(self.manifest["written_durations"])
        for addon in self.root.iter("techniqueAddOn"):
            on = int(addon.find("switchOnActions/switchOnAction/param2").text)
            off = int(addon.find("switchOffActions/switchOffAction/param2").text)
            self.assertEqual(off, on + engine.TYPED_OFF)
        pitches = {int(a.findtext("param1")) for a in self.root.iter("switchOnAction") if a.findtext("type") == "kKeySwitch"}
        self.assertFalse(pitches & set(range(21, 115)) - set(engine.BEACON_PITCHES))
        options = {o.findtext("option") for o in self.root.iter("optionsOverride")}
        self.assertTrue(set(engine.DURATION_OPTIONS) <= options)

    def test_full_inventory(self):
        inventory = (generator.HERE / "fretted-r1" / "inventory.md").read_text()
        self.assertEqual(inventory.count("\n| `pt."), 298)

    def test_bridge_sections_stems(self):
        header = (ROOT / "Source/Server/BridgeSections.h").read_text()
        stems = re.findall(r'\{"(instrument\.[^"]+)", BridgeSection::Fretted\}', header)
        self.assertEqual(stems, STEMS)
        self.assertIn('"Fiddle-Fretted-r1.doricolib"', header)


if __name__ == "__main__":
    unittest.main()
