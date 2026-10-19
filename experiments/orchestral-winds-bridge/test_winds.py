"""Profile, allocation and reproducibility contracts; no Dorico simulation."""
import copy
import hashlib
import json
import unittest
import xml.etree.ElementTree as ET

import generate_winds as generator
from profiles import PROFILES, strings

engine = generator.engine


def signature(node):
    return (node.tag, node.attrib, (node.text or "").strip(), [signature(c) for c in node])


class WindBridgeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.outputs = {key: generator.artifacts(key) for key in PROFILES}
        cls.manifests = {key: json.loads(files["allocation.json"]) for key, files in cls.outputs.items()}

    def definition(self, key):
        spec = PROFILES[key]
        name = f"Fiddle-Orchestral-{spec.SECTION}-r1.doricolib"
        return ET.fromstring(self.outputs[key][name]).find(".//ExpressionMapDefinition")

    def test_artifacts_are_current_and_deterministic(self):
        for key, outputs in self.outputs.items():
            self.assertEqual(outputs, generator.artifacts(key))
            for name, data in outputs.items():
                self.assertEqual((generator.HERE / f"{key}-r1" / name).read_bytes(), data)
            spec = PROFILES[key]
            manifest = self.manifests[key]
            self.assertEqual(manifest["map_sha256"], hashlib.sha256(outputs[f"Fiddle-Orchestral-{spec.SECTION}-r1.doricolib"]).hexdigest())
            self.assertFalse(manifest["runtime_compatible"])
            self.assertFalse(manifest["dorico_import_verified"])
            self.assertEqual(manifest["instruments"], spec.INSTRUMENTS)
            self.assertIn("one Dorico voice", manifest["supported_routing"])

    def test_all_map_and_group_identities_are_distinct(self):
        identities, group_ids = {strings.ENTITY_ID}, set()
        for key, spec in PROFILES.items():
            definition = self.definition(key)
            self.assertNotIn(definition.findtext("entityID"), identities)
            identities.add(definition.findtext("entityID"))
            self.assertEqual(definition.findtext("entityID"), spec.ENTITY_ID)
            self.assertIn(spec.SECTION, definition.findtext("name"))
            self.assertIn(spec.LABEL, definition.findtext("description"))
            self.assertNotIn("Bowed orchestral strings", definition.findtext("description"))
            for group in definition.findall("mutualExclusionGroups/mutualExclusionGroup"):
                gid = group.findtext("groupID")
                self.assertNotIn(gid, group_ids)
                group_ids.add(gid)
                self.assertTrue(gid.startswith(f"ptmg.user.Fiddle.Orchestral{spec.SECTION}.r1."))

    def test_base_rows_retain_original_articulation_length_and_timing(self):
        reference = ET.parse(engine.HERE / "r3/Fiddle-Orchestral-Strings-r3.doricolib")
        bases = reference.find(".//playingTechniqueCombinations")
        self.assertEqual(len(bases), 25)
        for key in PROFILES:
            self.assertEqual(signature(self.definition(key).find("playingTechniqueCombinations")), signature(bases))

    def test_every_catalog_id_is_accounted_for_and_section_scope_is_explicit(self):
        catalog = engine.inventory()
        self.assertEqual(len(catalog), 298)
        owners = {}
        for key, spec in PROFILES.items():
            owners[key] = engine.validate(engine.dimensions(spec), catalog, spec)
            for pt, row in catalog.items():
                status, reason = engine.disposition(pt, row, owners[key], spec)
                self.assertIn(status, ("Base", "Add-on", "Out of scope"))
                self.assertTrue(reason, (key, pt))
            self.assertFalse({"pt.guitarPizzicato", "pt.pinchHarmonic", "pt.vibratoBarDip",
                              "pt.nasalVoice", "pt.spoken", "pt.sustainPedal"} & owners[key].keys())
        self.assertTrue({"pt.keyClick", "pt.keyClickWithPitch", "pt.slapTongue", "pt.jetWhistle",
                         "pt.whistleTone", "pt.naturalHarmonic1"} <= owners["woodwinds"].keys())
        self.assertFalse({"pt.stopped", "pt.plungerMute", "pt.straightMute"} & owners["woodwinds"].keys())
        self.assertTrue({"pt.stopped", "pt.cuivre", "pt.plungerMute", "pt.straightMute",
                         "pt.harmonMuteStemIn", "pt.harmonMuteStemOut", "pt.harmonMuteStemExtended",
                         "pt.shake", "pt.smear", "pt.rip"} <= owners["brass"].keys())
        self.assertFalse({"pt.jetWhistle", "pt.keyClick", "pt.whistleTone"} & owners["brass"].keys())

    def test_shared_pt_wire_encoding_matches_across_current_sections(self):
        profiles = {"strings": strings, **PROFILES}
        seen = {}
        for key, spec in profiles.items():
            for d in engine.dimensions(spec):
                for pt, value in d["values"].items():
                    wire = (d["id"], d["kind"], d["number"], d["unspecified"], value)
                    if pt in seen:
                        self.assertEqual(wire, seen[pt], (key, pt))
                    seen[pt] = wire

    def test_actions_initialize_and_reset_only_allocated_dimensions(self):
        for key, spec in PROFILES.items():
            definition = self.definition(key)
            dims = engine.dimensions(spec)
            counts = self.manifests[key]["counts"]
            by_pt = {pt: (d, v) for d in dims for pt, v in d["values"].items()}
            expected_counts = (35, 64, 36, 16) if key == "woodwinds" else (36, 71, 37, 15)
            self.assertEqual((counts["addon_dimensions"], counts["add_on_switches"], counts["conduits"], counts["spare_conduits"]), expected_counts)
            self.assertEqual(counts["cc_conduits"], 18)
            self.assertEqual(len({(d["kind"], d["number"]) for d in dims}), len(dims))
            addons = definition.findall("techniqueAddOns/techniqueAddOn")
            self.assertEqual({a.findtext("techniqueIDs") for a in addons}, by_pt.keys())
            for index, addon in enumerate(addons):
                self.assertEqual(addon.findtext("techAddOnSwitchID"), str(index))
                d, value = by_pt[addon.findtext("techniqueIDs")]
                for phase, expected in (("On", value), ("Off", d["unspecified"])):
                    actions = list(addon.find(f"switch{phase}Actions"))
                    self.assertEqual(len(actions), 1)
                    a = actions[0]
                    self.assertEqual(a.findtext("type"), "kControlChange" if d["kind"] == "CC" else "kKeySwitch")
                    self.assertEqual((int(a.findtext("param1")), int(a.findtext("param2"))), (d["number"], expected))
            expected_init = {("kControlChange", 102): 0}
            expected_init.update({("kControlChange" if d["kind"] == "CC" else "kKeySwitch", d["number"]): d["unspecified"] for d in dims})
            init = definition.findall("initSwitchData/initActions/switchOnAction")
            self.assertEqual(len(init), counts["conduits"])
            self.assertEqual({(a.findtext("type"), int(a.findtext("param1"))): int(a.findtext("param2")) for a in init}, expected_init)

    def test_mutex_groups_keep_independent_controls_separate(self):
        for key in PROFILES:
            definition = self.definition(key)
            groups = [{pt.strip() for pt in g.findtext("techniqueIDs").split(",")}
                      for g in definition.findall("mutualExclusionGroups/mutualExclusionGroup")]
            self.assertEqual(len(groups), 14)
            self.assertEqual(definition.findtext("autoMutualExclusion"), "false")
            self.assertIn({"pt.doubleTongue", "pt.tripleTongue"}, groups)
            self.assertFalse(any({"pt.doubleTongue", "pt.flutterTongue"} <= group for group in groups))
            self.assertFalse(any({"pt.fadeIn", "pt.fadeOut"} <= group for group in groups))
            self.assertFalse(any("pt.tenuto" in group for group in groups))
            if key == "woodwinds":
                self.assertIn({"pt.keyClick", "pt.keyClickWithPitch"}, groups)
            else:
                self.assertIn({"pt.open1", "pt.stopped"}, groups)
                self.assertIn({"pt.open1", "pt.plungerMute"}, groups)
                self.assertFalse(any({"pt.straightMute", "pt.plungerMute"} <= group for group in groups))
                self.assertFalse(any({"pt.cuivre", "pt.stopped"} <= group for group in groups))

    def test_bad_values_conduits_and_groups_are_rejected(self):
        spec = copy.deepcopy(PROFILES["woodwinds"])
        spec.CONDUITS["tonguing"] = ("KS", 60)
        with self.assertRaises(ValueError):
            engine.dimensions(spec)
        spec = PROFILES["woodwinds"]
        dims = engine.dimensions(spec)
        dims[0]["values"]["pt.nonLegato"] = dims[0]["values"]["pt.legato"]
        with self.assertRaises(ValueError):
            engine.validate(dims, engine.inventory(), spec)
        spec = copy.deepcopy(PROFILES["brass"])
        spec.EXTRA_MUTEX_GROUPS.append(("bad", ["pt.open1", "pt.keyClick"]))
        with self.assertRaises(ValueError):
            engine.validate(engine.dimensions(spec), engine.inventory(), spec)


if __name__ == "__main__":
    unittest.main()
