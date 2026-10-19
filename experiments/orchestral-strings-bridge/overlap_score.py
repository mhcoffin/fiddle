#!/usr/bin/env python3
"""Marker-free paired control/overlap score; unchanged r2 bridge, no installation."""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

from generate import MAP_FILE, sub
from profile import ENTITY_ID, REVISION

HERE = Path(__file__).resolve().parent


def add_note(measure, voice, duration, step=None, octave=None):
    n = sub(measure, "note")
    if step is None:
        sub(n, "rest")
    else:
        p = sub(n, "pitch")
        sub(p, "step", step)
        sub(p, "octave", octave)
    sub(n, "duration", duration)
    sub(n, "voice", voice)
    sub(n, "type", {1: "quarter", 3: "half", 4: "whole"}[duration])
    if duration == 3:
        sub(n, "dot")
    if step is not None:
        sub(n, "stem", "down" if voice == 1 else "up")


def scaffold():
    root = ET.Element("score-partwise", version="3.1")
    sub(sub(root, "work"), "work-title", f"Strings bridge r{REVISION}: overlap test")
    sp = sub(sub(root, "part-list"), "score-part", id="P1")
    sub(sp, "part-name", "Violin")
    sub(sub(sp, "score-instrument", id="P1-I1"), "instrument-name", "Violin")
    midi = sub(sp, "midi-instrument", id="P1-I1")
    sub(midi, "midi-channel", 1)
    sub(midi, "midi-program", 41)
    part = sub(root, "part", id="P1")
    for bar in range(1, 6):
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
        length = 3 if bar in (2, 4) else 1
        add_note(measure, 1, length, "C", 5 if bar == 5 else 4)
        add_note(measure, 1, 4 - length)
        sub(sub(measure, "backup"), "duration", 4)
        if bar == 5:
            add_note(measure, 2, 4)
            sub(sub(measure, "barline", location="right"), "bar-style", "light-heavy")
        else:
            add_note(measure, 2, 1)
            for step in ("D", "E", "F"):
                add_note(measure, 2, 1, step, 5)
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def expectations():
    notes = []
    for bar in range(1, 5):
        for beat, pitch in enumerate((60, 74, 76, 77), 1):
            pt = ("pt.flautando" if beat == 1 else "pt.overpressure") if bar <= 2 else (
                "pt.strumUp" if beat == 1 else "pt.strumDown")
            notes.append(dict(note=len(notes) + 1, bar=bar, beat=beat, pitch=pitch,
                              voice=1 if beat == 1 else 2,
                              quarter_offset=(bar - 1) * 4 + beat - 1,
                              written_duration_quarters=3 if bar in (2, 4) and beat == 1 else 1,
                              techniques=[pt]))
    notes.append(dict(note=17, bar=5, beat=1, pitch=72, voice=1,
                      quarter_offset=16, written_duration_quarters=1, techniques=[]))
    return dict(schema_version=1, test_id="overlapping-notes-r2", map_entity_id=ENTITY_ID,
                map_revision=REVISION,
                map_sha256=hashlib.sha256((HERE / MAP_FILE).read_bytes()).hexdigest(),
                status="desired-note-start-states-not-observed-results",
                natural_base_normalized_to_empty=True, length_category_asserted=False,
                unlisted_addon_dimensions="unspecified; missing initialization is unknown, not zero",
                notes=notes,
                controls=[dict(bar=1, old_note=1, next_note=2),
                          dict(bar=3, old_note=9, next_note=10)],
                overlap_probes=[dict(bar=2, old_note=5, overlapping_notes=[6, 7],
                                     post_release_note=8),
                                dict(bar=4, old_note=13, overlapping_notes=[14, 15],
                                     post_release_note=16)],
                acceptance="Compare control states first; independently prove actual NoteOff overlap and sentinel ordering from the capture. No overlap means inconclusive, not pass.")


def artifacts():
    return {f"overlap-r{REVISION}.musicxml": scaffold(),
            f"overlap-r{REVISION}.expected.json": (json.dumps(expectations(), indent=2) + "\n").encode()}


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
    print("Checked overlap scaffold" if args.check else "Generated overlap scaffold")


if __name__ == "__main__":
    main()
