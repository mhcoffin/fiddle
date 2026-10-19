#!/usr/bin/env python3
"""Generate a separate r6 reliability scaffold; no map or runtime changes.

Score labels are instructions, NOT active Dorico playing techniques.
See reliability.md for the required manual setup and playback passes.
"""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET

from generate import sub

HERE = Path(__file__).resolve().parent
MARKS = {
    (1, 1): "A: unmarked baseline",
    (2, 1): "B: add NON VIBRATO (persistent)",
    (3, 1): "C: add CON VIBRATO (persistent)",
    (4, 1): "D: add WIDE VIBRATO (whole-bar duration)",
    (5, 1): "E: add VIBRATO (persistent)",
    (5, 2): "NON VIBRATO: duration beats 2-3 only",
    (5, 4): "E return: no new marking here",
    (6, 1): "F: VIBRATO explicitly; chord test",
    (7, 1): "G: vibrato continues",
    (7, 2): "FLAUTANDO: duration beats 2-3 only",
    (7, 4): "G return: no new marking here",
}


def expectations():
    """Desired states, not claims that Dorico restores scoped directions."""
    notes = []
    for bar in range(1, 8):
        for beat in range(1, 5):
            pt = {1: None, 2: "pt.nonVibrato", 3: "pt.conVibrato",
                  4: "pt.wideVibrato"}.get(bar, "pt.vibrato")
            if bar == 5 and beat in (2, 3):
                pt = "pt.nonVibrato"
            techniques = [pt] if pt else []
            if bar == 7 and beat in (2, 3):
                techniques.append("pt.flautando")
            pitches = [72, 76, 79] if bar == 6 or (bar, beat) == (1, 4) else [72]
            for pitch in pitches:
                notes.append({"label": f"{bar}.{beat} pitch {pitch}", "bar": bar,
                              "beat": beat, "quarter_offset": (bar - 1) * 4 + beat - 1,
                              "pitch": pitch, "length": "kMedium",
                              "techniques": sorted(techniques),
                              "restoration_probe": (bar, beat) in ((5, 4), (7, 4))})
    return notes


def scaffold():
    root = ET.Element("score-partwise", version="3.1")
    sub(sub(root, "work"), "work-title", "Fiddle r6 reliability — disposable test")
    part = sub(sub(root, "part-list"), "score-part", id="P1")
    sub(part, "part-name", "Violin")
    instrument = sub(part, "score-instrument", id="P1-I1")
    sub(instrument, "instrument-name", "Violin")
    midi = sub(part, "midi-instrument", id="P1-I1")
    sub(midi, "midi-channel", 2)
    sub(midi, "midi-program", 41)
    music = sub(root, "part", id="P1")
    for bar in range(1, 8):
        measure = sub(music, "measure", number=str(bar))
        if bar == 1:
            attributes = sub(measure, "attributes")
            sub(attributes, "divisions", 1)
            sub(sub(attributes, "key"), "fifths", 0)
            time = sub(attributes, "time")
            sub(time, "beats", 4)
            sub(time, "beat-type", 4)
            clef = sub(attributes, "clef")
            sub(clef, "sign", "G")
            sub(clef, "line", 2)
            tempo = sub(measure, "direction", placement="above")
            metronome = sub(sub(tempo, "direction-type"), "metronome")
            sub(metronome, "beat-unit", "quarter")
            sub(metronome, "per-minute", 120)
            sub(tempo, "sound", tempo="120")
        for beat in range(1, 5):
            if (bar, beat) in MARKS:
                direction = sub(measure, "direction", placement="above")
                sub(sub(direction, "direction-type"), "words", MARKS[bar, beat])
            pitches = ["C", "E", "G"] if bar == 6 or (bar, beat) == (1, 4) else ["C"]
            for i, step in enumerate(pitches):
                note = sub(measure, "note")
                if i:
                    sub(note, "chord")
                pitch = sub(note, "pitch")
                sub(pitch, "step", step)
                sub(pitch, "octave", 5)
                sub(note, "duration", 1)
                sub(note, "type", "quarter")
        if bar == 7:
            sub(sub(measure, "barline", location="right"), "bar-style", "light-heavy")
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def artifacts():
    return {"reliability-r6.musicxml": scaffold(),
            "reliability-r6.expected.json": (json.dumps(expectations(), indent=2) + "\n").encode()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for name, data in artifacts().items():
        path = HERE / "generated" / name
        if args.check:
            if path.read_bytes() != data:
                parser.error(f"Stale artifact: {path}")
        else:
            path.write_bytes(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
