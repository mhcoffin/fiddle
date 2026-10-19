import copy
import hashlib
import json
import unittest
import xml.etree.ElementTree as ET

import shared_overlap_score
import two_base_diagnostic as d


def signature(node):
    return (node.tag, node.attrib, (node.text or "").strip(), [signature(c) for c in node])


class TwoBaseDiagnosticTest(unittest.TestCase):
    def setUp(self):
        self.source = ET.parse(d.HERE / d.SOURCE).find(".//ExpressionMapDefinition")
        self.variant = ET.fromstring(d.build()).find(".//ExpressionMapDefinition")

    def test_artifacts_and_frozen_source(self):
        self.assertEqual(hashlib.sha256((d.HERE / d.SOURCE).read_bytes()).hexdigest(), d.SOURCE_SHA256)
        for name, data in d.artifacts().items():
            self.assertEqual((d.HERE / name).read_bytes(), data)

    def test_retained_bases_are_exact_originals(self):
        bases = list(self.variant.find("playingTechniqueCombinations"))
        self.assertEqual([b.findtext("baseSwitchID") for b in bases], ["2", "4"])
        for base, condition in zip(bases, ("NoteLength == kMedium", "NoteLength > kLong")):
            original = next(b for b in self.source.find("playingTechniqueCombinations")
                            if b.findtext("baseSwitchID") == base.findtext("baseSwitchID"))
            self.assertEqual(signature(base), signature(original))
            self.assertEqual(base.findtext("conditionString"), condition)
            self.assertEqual(base.findtext("switchOnActions/switchOnAction/param1"), "102")
            self.assertEqual(base.findtext("switchOnActions/switchOnAction/param2"), base.findtext("baseSwitchID"))

    def test_every_other_behavior_is_unchanged(self):
        excluded = {"name", "entityID", "version", "description", "playingTechniqueCombinations"}
        self.assertEqual([signature(c) for c in self.source if c.tag not in excluded],
                         [signature(c) for c in self.variant if c.tag not in excluded])
        self.assertEqual(len(self.variant.findall("techniqueAddOns/techniqueAddOn")), 106)
        addon = next(a for a in self.variant.find("techniqueAddOns") if a.findtext("techniqueIDs") == "pt.flautando")
        for phase, value in (("On", "1"), ("Off", "0")):
            action = addon.find(f"switch{phase}Actions/switch{phase}Action")
            self.assertEqual(action.findtext("type"), "kControlChange")
            self.assertEqual(action.findtext("param1"), "112")
            self.assertEqual(action.findtext("param2"), value)

    def test_distinct_identity_and_honest_description(self):
        self.assertEqual(self.variant.findtext("entityID"), d.ENTITY_ID)
        self.assertNotEqual(self.variant.findtext("entityID"), self.source.findtext("entityID"))
        self.assertEqual(self.variant.findtext("name"), d.NAME)
        self.assertIn("PROBE ONLY", d.NAME)
        self.assertIn(d.SOURCE_SHA256, self.variant.findtext("description"))

    def test_expectations_bound_to_new_map_same_score(self):
        artifacts = d.artifacts()
        expected = json.loads(artifacts[d.EXPECTED_FILE])
        original = shared_overlap_score.expectations()
        for key in ("notes", "controls", "overlap_probes"):
            self.assertEqual(expected[key], original[key])
        self.assertEqual(expected["map_entity_id"], d.ENTITY_ID)
        self.assertEqual(expected["map_sha256"], hashlib.sha256(artifacts[d.MAP_FILE]).hexdigest())
        self.assertEqual(expected["score_sha256"], hashlib.sha256(shared_overlap_score.scaffold()).hexdigest())
        self.assertEqual(expected["allowed_base_values"], [2, 4])


if __name__ == "__main__":
    unittest.main()
