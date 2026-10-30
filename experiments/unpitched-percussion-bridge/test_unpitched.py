"""Unpitched-percussion profile contracts: artifacts current, shared conduits
identical to pitched percussion r2, schema 2, base dynamics as declared,
pitch codes musical and uniform across instruments, full inventory."""
import json
import unittest
import xml.etree.ElementTree as ET

import generate_unpitched as generator
from profiles import (BASES, CODES, COMMON_IDS, HIT_BASES, INSTRUMENTS, PITCHED_IDS, PROBE, ROLL_FAMILY,
                      UNPITCHED_R1 as spec, engine, pitched_dims)


class UnpitchedPercussion(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.outputs = engine.build(spec)
        cls.manifest = json.loads(cls.outputs["allocation.json"])
        cls.root = ET.fromstring(cls.outputs["Fiddle-Orchestral-UnpitchedPercussion-r1.doricolib"])
        cls.probe = engine.build(PROBE)

    def test_artifacts_are_current(self):
        for name, data in self.outputs.items():
            self.assertEqual((generator.HERE / "unpitched-percussion-r1" / name).read_bytes(), data, name)
        for name, data in self.probe.items():
            self.assertEqual((generator.HERE / "probe" / name).read_bytes(), data, name)

    def test_shared_dimensions_keep_pitched_wire(self):
        dims = {d["id"]: d for d in self.manifest["dimensions"]}
        for key in COMMON_IDS + PITCHED_IDS:
            self.assertEqual((dims[key]["kind"], dims[key]["number"], dims[key]["values"]),
                             (pitched_dims[key]["kind"], pitched_dims[key]["number"], pitched_dims[key]["values"]))
        # New mechanisms sit on conduits no pitched dimension uses.
        taken = {(d["kind"], d["number"]) for d in pitched_dims.values()}
        for key in ("snares", "throwoff", "choke"):
            self.assertNotIn((dims[key]["kind"], dims[key]["number"]), taken)

    def test_schema_two(self):
        self.assertEqual(self.manifest["wire_schema"], 2)
        for addon in self.root.iter("techniqueAddOn"):
            on = int(addon.find("switchOnActions/switchOnAction/param2").text)
            off = int(addon.find("switchOffActions/switchOffAction/param2").text)
            self.assertEqual(off, on + engine.TYPED_OFF)
        pitches = {int(a.findtext("param1")) for a in self.root.iter("switchOnAction") if a.findtext("type") == "kKeySwitch"}
        self.assertFalse(pitches & set(range(*engine.MUSICAL_RANGE)) - set(engine.BEACON_PITCHES))

    def test_bases_take_their_dynamics(self):
        rows = self.root.findall(".//playingTechniqueCombination")
        self.assertEqual(len(rows), 5 * len(BASES))
        for i, (ids, volume) in enumerate(BASES):
            for bucket in range(5):
                row = rows[5 * i + bucket]
                self.assertEqual(row.findtext("baseSwitchID"), str(5 * i + bucket))
                self.assertEqual(row.findtext("techniqueIDs"), ids)
                self.assertEqual(row.findtext("conditionString"), engine.LENGTH_CONDITIONS[bucket])
                self.assertEqual(row.findtext("volumeType/type"), "kNoteVelocity" if volume == "velocity" else "kCC")
                self.assertEqual(row.find("switchOnActions/switchOnAction/param2").text, str(5 * i + bucket))
        self.assertEqual([b["dynamics"] for b in self.manifest["bases"]],
                         ["velocity"] * 5 * len(HIT_BASES) + ["cc1"] * 5 * len(ROLL_FAMILY))
        # Staccato keeps Dorico's stock flag; the synthesized roll rows copy Natural's.
        flags = {r.findtext("techniqueIDs"): r.findtext("flags") for r in rows}
        self.assertEqual((flags["pt.staccato"], flags["pt.natural"], flags["pt.roll"]), ("1", "0", "0"))

    def test_pitch_codes(self):
        low, high = engine.MUSICAL_RANGE
        pitches = [pitch for pitch, *_ in CODES]
        self.assertEqual(len(set(pitches)), len(CODES))
        self.assertTrue(all(low <= p <= high for p in pitches))
        self.assertIn((36, "pt.natural"), CODES)
        catalog = engine.inventory()
        for _, ids in CODES:
            for pt in ids.split("+"):
                self.assertIn(pt, catalog)
        definitions = self.root.findall("drumKitNoteMapDefinitions/entities/DrumKitNoteMapDefinition")
        self.assertEqual(len(definitions), 1)
        definition = definitions[0]
        self.assertEqual(definition.findtext("entityID"), spec.PERCUSSION.ENTITY_ID)
        self.assertEqual(definition.findtext("version"), "1")
        entries = definition.findall("entries/entry")
        self.assertEqual(len(entries), len(INSTRUMENTS) * len(CODES))
        tables = {}
        for entry in entries:
            self.assertIsNone(entry.find("keyswitches"))
            tables.setdefault(entry.findtext("instrumentEntityID"), set()).add(
                (int(entry.findtext("pitch")), entry.findtext("techniqueIDs")))
        self.assertEqual(set(tables), set(INSTRUMENTS))
        self.assertEqual(set(tables.values().__iter__().__next__()), set(CODES))
        self.assertEqual(len({frozenset(t) for t in tables.values()}), 1)

    def test_roll_family_is_both_base_and_code(self):
        inventory = self.outputs["inventory.md"].decode()
        for pt in ROLL_FAMILY:
            self.assertIn(f"| `{pt}` |", inventory)
            self.assertRegex(inventory, rf"\| `{pt}` \|[^|]*\|[^|]*\| Pitch code \+ Base \|")
        self.assertRegex(inventory, r"\| `pt\.rim` \|[^|]*\|[^|]*\| Pitch code \|")
        self.assertEqual(self.manifest["counts"]["pitch_code_ids"], len({pt for _, ids in CODES for pt in ids.split("+")}))

    def test_full_inventory(self):
        self.assertEqual(self.outputs["inventory.md"].decode().count("\n| `pt."), 298)

    def test_probe_carries_two_maps_and_a_keyswitch_entry(self):
        root = ET.fromstring(self.probe["Fiddle-Orchestral-UnpitchedProbe-r1.doricolib"])
        maps = root.findall("drumKitNoteMapDefinitions/entities/DrumKitNoteMapDefinition")
        self.assertEqual([m.findtext("entityID") for m in maps],
                         ["drumkitmap.user.Fiddle.UnpitchedProbe.A", "drumkitmap.user.Fiddle.UnpitchedProbe.B"])
        self.assertEqual([len(m.findall("entries/entry")) for m in maps], [11, 1])
        rods = [e for e in maps[0].findall("entries/entry") if e.findtext("techniqueIDs") == "pt.beaterRods"]
        self.assertEqual((rods[0].findtext("pitch"), rods[0].findtext("keyswitches")), ("68", "15"))
        # Hit bases by velocity, roll bases on CC1; no shortening group without staccato bases.
        volumes = {r.findtext("techniqueIDs"): r.findtext("volumeType/type") for r in root.iter("playingTechniqueCombination")}
        self.assertEqual(volumes, {"pt.natural": "kNoteVelocity", "pt.roll": "kCC", "pt.tremolo": "kCC", "pt.buzzRoll": "kCC"})
        self.assertNotIn("pt.staccato", [g.findtext("techniqueIDs") for g in root.iter("mutualExclusionGroup")])

    def test_engine_rejects_bad_codes(self):
        from types import SimpleNamespace
        catalog = engine.inventory()
        def bad(codes):
            s = SimpleNamespace(PERCUSSION=SimpleNamespace(ENTITY_ID="x", INSTRUMENTS=["i"], CODES=codes,
                                                           FOR_SINGLE_INSTRUMENT=False))
            with self.assertRaises(ValueError):
                engine.validate_percussion(s, catalog)
        bad([(36, "pt.natural"), (36, "pt.rim")])          # duplicate pitch
        bad([(36, "pt.natural"), (20, "pt.rim")])          # keyswitch pitch
        bad([(36, "pt.natural"), (37, "pt.nonsense")])     # unknown ID
        bad([(36, "pt.natural"), (37, "pt.rim+pt.rim")])   # duplicate technique
        bad([(37, "pt.rim")])                              # no Natural
        bad([(36, "pt.natural"), (37, "pt.rim", 60)])      # keyswitch inside the musical range


if __name__ == "__main__":
    unittest.main()
