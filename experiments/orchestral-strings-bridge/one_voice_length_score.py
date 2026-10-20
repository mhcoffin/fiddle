#!/usr/bin/env python3
"""One-voice note-length fixture for strings r4: a persistent add-on across
changing note lengths, then explicit cancellation. Marker-free MusicXML."""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import profile_r4 as spec
from generate import sub

HERE = Path(__file__).resolve().parent
STEM = f"one-voice-length-r{spec.REVISION}"
MAP = HERE / f"r{spec.REVISION}" / f"Fiddle-Orchestral-Strings-r{spec.REVISION}.doricolib"
# (bar, [(step, octave, eighths)]); divisions = 2 per quarter.
BARS = [
    (1, [("C", 5, 2), ("D", 5, 2), ("E", 5, 2), ("F", 5, 2)]),
    (2, [("G", 5, 4), ("E", 5, 4)]),
    (3, [("C", 5, 8)]),
    (4, [(s, 5, 1) for s in "CDEFGFED"]),
    (5, [("C", 5, 2), (None, None, 6)]),
]
TYPES = {1: "eighth", 2: "quarter", 4: "half", 6: "half", 8: "whole"}
MIDI = {"C": 72, "D": 74, "E": 76, "F": 77, "G": 79}


def scaffold():
    root = ET.Element("score-partwise", version="3.1")
    sub(sub(root, "work"), "work-title", f"Strings bridge r{spec.REVISION}: one voice, changing lengths")
    sp = sub(sub(root, "part-list"), "score-part", id="P1")
    sub(sp, "part-name", "Violin")
    sub(sub(sp, "score-instrument", id="P1-I1"), "instrument-name", "Violin")
    midi = sub(sp, "midi-instrument", id="P1-I1")
    sub(midi, "midi-channel", 2)
    sub(midi, "midi-program", 41)
    part = sub(root, "part", id="P1")
    for bar, notes in BARS:
        measure = sub(part, "measure", number=str(bar))
        if bar == 1:
            attrs = sub(measure, "attributes")
            sub(attrs, "divisions", 2)
            sub(sub(attrs, "key"), "fifths", 0)
            time = sub(attrs, "time")
            sub(time, "beats", 4)
            sub(time, "beat-type", 4)
            clef = sub(attrs, "clef")
            sub(clef, "sign", "G")
            sub(clef, "line", 2)
            tempo = sub(measure, "direction", placement="above")
            met = sub(sub(tempo, "direction-type"), "metronome")
            sub(met, "beat-unit", "quarter")
            sub(met, "per-minute", 80)
            sub(tempo, "sound", tempo="80")
        for step, octave, eighths in notes:
            node = sub(measure, "note")
            if step is None:
                sub(node, "rest")
            else:
                pitch = sub(node, "pitch")
                sub(pitch, "step", step)
                sub(pitch, "octave", octave)
            sub(node, "duration", eighths)
            sub(node, "voice", 1)
            sub(node, "type", TYPES[eighths])
            if eighths == 6:
                sub(node, "dot")
        if bar == BARS[-1][0]:
            sub(sub(measure, "barline", location="right"), "bar-style", "light-heavy")
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def expectations():
    rows, offset = [], 0
    for bar, notes in BARS:
        for step, _, eighths in notes:
            if step is not None:
                rows.append(dict(note=len(rows) + 1, bar=bar, pitch=MIDI[step],
                                 quarter_offset=offset / 2, written_duration_quarters=eighths / 2,
                                 techniques=["pt.flautando"] if bar < 5 else []))
            offset += eighths
    return dict(schema_version=1, test_id=STEM, map_entity_id=spec.ENTITY_ID,
                map_revision=spec.REVISION, map_sha256=hashlib.sha256(MAP.read_bytes()).hexdigest(),
                voice="single voice on its own channel (Port 1 / Channel 2)",
                natural_base_normalized_to_empty=True,
                notes=rows,
                acceptance=("Flautando must persist on every note of bars 1-4 although the base length "
                            "category changes between bars, and clear at bar 5 (nat.). Record whether "
                            "Off commands for CC112 arrive after a restated On, and Fiddle's debounced/"
                            "ignored Off counters for the input."))


def artifacts():
    return {f"{STEM}.musicxml": scaffold(),
            f"{STEM}.expected.json": (json.dumps(expectations(), indent=2) + "\n").encode()}


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
    print("Checked" if args.check else "Generated", STEM)


if __name__ == "__main__":
    main()
