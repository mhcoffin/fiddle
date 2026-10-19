#!/usr/bin/env python3
"""Marker-free shared-PT overlap scaffold; no map or runtime changes."""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

from generate import MAP_FILE, sub
from profile import ENTITY_ID, REVISION

HERE = Path(__file__).resolve().parent
STEM = f"shared-overlap-r{REVISION}"


def add_note(measure, voice, duration, pitch=None):
    node = sub(measure, "note")
    if pitch is None:
        sub(node, "rest")
    else:
        p = sub(node, "pitch")
        sub(p, "step", pitch[0])
        sub(p, "octave", pitch[1])
    sub(node, "duration", duration)
    sub(node, "voice", voice)
    sub(node, "type", {1: "quarter", 3: "half"}[duration])
    if duration == 3:
        sub(node, "dot")
    if pitch is not None:
        sub(node, "stem", "down" if voice == 1 else "up")


def scaffold():
    root = ET.Element("score-partwise", version="3.1")
    sub(sub(root, "work"), "work-title", f"Strings bridge r{REVISION}: shared PT overlap")
    sp = sub(sub(root, "part-list"), "score-part", id="P1")
    sub(sp, "part-name", "Violin")
    sub(sub(sp, "score-instrument", id="P1-I1"), "instrument-name", "Violin")
    midi = sub(sp, "midi-instrument", id="P1-I1")
    sub(midi, "midi-channel", 1)
    sub(midi, "midi-program", 41)
    part = sub(root, "part", id="P1")
    for bar in range(1, 4):
        measure = sub(part, "measure", number=str(bar))
        if bar == 1:
            attrs = sub(measure, "attributes")
            sub(attrs, "divisions", 1)
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
        length = 3 if bar == 2 else 1
        add_note(measure, 1, length, ("C", 4))
        add_note(measure, 1, 4 - length)
        sub(sub(measure, "backup"), "duration", 4)
        if bar == 3:
            add_note(measure, 2, 1, ("C", 5))
            add_note(measure, 2, 3)
            sub(sub(measure, "barline", location="right"), "bar-style", "light-heavy")
        else:
            for step in ("G", "D", "E", "F"):
                add_note(measure, 2, 1, (step, 5))
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def expectations():
    notes = []
    for bar in range(1, 4):
        rows = [(1, 1, 60, 3 if bar == 2 else 1)]
        rows += ([(1, 2, 72, 1)] if bar == 3 else
                 [(beat, 2, pitch, 1) for beat, pitch in enumerate((79, 74, 76, 77), 1)])
        for beat, voice, pitch, duration in rows:
            notes.append(dict(note=len(notes) + 1, bar=bar, beat=beat,
                              voice=voice, pitch=pitch,
                              quarter_offset=(bar - 1) * 4 + beat - 1,
                              written_duration_quarters=duration,
                              techniques=["pt.flautando"] if bar < 3 else []))
    return dict(schema_version=1, test_id=STEM, map_entity_id=ENTITY_ID,
                map_revision=REVISION,
                map_sha256=hashlib.sha256((HERE / MAP_FILE).read_bytes()).hexdigest(),
                status="conditional-hypotheses-require-active-PT-and-live-timing-checks",
                simultaneous_note_order="not asserted; match onset group and pitch, not serialized note order",
                voice="notation only; no voice identity is expected in MIDI",
                natural_base_normalized_to_empty=True, length_category_asserted=False,
                unlisted_addon_dimensions="unspecified; missing initialization is unknown, not zero",
                notes=notes,
                controls=[dict(bar=1, old_note=1, next_note=3)],
                overlap_probes=[dict(bar=2, old_note=6, overlapping_notes=[8, 9], post_release_note=10)],
                acceptance="Both voices must show the same active PT timeline first. Prove actual overlap and inspect clears/reassertions, not just final note-start state. No distinct-technique overlap or independent voice claim.")


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
    print("Checked shared-PT overlap scaffold" if args.check else "Generated shared-PT overlap scaffold")


if __name__ == "__main__":
    main()
