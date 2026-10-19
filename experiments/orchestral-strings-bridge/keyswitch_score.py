#!/usr/bin/env python3
"""Generate the r2 active-KS scaffold; text labels are NOT active PTs.

Uses the unchanged map with built-in Sons xylophoniques, Strum up/down and
Vibrato. No runtime integration or installation. See keyswitch-test.md.
"""
import argparse
import hashlib
import json
from pathlib import Path

from generate import MAP_FILE
from profile import ENTITY_ID, REVISION
from smoke_score import scaffold

HERE = Path(__file__).resolve().parent
MARKS = {
    1: "A: baseline",
    2: "Xylo (persistent)",
    3: "Strum Up (this note)",
    4: "Strum Down (this note)",
    5: "B: no new marking; strum expires",
    6: "Vibrato (persistent)",
    7: "Strum Up (this note)",
    8: "Strum Down (this note)",
    9: "C: Natural (clear all)",
    10: "Strum Up (this note)",
    11: "Strum Down (this note)",
    12: "No marking; strum expires",
}


def expectations():
    notes = []
    for number in range(1, 13):
        pts = set()
        if 2 <= number <= 8:
            pts.add("pt.sonsXylophoniques")
        if 6 <= number <= 8:
            pts.add("pt.vibrato")
        if number in (3, 7, 10):
            pts.add("pt.strumUp")
        if number in (4, 8, 11):
            pts.add("pt.strumDown")
        notes.append(dict(note=number, bar=(number - 1) // 4 + 1,
                          beat=(number - 1) % 4 + 1, quarter_offset=number - 1,
                          pitch=72, techniques=sorted(pts)))
    return dict(schema_version=1, test_id="active-keyswitches-r2", map_entity_id=ENTITY_ID,
                map_revision=REVISION,
                map_sha256=hashlib.sha256((HERE / MAP_FILE).read_bytes()).hexdigest(),
                status="desired-states-not-observed-results",
                natural_base_normalized_to_empty=True, length_category_asserted=False,
                unlisted_addon_dimensions="unspecified; missing initialization is unknown, not zero",
                notes=notes)


def artifacts():
    # Preserve the completed marked fixture. Future tests must omit marks and
    # put all entry instructions in their Markdown guide (user preference).
    return {f"keyswitch-r{REVISION}.musicxml": scaffold(
                title=f"Strings bridge r{REVISION}: active keyswitch smoke test",
                marks=MARKS, bars=3),
            f"keyswitch-r{REVISION}.expected.json":
                (json.dumps(expectations(), indent=2) + "\n").encode()}


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
    print("Checked keyswitch scaffold" if args.check else "Generated keyswitch scaffold")


if __name__ == "__main__":
    main()
