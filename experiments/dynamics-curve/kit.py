#!/usr/bin/env python3
"""Generate the dynamics measurement kit: two measurement expression maps, the
MusicXML score and the expected-note list the analysis matches against.

No installation or playback mutation. --check is strictly read-only.
"""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
MAP_FILE = "Fiddle-Dynamics-Probe.doricolib"
SCORE_FILE = "dynamics-probe.musicxml"
EXPECTED_FILE = "dynamics-probe.expected.json"
TEMPO = 120  # quarter notes per minute: one 4/4 bar = 2 s

# The two maps differ only in how Dorico expresses dynamics. Each part of the
# score is routed to one of them, on its own channel of the MIDI Probe.
MAPS = [
    dict(channel=1, part="P1", name="Fiddle Dynamics Probe - CC1",
         entity="xmap.user.Fiddle.DynamicsProbe.CC1", volume=("kCC", 1)),
    dict(channel=2, part="P2", name="Fiddle Dynamics Probe - velocity",
         entity="xmap.user.Fiddle.DynamicsProbe.Velocity", volume=("kNoteVelocity", 0)),
]

# Every written dynamic Dorico's MusicXML import accepts, softest first.
LADDER = ["pppppp", "ppppp", "pppp", "ppp", "pp", "p", "mp",
          "mf", "f", "ff", "fff", "ffff", "fffff", "ffffff"]
# One bar per dynamic: half note, two quarters (C5, D5, E5).
LADDER_NOTES = [("C", 5, 4), ("D", 5, 2), ("E", 5, 2)]
# At mf: each articulated quarter follows a plain one at the same pitch.
ARTICULATION_BARS = [
    [("G", 5, None), ("G", 5, "accent"), ("A", 5, None), ("A", 5, "strong-accent")],
    [("B", 5, None), ("B", 5, "tenuto"), ("C", 6, None), ("C", 6, None)],
]
MIDI = {("C", 5): 72, ("D", 5): 74, ("E", 5): 76, ("G", 5): 79, ("A", 5): 81,
        ("B", 5): 83, ("C", 6): 84, ("F", 5): 77}
TYPES = {2: "quarter", 4: "half", 8: "whole"}
ARTICULATION_PT = {"accent": "accent", "strong-accent": "marcato", "tenuto": "tenuto"}

# Passes of the score, in order, with the Library > Playback Options values
# that differ from pass A. The baseline settings are in README.md.
PASSES = [
    dict(name="A", curve_power=2.5, min_level=-6, max_level=6,
         accent=0.5, marcato=0.8, tenuto=0.0),
    dict(name="B", curve_power=1.0, min_level=-6, max_level=6,
         accent=0.5, marcato=0.8, tenuto=0.0),
    dict(name="C", curve_power=1.0, min_level=-3, max_level=3,
         accent=0.5, marcato=0.8, tenuto=0.0),
    dict(name="D", curve_power=2.5, min_level=-6, max_level=6,
         accent=1.0, marcato=2.0, tenuto=1.5),
]


def sub(parent, tag, value=None, **attrs):
    node = ET.SubElement(parent, tag, attrs)
    if value is not None:
        node.text = str(value)
    return node


def expression_maps():
    """A Natural-only map per dynamics mode: no switches, conditions,
    playback overrides or Init actions, so only Dorico's dynamics vary."""
    root = ET.Element("kScoreLibrary")
    sub(root, "fileVersion", "1.1438")
    entities = sub(sub(root, "expressionMapDefinitions"), "entities", array="true")
    for spec in MAPS:
        d = sub(entities, "ExpressionMapDefinition")
        for tag, value in [
            ("name", spec["name"]), ("entityID", spec["entity"]), ("parentEntityID", ""),
            ("inheritanceMask", 0), ("creator", "Fiddle"),
            ("description", "Fiddle dynamics measurement: Natural only, no switches. Capture only."),
            ("version", 1), ("pluginNames", ""), ("autoMutualExclusion", "false"),
            ("allowMultipleNotesAtSamePitch", "true"), ("applyStageTemplateSettings", "false"),
            ("pitchBendRange", 2), ("microtonalPlaybackMethod", "kAuto")]:
            sub(d, tag, value)
        init = sub(d, "initSwitchData")
        sub(init, "enabled", "false")
        sub(init, "initActions", array="true")
        base = sub(sub(d, "playingTechniqueCombinations", array="true"), "playingTechniqueCombination")
        for tag, value in [
            ("baseSwitchID", 1), ("techniqueIDs", "pt.natural"), ("enabled", "true"), ("flags", 0),
            ("conditionString", None), ("velocityRange", "1,127"), ("pitchRange", "0,127"),
            ("transpose", 0), ("ticksBefore", 0), ("millisecondsBefore", 0),
            ("velocityFactor", "1.000000"), ("lengthFactor", "1.000000"), ("monophonic", "false"),
            ("applyMillisecondsBeforeToEndOffsets", "false"),
            ("applyMillisecondsBeforeToControllers", "false")]:
            sub(base, tag, value)
        volume = sub(base, "volumeType")
        sub(volume, "type", spec["volume"][0])
        sub(volume, "param1", spec["volume"][1])
        attack = sub(base, "attackType")
        sub(attack, "type", "kNoteVelocity")
        sub(attack, "param1", 0)
        sub(base, "switchOnActions", array="true")
        sub(base, "switchOffActions", array="true")
        sub(d, "techniqueAddOns", array="true")
        sub(d, "mutualExclusionGroups", array="true")
        sub(d, "playbackOptionsOverrides", array="true")
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def bars():
    """(dynamic or None, wedge, [(step, octave, quarters*2, articulation, tie)])"""
    result = [(mark, None, [(s, o, n, None, None) for s, o, n in LADDER_NOTES]) for mark in LADDER]
    result.append(("mf", None, [(s, o, 2, a, None) for s, o, a in ARTICULATION_BARS[0]]))
    result.append((None, None, [(s, o, 2, a, None) for s, o, a in ARTICULATION_BARS[1]]))
    # A held F5 under a two-bar crescendo, p to f: CC1 shape over one note.
    result.append(("p", "crescendo", [("F", 5, 8, None, "start")]))
    result.append((None, "stop", [("F", 5, 8, None, "stop")]))
    result.append(("f", None, [("F", 5, 8, None, None)]))
    result.append((None, None, [(None, None, 8, None, None)]))
    return result


def direction(measure, **kinds):
    node = sub(measure, "direction", placement="below")
    kind = sub(node, "direction-type")
    if "dynamic" in kinds:
        sub(sub(kind, "dynamics"), kinds["dynamic"])
    if "wedge" in kinds:
        sub(kind, "wedge", type=kinds["wedge"])
    return node


def part(root, spec):
    p = sub(root, "part", id=spec["part"])
    for number, (mark, wedge, notes) in enumerate(bars(), start=1):
        measure = sub(p, "measure", number=str(number))
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
        if mark:
            direction(measure, dynamic=mark)
        if wedge == "crescendo":
            direction(measure, wedge="crescendo")
        for step, octave, length, articulation, tie in notes:
            note = sub(measure, "note")
            if step is None:
                sub(note, "rest")
            else:
                pitch = sub(note, "pitch")
                sub(pitch, "step", step)
                sub(pitch, "octave", octave)
            sub(note, "duration", length)
            if tie:
                sub(note, "tie", type=tie)
            sub(note, "voice", 1)
            sub(note, "type", TYPES[length])
            notations = None
            if tie:
                notations = sub(note, "notations")
                sub(notations, "tied", type=tie)
            if articulation:
                notations = notations if notations is not None else sub(note, "notations")
                sub(sub(notations, "articulations"), articulation)
        if wedge == "stop":
            direction(measure, wedge="stop")
        if number == len(bars()):
            sub(sub(measure, "barline", location="right"), "bar-style", "light-heavy")


def score():
    root = ET.Element("score-partwise", version="3.1")
    sub(sub(root, "work"), "work-title", "Fiddle dynamics measurement")
    parts = sub(root, "part-list")
    for spec in MAPS:
        sp = sub(parts, "score-part", id=spec["part"])
        sub(sp, "part-name", f"Violin {spec['channel']}")
        sub(sub(sp, "score-instrument", id=f"{spec['part']}-I1"), "instrument-name", "Violin")
        midi = sub(sp, "midi-instrument", id=f"{spec['part']}-I1")
        sub(midi, "midi-channel", spec["channel"])
        sub(midi, "midi-program", 41)
    for spec in MAPS:
        part(root, spec)
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def expected_notes():
    """Sounding notes in order (a tied note sounds once), with the written
    dynamic in force and any articulation."""
    rows, quarters, dynamic = [], 0.0, None
    for number, (mark, wedge, notes) in enumerate(bars(), start=1):
        dynamic = mark or dynamic
        for step, octave, length, articulation, tie in notes:
            if step is not None and tie != "stop":
                kind = ("hairpin" if tie == "start" else "hairpin-end" if number == 19
                        else "articulation" if 15 <= number <= 16 else "ladder")
                rows.append(dict(index=len(rows), bar=number, quarter=quarters,
                                 pitch=MIDI[(step, octave)], dynamic=dynamic, kind=kind,
                                 articulation=ARTICULATION_PT.get(articulation)))
            quarters += length / 2
    return rows


def expectations():
    return dict(schema_version=1, tempo=TEMPO, ladder=LADDER, passes=PASSES,
                channels={str(m["channel"]): m["name"] for m in MAPS},
                notes=expected_notes())


def artifacts():
    return {MAP_FILE: expression_maps(), SCORE_FILE: score(),
            EXPECTED_FILE: (json.dumps(expectations(), indent=2) + "\n").encode()}


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
