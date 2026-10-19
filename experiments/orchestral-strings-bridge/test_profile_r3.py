"""Curated scope and revision-isolation contracts, not a host simulation."""
import hashlib
import json
import unittest
import xml.etree.ElementTree as ET

import generate as g
import profile as r2
import profile_r3 as r3


def signature(node):
    return (node.tag, node.attrib, (node.text or "").strip(), [signature(c) for c in node])


class CuratedStringsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.outputs = g.build(r3)
        cls.manifest = json.loads(cls.outputs["allocation.json"])
        cls.definition = ET.fromstring(cls.outputs["Fiddle-Orchestral-Strings-r3.doricolib"]).find(".//ExpressionMapDefinition")

    def test_current_artifacts_and_new_identity(self):
        self.assertEqual(self.outputs, g.build(r3))
        for name, data in self.outputs.items():
            self.assertEqual((g.HERE / "r3" / name).read_bytes(), data, name)
        self.assertEqual(self.definition.findtext("entityID"), r3.ENTITY_ID)
        self.assertNotEqual(r3.ENTITY_ID, r2.ENTITY_ID)
        self.assertEqual(self.definition.findtext("version"), "3")
        self.assertEqual(self.manifest["map_sha256"], hashlib.sha256(self.outputs["Fiddle-Orchestral-Strings-r3.doricolib"]).hexdigest())
        self.assertFalse(self.manifest["runtime_compatible"])
        self.assertFalse(self.manifest["dorico_import_verified"])

    def test_frozen_r2_artifacts_still_reproduce(self):
        for name, data in g.build().items():
            self.assertEqual((g.HERE / name).read_bytes(), data, name)
        self.assertEqual(hashlib.sha256((g.HERE / g.MAP_FILE).read_bytes()).hexdigest(),
                         "31e8826434ecda621459343e95725ca31e1c495053f309e9fc70f4c5b0b92647")

    def test_removed_pt_set_has_no_unintentional_losses(self):
        old = g.validate(g.dimensions(), g.inventory())
        current = g.validate(g.dimensions(r3), g.inventory(), r3)
        removed = set(r3.DEFERRED_VALUES)
        removed.update("pt." + pt for key, members, _, _ in r2.DIMENSIONS
                       if key in r3.DEFERRED_DIMENSIONS for pt in members.split())
        self.assertEqual(len(removed), 19)
        self.assertEqual(old.keys() - current.keys(), removed)
        self.assertFalse(current.keys() - old.keys())
        for pt, row in g.inventory().items():
            disposition, reason = g.disposition(pt, row, current, r3)
            self.assertTrue(reason, pt)
            if pt in removed:
                self.assertEqual(disposition, "Out of scope", pt)
                self.assertEqual(reason, r3.EXCLUSIONS[pt])
        self.assertTrue({"pt.nailPizzicato", "pt.pizzicatoAllaChitarra", "pt.buzzPizzicato",
                         "pt.strumUp", "pt.strumDown", "pt.glissandoUp", "pt.portamentoDown",
                         "pt.muffledStrings", "pt.damp", "pt.laissezVibrer"} <= current.keys())

    def test_budget_savings_and_no_dead_conduits(self):
        counts = self.manifest["counts"]
        self.assertEqual(counts["addon_dimensions"], 32)
        self.assertEqual(counts["add_on_switches"], 87)
        self.assertEqual(counts["conduits"], 33)
        self.assertEqual(counts["spare_conduits"], 19)
        dims = self.manifest["dimensions"]
        self.assertEqual(sum(len(d["values"]) > 1 for d in dims), 19)
        self.assertEqual(sum(len(d["values"]) == 1 for d in dims), 13)
        self.assertEqual({d["number"] for d in dims if d["kind"] == "CC"}, set(range(103, 120)))
        self.assertEqual({d["number"] for d in dims if d["kind"] == "KS"}, set(range(15)))
        init = self.definition.findall("initSwitchData/initActions/switchOnAction")
        expected = {("kControlChange", 102): 0}
        expected.update({("kControlChange" if d["kind"] == "CC" else "kKeySwitch", d["number"]): d["unspecified"] for d in dims})
        self.assertEqual(len(init), 33)
        self.assertEqual({(a.findtext("type"), int(a.findtext("param1"))): int(a.findtext("param2")) for a in init}, expected)
        by_pt = {pt: (d, value) for d in dims for pt, value in d["values"].items()}
        addons = self.definition.findall("techniqueAddOns/techniqueAddOn")
        self.assertEqual({a.findtext("techniqueIDs") for a in addons}, by_pt.keys())
        for addon in addons:
            d, on = by_pt[addon.findtext("techniqueIDs")]
            for phase, value in (("On", on), ("Off", d["unspecified"])):
                actions = addon.findall(f"switch{phase}Actions/switch{phase}Action")
                self.assertEqual(len(actions), 1)
                action = actions[0]
                self.assertEqual(action.findtext("type"), "kControlChange" if d["kind"] == "CC" else "kKeySwitch")
                self.assertEqual((int(action.findtext("param1")), int(action.findtext("param2"))), (d["number"], value))

    def test_base_and_retained_groups_keep_reviewed_semantics(self):
        original = ET.parse(g.HERE / g.MAP_FILE).find(".//ExpressionMapDefinition")
        self.assertEqual(signature(self.definition.find("playingTechniqueCombinations")),
                         signature(original.find("playingTechniqueCombinations")))
        old_dims = {d["id"]: d for d in g.dimensions()}
        for current in g.dimensions(r3):
            old = old_dims[current["id"]]
            self.assertEqual(current["natural_cancels"], old["natural_cancels"])
            self.assertEqual(set(current["values"]), set(old["values"]) - r3.DEFERRED_VALUES.keys())
        groups = self.definition.findall("mutualExclusionGroups/mutualExclusionGroup")
        actual = {frozenset(pt.strip() for pt in group.findtext("techniqueIDs").split(",")) for group in groups}
        expected = {frozenset(("pt.staccato", "pt.staccatissimo"))}
        for d in g.dimensions(r3):
            pts = set(d["values"]) | ({"pt.natural"} if d["natural_cancels"] else set())
            if len(pts) > 1:
                expected.add(frozenset(pts))
        self.assertEqual(actual, expected)
        self.assertEqual(len(groups), 22)
        self.assertEqual(self.definition.findtext("autoMutualExclusion"), "false")


if __name__ == "__main__":
    unittest.main()
