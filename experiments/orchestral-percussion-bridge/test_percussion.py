"""Pitched-percussion profile contracts: shared wire values, schema 2, inventory."""
import json
import unittest
import xml.etree.ElementTree as ET

import generate_percussion as generator
from profiles import COMMON_IDS, PITCHED_PERCUSSION as spec, engine, reference


class PitchedPercussion(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.outputs = engine.build(spec)
        cls.manifest = json.loads(cls.outputs["allocation.json"])

    def test_artifacts_are_current(self):
        for name, data in self.outputs.items():
            self.assertEqual((generator.HERE / "pitched-percussion-r1" / name).read_bytes(), data, name)

    def test_shared_dimensions_keep_strings_wire(self):
        dims = {d["id"]: d for d in self.manifest["dimensions"]}
        for key in COMMON_IDS:
            self.assertEqual((dims[key]["kind"], dims[key]["number"], dims[key]["values"]),
                             (reference[key]["kind"], reference[key]["number"], reference[key]["values"]))

    def test_schema_two(self):
        self.assertEqual(self.manifest["wire_schema"], 2)
        root = ET.fromstring(self.outputs["Fiddle-Orchestral-PitchedPercussion-r1.doricolib"])
        for addon in root.iter("techniqueAddOn"):
            on = int(addon.find("switchOnActions/switchOnAction/param2").text)
            off = int(addon.find("switchOffActions/switchOffAction/param2").text)
            self.assertEqual(off, on + engine.TYPED_OFF)
        pitches = {int(a.findtext("param1")) for a in root.iter("switchOnAction") if a.findtext("type") == "kKeySwitch"}
        self.assertFalse(pitches & set(range(21, 115)) - set(engine.BEACON_PITCHES))

    def test_full_inventory(self):
        inventory = (generator.HERE / "pitched-percussion-r1" / "inventory.md").read_text()
        self.assertEqual(inventory.count("\n| `pt."), 298)


if __name__ == "__main__":
    unittest.main()
