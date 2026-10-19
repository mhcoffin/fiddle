#!/usr/bin/env python3
"""Derive an isolated two-base map from frozen r2; never install or overwrite r2."""
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
MAP_FILE = "Fiddle-Orchestral-Strings-r2-two-base-d2.doricolib"
EXPECTED_FILE = "shared-overlap-two-base-d2.expected.json"
NAME = "Fiddle Strings r2 - TWO BASE d2 - PROBE ONLY"
ENTITY_ID = "xmap.user.Fiddle.OrchestralStrings.Diagnostic.TwoBase.d2"


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
        "DIAGNOSTIC d2 ONLY: silent Fiddle MIDI Probe, not production Fiddle. "
        "Retains frozen r2 Natural bases 2 (Medium) and 4 (>Long), including "
        "their original conditions and actions; omits the other 23 bases. "
        "All add-ons, On/Off, Init and mutex data unchanged. No base covers "
        "VeryShort, Short or Long; use only the documented shared-overlap fixture. "
        "Source SHA256: " + SOURCE_SHA256)
    bases = definition.find("playingTechniqueCombinations")
    retained = [copy.deepcopy(b) for b in bases if b.findtext("baseSwitchID") in ("2", "4")]
    if len(retained) != 2 or any(b.findtext("techniqueIDs") != "pt.natural" for b in retained):
        raise ValueError("Expected Natural bases 2 and 4")
    for base in list(bases):
        bases.remove(base)
    bases.extend(retained)
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def artifacts():
    xml = build()
    expected = shared_overlap_score.expectations()
    expected.update(test_id="shared-overlap-two-base-d2", map_entity_id=ENTITY_ID,
                    map_revision=1, map_sha256=hashlib.sha256(xml).hexdigest(),
                    source_map=SOURCE, source_map_sha256=SOURCE_SHA256,
                    score_file="shared-overlap-r2.musicxml",
                    score_sha256=hashlib.sha256((HERE / "shared-overlap-r2.musicxml").read_bytes()).hexdigest(),
                    reference_allocation="allocation.json",
                    allowed_base_values=[2, 4],
                    expected_base_transition="2 to 4 near bar 2; inspect full stream and per-note state",
                    unchanged_addon_allocation=True,
                    caveat="Init CC102=0 retained. Only Medium and >Long Natural bases exist. Shared-channel state during overlap need not follow each note duration; inspect actual transitions. Missing categories are outside this diagnostic.")
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
    print("Checked two-base diagnostic" if args.check else "Generated two-base diagnostic")


if __name__ == "__main__":
    main()
