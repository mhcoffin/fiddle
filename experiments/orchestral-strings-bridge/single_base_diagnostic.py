#!/usr/bin/env python3
"""Derive an isolated single-base map from frozen r2; never install or overwrite r2."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import shared_overlap_score

HERE = Path(__file__).resolve().parent
SOURCE = "Fiddle-Orchestral-Strings-r2.doricolib"
SOURCE_SHA256 = "31e8826434ecda621459343e95725ca31e1c495053f309e9fc70f4c5b0b92647"
MAP_FILE = "Fiddle-Orchestral-Strings-r2-single-base-d1.doricolib"
EXPECTED_FILE = "shared-overlap-single-base-d1.expected.json"
NAME = "Fiddle Strings r2 - SINGLE BASE d1 - PROBE ONLY"
ENTITY_ID = "xmap.user.Fiddle.OrchestralStrings.Diagnostic.SingleBase.d1"


def build():
    source = (HERE / SOURCE).read_bytes()
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise ValueError("Frozen r2 source changed; review before regenerating diagnostic")
    root = ET.fromstring(source)
    definition = root.find(".//ExpressionMapDefinition")
    definition.find("name").text = NAME
    definition.find("entityID").text = ENTITY_ID
    definition.find("version").text = "1"
    definition.find("description").text = (
        "DIAGNOSTIC d1 ONLY: silent Fiddle MIDI Probe, not production Fiddle. "
        "Derived from frozen strings r2; keeps Natural base 2 with its length "
        "condition removed, omits the other 24 bases. All add-ons, On/Off actions, "
        "Init and mutex groups unchanged. Does not transmit note-length categories "
        "or other base articulations. Source SHA256: " + SOURCE_SHA256)
    bases = definition.find("playingTechniqueCombinations")
    retained = copy.deepcopy(next(b for b in bases if b.findtext("baseSwitchID") == "2"))
    if retained.findtext("techniqueIDs") != "pt.natural":
        raise ValueError("Expected Natural base 2")
    # Empty condition is an unconditional switch. Keep its ID, CC102=2,
    # note-length factor, and every other property unchanged.
    retained.find("conditionString").text = None
    for base in list(bases):
        bases.remove(base)
    bases.append(retained)
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def artifacts():
    xml = build()
    expected = shared_overlap_score.expectations()
    expected.update(test_id="shared-overlap-single-base-d1", map_entity_id=ENTITY_ID,
                    map_revision=1, map_sha256=hashlib.sha256(xml).hexdigest(),
                    source_map=SOURCE, source_map_sha256=SOURCE_SHA256,
                    score_file="shared-overlap-r2.musicxml",
                    score_sha256=hashlib.sha256((HERE / "shared-overlap-r2.musicxml").read_bytes()).hexdigest(),
                    reference_allocation="allocation.json",
                    base_value_during_notes=2,
                    unchanged_addon_allocation=True,
                    caveat="Init CC102=0 is retained; every musical note must observe base 2. This diagnostic cannot test original note-length/base-articulation coverage.")
    return {MAP_FILE: xml, EXPECTED_FILE: (json.dumps(expected, indent=2) + "\n").encode()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for name, data in artifacts().items():
        path = HERE / name
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                parser.error(f"Stale/missing artifact: {path}")
        else:
            path.write_bytes(data)
    print("Checked single-base diagnostic" if args.check else "Generated single-base diagnostic")


if __name__ == "__main__":
    main()
