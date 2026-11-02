#!/usr/bin/env python3
"""Compare what Dorico sends an instrument directly (a MIDI Probe capture,
Dorico playing your library expression map) with what a Fiddle layer sends
(an exported MIDI Capture of the layer's emitted MIDI, Dorico playing the
bridge map and Fiddle applying the same library map).

For every note of the branch check score it compares onset, duration,
velocity, channel, the dynamics controllers in force at the note's start,
and the switch messages (keyswitches, other CCs, program changes) sent
since the previous note. Times are relative to each side's first note.

Read-only: no installation, routing or playback.
"""
import argparse
import importlib.util
import json
from collections import Counter
from pathlib import Path
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
NOTE_ON, NOTE_OFF, CONTROL_CHANGE, PROGRAM_CHANGE = 0, 1, 3, 6  # RealtimeMidiEventType
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
SETTLE_MS = 15.0   # a dynamic's CC can follow its note-on by a few ms
TOLERANCE = dict(onset_ms=3.0, duration_ms=3.0, velocity=1, controller=1)


def load_probe_reader():
    path = HERE.parent / "dynamics-curve" / "analyze.py"
    spec = importlib.util.spec_from_file_location("dynamics_analyze", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def map_facts(path, name=None):
    """Keyswitch pitches and dynamics controllers of a library map file."""
    root = ET.parse(path).getroot()
    maps = list(root.iter("ExpressionMapDefinition"))
    if name:
        maps = [m for m in maps if m.findtext("name") == name]
        if not maps:
            raise ValueError(f"No expression map named {name!r} in {path}")
    keyswitches, controllers = set(), {1}
    for m in maps:
        for action in m.iter():
            if action.tag in ("switchOnAction", "switchOffAction") and action.findtext("type") == "kKeySwitch":
                keyswitches.add(int(action.findtext("param1")))
        for tag in ("volumeType", "volumeType2"):
            for volume in m.iter(tag):
                if volume.findtext("type") == "kCC":
                    controllers.add(int(volume.findtext("param1")))
    return keyswitches, controllers


def probe_events(path, expected_count, keyswitches, pass_number=None):
    """Events [(ms, kind, channel, number, value)] of the probe pass that
    contains the whole score."""
    reader = load_probe_reader()
    header, blocks = reader.read_capture(path)
    rate = header.get("sample_rate", 48000.0)
    candidates = []
    for index, blocks_of_pass in enumerate(reader.passes(blocks), start=1):
        events = []
        for block in blocks_of_pass:
            for e in block["events"]:
                ms = e["host_samples"] * 1000.0 / rate
                if e["type"] == NOTE_ON and e["data2"] > 0:
                    events.append((ms, "on", e["channel"], e["data1"], e["data2"]))
                elif e["type"] in (NOTE_ON, NOTE_OFF):
                    events.append((ms, "off", e["channel"], e["data1"], 0))
                elif e["type"] == CONTROL_CHANGE:
                    events.append((ms, "cc", e["channel"], e["data1"], e["data2"]))
                elif e["type"] == PROGRAM_CHANGE:
                    events.append((ms, "pc", e["channel"], e["data1"], 0))
        musical = [e for e in events if e[1] == "on" and e[3] not in keyswitches]
        if len(musical) == expected_count:
            candidates.append((index, events))
    if pass_number:
        candidates = [c for c in candidates if c[0] == pass_number]
    if not candidates:
        raise ValueError(f"No probe pass contains exactly {expected_count} notes")
    return candidates[-1][1]


def note_number(text):
    if text.lstrip("-").isdigit():
        return int(text)
    name, octave = (text[:2], text[2:]) if len(text) > 1 and text[1] == "#" else (text[:1], text[1:])
    return NAMES.index(name) + 12 * (int(octave) + 1)


def fiddle_events(path):
    """Events from Fiddle's MIDI Capture export (time, type, ChN, note/CC, value)."""
    kinds = {"NoteOn": "on", "NoteOff": "off", "CC": "cc", "PC": "pc"}
    events = []
    for line in Path(path).read_text().splitlines():
        fields = line.split("\t")
        if len(fields) < 5 or fields[1] not in kinds:
            continue
        kind = kinds[fields[1]]
        value = int(fields[4])
        if kind == "on" and value == 0:
            kind = "off"
        events.append((float(fields[0].rstrip("s")) * 1000.0, kind, int(fields[2].lstrip("Ch")),
                       note_number(fields[3]) if kind in ("on", "off") else int(fields[3]), value))
    return sorted(events, key=lambda e: e[0])


def notes(events, keyswitches, controllers):
    """Per musical note: onset, duration, velocity, channel, dynamics
    controllers at its start and the switches sent since the previous note."""
    events = sorted(events, key=lambda e: e[0])
    starts = [e for e in events if e[1] == "on" and e[3] not in keyswitches]
    if not starts:
        return []
    origin = starts[0][0]
    result = []
    for i, (ms, _, channel, pitch, velocity) in enumerate(starts):
        end = next((e[0] for e in events if e[1] == "off" and e[2] == channel and e[3] == pitch
                    and e[0] > ms), None)
        next_on = starts[i + 1][0] if i + 1 < len(starts) else float("inf")
        dynamics = {}
        for e in events:
            if e[1] == "cc" and e[3] in controllers and e[0] <= min(ms + SETTLE_MS, next_on - 1):
                dynamics[e[3]] = e[4]
        since = starts[i - 1][0] + SETTLE_MS if i else float("-inf")
        switches = Counter()
        leads = []
        for e in events:
            if not since < e[0] <= ms + SETTLE_MS:
                continue
            if e[1] == "on" and e[3] in keyswitches:
                switches[("KS", e[2], e[3], e[4])] += 1
                leads.append(ms - e[0])
            elif e[1] == "cc" and e[3] not in controllers and e[3] < 120:
                switches[("CC", e[2], e[3], e[4])] += 1
                leads.append(ms - e[0])
            elif e[1] == "pc":
                switches[("PC", e[2], e[3])] += 1
        result.append(dict(onset=ms - origin, duration=None if end is None else end - ms,
                           velocity=velocity, channel=channel, pitch=pitch,
                           dynamics=dynamics, switches=switches,
                           lead=max(leads) if leads else None))
    return result


def compare(reference, candidate, expected):
    """Rows of per-note differences, and counts per category."""
    rows, counts = [], Counter()
    for spec, ref, got in zip(expected["notes"], reference, candidate):
        problems = []
        if got["pitch"] != ref["pitch"]:
            problems.append(f"pitch {ref['pitch']}->{got['pitch']}")
        if got["channel"] != ref["channel"]:
            problems.append(f"channel {ref['channel']}->{got['channel']}")
        if abs(got["onset"] - ref["onset"]) > TOLERANCE["onset_ms"]:
            problems.append(f"onset {got['onset'] - ref['onset']:+.1f} ms")
        if ref["duration"] is None or got["duration"] is None:
            problems.append("missing note-off")
        elif abs(got["duration"] - ref["duration"]) > TOLERANCE["duration_ms"]:
            problems.append(f"duration {ref['duration']:.0f}->{got['duration']:.0f} ms")
        if abs(got["velocity"] - ref["velocity"]) > TOLERANCE["velocity"]:
            problems.append(f"velocity {ref['velocity']}->{got['velocity']}")
        for cc in sorted(set(ref["dynamics"]) | set(got["dynamics"])):
            a, b = ref["dynamics"].get(cc), got["dynamics"].get(cc)
            if a is None or b is None or abs(a - b) > TOLERANCE["controller"]:
                problems.append(f"CC{cc} {a}->{b}")
        if ref["switches"] != got["switches"]:
            missing = ref["switches"] - got["switches"]
            extra = got["switches"] - ref["switches"]
            problems.append("switches" + (f" missing {sorted(missing)}" if missing else "")
                            + (f" extra {sorted(extra)}" if extra else ""))
        elif ref["lead"] is not None and got["lead"] is not None and \
                abs(ref["lead"] - got["lead"]) > TOLERANCE["onset_ms"]:
            problems.append(f"switch lead {ref['lead']:.0f}->{got['lead']:.0f} ms")
        for p in problems:
            counts[p.split(" ")[0]] += 1
        rows.append(dict(index=spec["index"], bar=spec["bar"], checks=spec["checks"], problems=problems))
    return rows, counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path, help="MIDI Probe capture.jsonl (Dorico direct)")
    parser.add_argument("fiddle", type=Path, help="Fiddle MIDI Capture export (.midi-dump, emitted)")
    parser.add_argument("--map", type=Path, required=True, help="the library .doricolib used on both sides")
    parser.add_argument("--map-name", help="the map's name, if the file holds several")
    parser.add_argument("--pass", dest="pass_number", type=int, help="probe pass number to use")
    args = parser.parse_args()
    expected = json.loads((HERE / "branch-check.expected.json").read_text())
    keyswitches, controllers = map_facts(args.map, args.map_name)
    count = len(expected["notes"])
    reference = notes(probe_events(args.probe, count, keyswitches, args.pass_number), keyswitches, controllers)
    candidate = notes(fiddle_events(args.fiddle), keyswitches, controllers)
    if len(candidate) != count:
        raise SystemExit(f"Fiddle capture has {len(candidate)} notes; the score has {count}. "
                         "Record a whole pass from bar 1.")
    rows, counts = compare(reference, candidate, expected)
    for row in rows:
        status = "ok  " if not row["problems"] else "DIFF"
        print(f"{status} bar {row['bar']:2} note {row['index'] + 1:2}  {row['checks']}"
              + ("" if not row["problems"] else "\n       " + "; ".join(row["problems"])))
    print(f"\n{sum(1 for r in rows if not r['problems'])}/{len(rows)} notes match"
          f" (onset/duration ±{TOLERANCE['onset_ms']:.0f} ms, velocity/CC ±1)."
          + ("" if not counts else " Differences by kind: " + ", ".join(f"{k} {v}" for k, v in counts.items())))


if __name__ == "__main__":
    main()
