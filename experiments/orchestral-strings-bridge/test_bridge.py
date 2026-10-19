"""Structural/contract tests; these do not simulate Dorico switch selection."""
import copy
import json
import unittest
import xml.etree.ElementTree as ET

import generate as g
from profile import ALIASES, BASE_IDS, ENTITY_ID


class StringsBridgeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.outputs = g.build()
        cls.root = ET.fromstring(cls.outputs[g.MAP_FILE])
        cls.map = cls.root.find(".//ExpressionMapDefinition")
        cls.manifest = json.loads(cls.outputs["allocation.json"])
        cls.bases = cls.map.findall("playingTechniqueCombinations/playingTechniqueCombination")
        cls.addons = cls.map.findall("techniqueAddOns/techniqueAddOn")

    def test_generated_artifacts_current_and_deterministic(self):
        self.assertEqual(self.outputs, g.build())
        for name, content in self.outputs.items():
            self.assertEqual((g.HERE / name).read_bytes(), content, name)

    def test_isolated_identity_and_no_builtin_redefinitions(self):
        self.assertEqual(self.map.findtext("entityID"), ENTITY_ID)
        self.assertNotEqual(ENTITY_ID, "xmap.user.Fiddle.1")
        self.assertIsNone(self.root.find("playingTechniques"))
        self.assertFalse(self.manifest["runtime_compatible"])
        self.assertFalse(self.manifest["dorico_import_verified"])

    def test_budget_and_counts(self):
        c = self.manifest["counts"]
        self.assertEqual(len(self.bases), 25)
        self.assertEqual(len(self.addons), 106)
        self.assertEqual(c["switch_rows"], 131)
        self.assertEqual(c["conduits"], 50)
        self.assertEqual(c["spare_conduits"], 2)
        dims = self.manifest["dimensions"]
        self.assertEqual({d["number"] for d in dims if d["kind"] == "CC"} | {102}, set(range(102, 120)))
        self.assertEqual({d["number"] for d in dims if d["kind"] == "KS"}, set(range(21)) | set(range(115, 126)))
        self.assertEqual(len({(d["kind"], d["number"]) for d in dims}), 49)

    def test_bases_preserve_all_fields_except_owned_on_actions(self):
        originals = ET.parse(g.BASE_SOURCE).findall(".//playingTechniqueCombination")
        def signature(node):
            return (node.tag, tuple(sorted(node.attrib.items())), (node.text or "").strip(),
                    tuple(signature(child) for child in node if child.tag != "switchOnActions"))
        for index, (base, source) in enumerate(zip(self.bases, originals)):
            self.assertEqual(signature(base), signature(source))
            self.assertEqual(int(base.findtext("baseSwitchID")), index)
            on = base.findall("switchOnActions/switchOnAction")
            self.assertEqual(len(on), 1)
            self.assertEqual(on[0].findtext("type"), "kControlChange")
            self.assertEqual(on[0].findtext("param1"), "102")
            self.assertEqual(on[0].findtext("param2"), str(index))
            self.assertEqual(len(base.find("switchOffActions")), 0)
        self.assertEqual([b.findtext("conditionString") for b in self.bases[:5]],
                         [f"NoteLength == k{x}" for x in ("VeryShort", "Short", "Medium", "Long")] + ["NoteLength > kLong"])
        self.assertEqual({b.findtext("techniqueIDs") for b in self.bases[15:20]}, {"pt.staccato+pt.tenuto"})

    def test_exact_pt_coverage_and_ownership(self):
        pts = [a.findtext("techniqueIDs") for a in self.addons]
        self.assertEqual(len(pts), len(set(pts)))
        owners = g.validate(g.dimensions(), g.inventory())
        self.assertEqual(set(pts), owners.keys())
        self.assertEqual(len(set(pts) | BASE_IDS), 110)
        self.assertEqual(ALIASES, {"pt.bowed": "pt.natural"})
        for pt, row in g.inventory().items():
            status, reason = g.disposition(pt, row, owners)
            self.assertTrue(reason)
            if row["family"].startswith("S") and pt != "pt.harpHarmonic":
                self.assertIn(status, ("Base", "Add-on", "Alias"), pt)

    def test_on_off_and_init_agree_with_allocation(self):
        by_pt = {pt: (d, value) for d in g.dimensions() for pt, value in d["values"].items()}
        for index, addon in enumerate(self.addons):
            self.assertEqual(addon.findtext("techAddOnSwitchID"), str(index))
            self.assertIsNone(addon.find("conditionString"))
            d, value = by_pt[addon.findtext("techniqueIDs")]
            for phase, expected in (("switchOn", value), ("switchOff", d["unspecified"])):
                actions = list(addon.find(phase + "Actions"))
                self.assertEqual(len(actions), 1)
                a = actions[0]
                self.assertEqual(a.tag, phase + "Action")
                self.assertEqual(a.findtext("type"), "kControlChange" if d["kind"] == "CC" else "kKeySwitch")
                self.assertEqual(int(a.findtext("param1")), d["number"])
                self.assertEqual(int(a.findtext("param2")), expected)
                self.assertGreaterEqual(expected, 0 if d["kind"] == "CC" else 1)
        init = self.map.findall("initSwitchData/initActions/switchOnAction")
        expected = {("kControlChange", 102): 0}
        expected.update({("kControlChange" if d["kind"] == "CC" else "kKeySwitch", d["number"]): d["unspecified"] for d in g.dimensions()})
        self.assertEqual(len(init), 50)
        self.assertEqual({(a.findtext("type"), int(a.findtext("param1"))): int(a.findtext("param2")) for a in init}, expected)

    def test_groups_match_enums_not_independent_flags(self):
        groups = self.map.findall("mutualExclusionGroups/mutualExclusionGroup")
        self.assertEqual(len(groups), 22)
        self.assertEqual(len({g.findtext("groupID") for g in groups}), 22)
        members = [{pt.strip() for pt in group.findtext("techniqueIDs").split(",")} for group in groups]
        self.assertIn({"pt.staccato", "pt.staccatissimo"}, members)
        self.assertFalse(any("pt.tenuto" in group for group in members))
        for d in g.dimensions():
            expected = set(d["values"]) | ({"pt.natural"} if d["natural_cancels"] else set())
            if len(expected) > 1:
                self.assertIn(expected, members)
        self.assertFalse(any({"pt.fadeIn", "pt.fadeOut"} <= group for group in members))
        self.assertFalse(any({"pt.natural", "pt.muted"} <= group for group in members))
        self.assertEqual(self.map.findtext("autoMutualExclusion"), "false")

    def test_reviewed_stroke_and_pressure_groups(self):
        dims = {d["id"]: d for d in g.dimensions()}
        self.assertIn("pt.fouette", dims["bow_stroke"]["values"])
        self.assertTrue(dims["bow_stroke"]["natural_cancels"])
        self.assertEqual(set(dims["bow_pressure"]["values"]), {"pt.flautando", "pt.overpressure"})
        self.assertTrue(dims["bow_pressure"]["natural_cancels"])
        self.assertFalse({"fouette", "flautando", "overpressure"} & dims.keys())
        self.assertNotEqual(dims["bow_pressure"]["number"], dims["bow_contact"]["number"])

    def test_validation_rejects_bad_allocations(self):
        dims = g.dimensions()
        for mutation in ("conduit", "id", "pt", "value"):
            bad = copy.deepcopy(dims)
            if mutation == "conduit":
                bad[1]["number"] = bad[0]["number"]
            elif mutation == "id":
                bad[1]["id"] = bad[0]["id"]
            elif mutation == "pt":
                bad[1]["values"]["pt.notABuiltIn"] = 3
            else:
                bad[1]["values"]["pt.accent"] = 128
            with self.assertRaises(ValueError, msg=mutation):
                g.validate(bad, g.inventory())


if __name__ == "__main__":
    unittest.main()
