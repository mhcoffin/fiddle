#!/usr/bin/env python3
"""Generate the branch check score: one violin, 14 bars, exercising durations,
same-pitch legato, articulations, switch changes, dynamics and a crescendo.

No installation or playback mutation. --check is strictly read-only.
"""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
SCORE_FILE = "branch-check.musicxml"
EXPECTED_FILE = "branch-check.expected.json"
TEMPO = 100
MIDI = {"G4": 67, "A4": 69, "B4": 71, "C5": 72, "D5": 74, "E5": 76, "F5": 77, "G5": 79,
        "A5": 81, "B5": 83}
TYPES = {2: "quarter", 4: "half", 8: "whole"}


def n(pitch, length=2, *articulations, slur=None, tie=None):
    return ("note", pitch, length, articulations, slur, tie)


# (what the bar checks, the technique the tester enters there or None, items)
BARS = [
    ("plain notes: default duration, mf", None,
     [("dynamic", "mf"), n("C5"), n("D5"), n("E5"), n("F5")]),
    ("staccato: duration, short-note switch", None,
     [n("G5", 2, "staccato"), n("F5", 2, "staccato"), n("E5", 2, "staccato"), n("D5", 2, "staccato")]),
    ("tenuto: duration and dynamic delta", None,
     [n("C5", 2, "tenuto"), n("D5", 2, "tenuto"), n("E5", 2, "tenuto"), n("F5", 2, "tenuto")]),
    ("slurred repeated D5: legato length without same-pitch overlap", None,
     [n("D5", slur="start"), n("D5"), n("D5"), n("E5", slur="stop")]),
    ("f with marcato and accent: dynamic deltas", None,
     [("dynamic", "f"), n("G5", 2, "strong-accent"), n("G5", 2, "accent"), n("A5"), n("B5")]),
    ("pizz.: switch change", "pizz.",
     [("dynamic", "mf"), n("G4"), n("A4"), n("B4"), n("C5")]),
    ("arco: switch back (Off actions of pizz.)", "arco",
     [n("D5", 4), n("E5", 4)]),
    ("con sord.: add-on switch", "con sord.",
     [n("F5", 4), n("G5", 4)]),
    ("senza sord.: add-on switch off", "senza sord.",
     [n("A5", 4), n("G5", 4)]),
    ("p, crescendo over a held note (live dynamics)", None,
     [("dynamic", "p"), ("wedge", "crescendo"), n("F5", 8, tie="start")]),
    ("crescendo continues", None,
     [n("F5", 8, tie="stop"), ("wedge", "stop")]),
    ("f then ff on long notes", None,
     [("dynamic", "f"), n("E5", 4), ("dynamic", "ff"), n("D5", 4)]),
    ("pp staccato: velocity dynamics at another level", None,
     [("dynamic", "pp"), n("C5", 2, "staccato"), n("D5", 2, "staccato"), n("E5", 2, "staccato"),
      n("F5", 2, "staccato")]),
    ("end", None, [("rest", 8)]),
]


def sub(parent, tag, value=None, **attrs):
    node = ET.SubElement(parent, tag, attrs)
    if value is not None:
        node.text = str(value)
    return node


def direction(measure, dynamic=None, wedge=None):
    node = sub(measure, "direction", placement="below")
    kind = sub(node, "direction-type")
    if dynamic:
        sub(sub(kind, "dynamics"), dynamic)
    if wedge:
        sub(kind, "wedge", type=wedge)


def score():
    root = ET.Element("score-partwise", version="3.1")
    sub(sub(root, "work"), "work-title", "Fiddle branch check")
    sp = sub(sub(root, "part-list"), "score-part", id="P1")
    sub(sp, "part-name", "Violin")
    sub(sub(sp, "score-instrument", id="P1-I1"), "instrument-name", "Violin")
    midi = sub(sp, "midi-instrument", id="P1-I1")
    sub(midi, "midi-channel", 1)
    sub(midi, "midi-program", 41)
    part = sub(root, "part", id="P1")
    for number, (_, _, items) in enumerate(BARS, start=1):
        measure = sub(part, "measure", number=str(number))
        if number == 1:
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
            metronome = sub(sub(tempo, "direction-type"), "metronome")
            sub(metronome, "beat-unit", "quarter")
            sub(metronome, "per-minute", TEMPO)
            sub(tempo, "sound", tempo=str(TEMPO))
        for item in items:
            if item[0] == "dynamic":
                direction(measure, dynamic=item[1])
            elif item[0] == "wedge":
                direction(measure, wedge=item[1])
            elif item[0] == "rest":
                note = sub(measure, "note")
                sub(note, "rest")
                sub(note, "duration", item[1])
                sub(note, "voice", 1)
                sub(note, "type", TYPES[item[1]])
            else:
                _, pitch, length, articulations, slur, tie = item
                note = sub(measure, "note")
                p = sub(note, "pitch")
                sub(p, "step", pitch[0])
                sub(p, "octave", pitch[1])
                sub(note, "duration", length)
                if tie:
                    sub(note, "tie", type=tie)
                sub(note, "voice", 1)
                sub(note, "type", TYPES[length])
                if articulations or slur or tie:
                    notations = sub(note, "notations")
                    if tie:
                        sub(notations, "tied", type=tie)
                    if slur:
                        sub(notations, "slur", type=slur, number="1")
                    if articulations:
                        arts = sub(notations, "articulations")
                        for a in articulations:
                            sub(arts, a)
        if number == len(BARS):
            sub(sub(measure, "barline", location="right"), "bar-style", "light-heavy")
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def expected():
    """Sounding notes in order (a tied note sounds once)."""
    rows = []
    for number, (label, technique, items) in enumerate(BARS, start=1):
        for item in items:
            if item[0] == "note" and item[5] != "stop":
                rows.append(dict(index=len(rows), bar=number, pitch=MIDI[item[1]],
                                 checks=label, technique=technique))
    return dict(schema_version=1, tempo=TEMPO,
                techniques={str(i + 1): t for i, (_, t, _) in enumerate(BARS) if t},
                notes=rows)


def artifacts():
    return {SCORE_FILE: score(),
            EXPECTED_FILE: (json.dumps(expected(), indent=2) + "\n").encode()}


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
    print(("Checked" if args.check else "Generated") + f" {len(artifacts())} artifacts")


if __name__ == "__main__":
    main()
