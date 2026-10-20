"""Wire schema 2 contracts: typed Off, reserved beacon pitches, frozen r3 allocation."""
import json
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

import generate as g
import profile_r3 as r3
import profile_r4 as r4

WINDS = Path(__file__).resolve().parents[1] / "orchestral-winds-bridge"


def actions(row, tag):
    return [(a.findtext("type"), int(a.findtext("param1")), int(a.findtext("param2")))
            for a in row.find(tag)]


class SchemaTwo(unittest.TestCase):
    def specs(self):
        sys.path.insert(0, str(WINDS))
        from profiles import CURRENT
        return [r4, *CURRENT.values()]

    def test_artifacts_are_current(self):
        for name, data in g.build(r4).items():
            self.assertEqual((g.HERE / "r4" / name).read_bytes(), data, name)

    def test_allocation_matches_r3(self):
        self.assertEqual(g.dimensions(r4), g.dimensions(r3))

    def test_typed_off_and_beacon(self):
        for spec in self.specs():
            xml = g.build(spec)[f"Fiddle-Orchestral-{getattr(spec, 'SECTION', 'Strings')}-r{spec.REVISION}.doricolib"]
            definition = ET.fromstring(xml).find(".//ExpressionMapDefinition")
            for addon in definition.find("techniqueAddOns"):
                (on_type, on_number, on), = actions(addon, "switchOnActions")
                (off_type, off_number, off), = actions(addon, "switchOffActions")
                self.assertEqual((on_type, on_number, off), (off_type, off_number, on + g.TYPED_OFF))
                self.assertNotIn(on_number if on_type == "kKeySwitch" else None, g.BEACON_PITCHES)
            init = actions(definition.find("initSwitchData"), "initActions")
            beacon = [(n, v) for t, n, v in init if t == "kKeySwitch" and n in g.BEACON_PITCHES]
            self.assertEqual(beacon, list(zip(g.BEACON_PITCHES, g.beacon(spec.ENTITY_ID, spec.REVISION))))
            self.assertTrue(all(1 <= v <= 127 for _, v in beacon))
            manifest = json.loads(g.build(spec)["allocation.json"])
            self.assertTrue(manifest["runtime_compatible"])
            self.assertEqual(manifest["conduit_budget"], 50)  # 52 minus two beacon pitches


if __name__ == "__main__":
    unittest.main()
