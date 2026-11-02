"""Written-duration revisions: r4/r2/r1 plus Dorico duration options pinned to 100%."""
import importlib.util
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

import generate as g
import profile_r4 as r4
import profile_r5 as r5

EXPERIMENTS = Path(__file__).resolve().parents[1]


def load(directory, module):
    spec = importlib.util.spec_from_file_location(
        f"{directory}_{module}", EXPERIMENTS / directory / f"{module}.py")
    loaded = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(EXPERIMENTS / directory))
    spec.loader.exec_module(loaded)
    return loaded


def overrides(xml):
    definition = ET.fromstring(xml).find(".//ExpressionMapDefinition")
    return {row.findtext("option"): row.findtext("value")
            for row in definition.find("playbackOptionsOverrides")}


def map_xml(outputs):
    return next(data for name, data in outputs.items() if name.endswith(".doricolib"))


class WrittenDurations(unittest.TestCase):
    def pairs(self):
        """(current pinned spec, frozen predecessor, artifact directory)."""
        winds = load("orchestral-winds-bridge", "profiles")
        percussion = load("orchestral-percussion-bridge", "profiles")
        result = [(r5, r4, g.HERE / "r5")]
        for key in ("woodwinds", "brass"):
            result.append((winds.CURRENT[key], winds.SCHEMA2[key],
                           EXPERIMENTS / "orchestral-winds-bridge" / f"{key}-r3"))
        result.append((percussion.PITCHED_PERCUSSION_R2, percussion.PITCHED_PERCUSSION,
                       EXPERIMENTS / "orchestral-percussion-bridge" / "pitched-percussion-r2"))
        return result

    def test_artifacts_are_current(self):
        for spec, _, directory in self.pairs():
            for name, data in g.build(spec).items():
                self.assertEqual((directory / name).read_bytes(), data, name)

    def test_pinned_playback_options(self):
        expected = {option: "int: 100" for option in g.DURATION_OPTIONS}
        self.assertEqual(len(expected), 6)
        expected["timingOptions.alwaysTruncateAbuttingNotesOfSamePitch"] = "bool: false"
        expected["timingOptions.numTicksBeforeForSwitches"] = "int: 10"
        expected["dynamicsOptions.dynamicCurveParams"] = 'string: "0.500000,1.000000,1.000000"'
        expected["dynamicsOptions.minProfileLevelForPlayback"] = "int: -8"
        expected["dynamicsOptions.maxProfileLevelForPlayback"] = "int: 8"
        for option in ("accent", "marcato", "tenuto", "ghostNote"):
            expected[f"dynamicsOptions.{option}DynamicDelta"] = "double: 0"
        for spec, previous, _ in self.pairs():
            self.assertEqual(overrides(map_xml(g.build(spec))), expected, spec.NAME)
            self.assertEqual(overrides(map_xml(g.build(previous))), {}, previous.NAME)

    def test_only_identity_and_overrides_change(self):
        for spec, previous, _ in self.pairs():
            self.assertEqual(spec.REVISION, previous.REVISION + 1)
            self.assertNotEqual(spec.ENTITY_ID, previous.ENTITY_ID)
            self.assertEqual(g.dimensions(spec), g.dimensions(previous))
            self.assertEqual(g.wire_schema(spec), 2)
            self.assertTrue(g.build(spec)["allocation.json"].decode().count('"written_durations": true'))


if __name__ == "__main__":
    unittest.main()
