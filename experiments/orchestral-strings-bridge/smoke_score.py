#!/usr/bin/env python3
"""Build a 16-note scaffold and expectations, not a Dorico performance result.

All score words are instructions, NOT active playing techniques. The user must
enter the actual PTs in Dorico as described in smoke-test.md. No map changes,
installation, endpoint assignment or runtime decoder integration.
"""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

from generate import MAP_FILE, sub
from profile import ENTITY_ID, REVISION

HERE = Path(__file__).resolve().parent
MARKS = {
    1: "A: baseline",
    3: "Flautando: notes 3-4 ONLY",
    5: "B: no new marking; check expiry",
    7: "Overpressure",
    9: "C: Non vibrato + Sul tasto",
    11: "Flautando (replace pressure)",
    13: "D: Fouette (this note)",
    14: "Detache",
    15: "Natural",
    16: "Vibrato",
}


def expectations():
    rows = []
    for number in range(1, 17):
        pts = set()
        if number in (3, 4) or 11 <= number <= 14:
            pts.add("pt.flautando")
        if 7 <= number <= 10:
            pts.add("pt.overpressure")
        if 9 <= number <= 15:
            pts.add("pt.nonVibrato")
        if 9 <= number <= 14:
            pts.add("pt.sulTasto")
        if number == 13:
            pts.add("pt.fouette")
        if number == 14:
            pts.add("pt.detache")
        if number == 16:
            pts.add("pt.vibrato")
        rows.append(dict(note=number, bar=(number - 1) // 4 + 1,
                         beat=(number - 1) % 4 + 1, pitch=72,
                         quarter_offset=number - 1, techniques=sorted(pts)))
    return dict(schema_version=1, map_entity_id=ENTITY_ID, map_revision=REVISION,
                map_sha256=hashlib.sha256((HERE / MAP_FILE).read_bytes()).hexdigest(),
                status="desired-states-not-observed-results",
                natural_base_normalized_to_empty=True,
                length_category_asserted=False,
                unlisted_addon_dimensions="unspecified; missing initialization is unknown, not zero",
                notes=rows)


def scaffold(title=None, marks=None, bars=4):
    # New scores omit instructional text: it imports poorly in Dorico.
    # Historical fixtures explicitly opt in below for byte-for-byte preservation.
    marks = {} if marks is None else marks
    root = ET.Element("score-partwise", version="3.1")
    sub(sub(root, "work"), "work-title", title or f"Strings bridge r{REVISION}: cancellation smoke test")
    score_part = sub(sub(root, "part-list"), "score-part", id="P1")
    sub(score_part, "part-name", "Violin")
    instrument = sub(score_part, "score-instrument", id="P1-I1")
    sub(instrument, "instrument-name", "Violin")
    midi = sub(score_part, "midi-instrument", id="P1-I1")
    sub(midi, "midi-channel", 1)
    sub(midi, "midi-program", 41)
    part = sub(root, "part", id="P1")
    for bar in range(1, bars + 1):
        measure = sub(part, "measure", number=str(bar))
        if bar == 1:
            attributes = sub(measure, "attributes")
            sub(attributes, "divisions", 1)
            sub(sub(attributes, "key"), "fifths", 0)
            meter = sub(attributes, "time")
            sub(meter, "beats", 4)
            sub(meter, "beat-type", 4)
            clef = sub(attributes, "clef")
            sub(clef, "sign", "G")
            sub(clef, "line", 2)
            tempo = sub(measure, "direction", placement="above")
            metronome = sub(sub(tempo, "direction-type"), "metronome")
            sub(metronome, "beat-unit", "quarter")
            sub(metronome, "per-minute", 80)
            sub(tempo, "sound", tempo="80")
        for beat in range(1, 5):
            number = (bar - 1) * 4 + beat
            if number in marks:
                sub(sub(sub(measure, "direction", placement="above"),
                        "direction-type"), "words", marks[number])
            note = sub(measure, "note")
            pitch = sub(note, "pitch")
            sub(pitch, "step", "C")
            sub(pitch, "octave", 5)
            sub(note, "duration", 1)
            sub(note, "type", "quarter")
        if bar == bars:
            sub(sub(measure, "barline", location="right"), "bar-style", "light-heavy")
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def artifacts():
    return {f"smoke-r{REVISION}.musicxml": scaffold(marks=MARKS),
            f"smoke-r{REVISION}.expected.json": (json.dumps(expectations(), indent=2) + "\n").encode()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for name, content in artifacts().items():
        path = HERE / name
        if args.check:
            if not path.exists() or path.read_bytes() != content:
                parser.error(f"Stale/missing artifact: {path}")
        else:
            path.write_bytes(content)
    print("Checked smoke scaffold" if args.check else "Generated smoke scaffold")


if __name__ == "__main__":
    main()
