#!/usr/bin/env python3
"""Inspect an exported SMF against an experimental decoder, never the live app.

Reports exact PTs at each note-on in file order. It does not simulate Dorico's
switch selection or repair late/missing messages. Type 0/1, PPQN files only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, size):
        if size < 0 or self.pos + size > len(self.data):
            raise ValueError("Truncated MIDI file")
        value = self.data[self.pos:self.pos + size]
        self.pos += size
        return value

    def byte(self):
        return self.take(1)[0]

    def vlq(self):
        result = 0
        for _ in range(4):
            value = self.byte()
            result = (result << 7) | (value & 127)
            if not value & 128:
                return result
        raise ValueError("Invalid MIDI variable-length integer")

    def chunk(self, tag):
        if self.take(4) != tag:
            raise ValueError("Expected MIDI chunk " + repr(tag))
        return self.take(struct.unpack(">I", self.take(4))[0])


def read_midi(data):
    reader = Reader(data)
    header = reader.chunk(b"MThd")
    if len(header) < 6:
        raise ValueError("Short MIDI header")
    kind, count, division = struct.unpack(">HHH", header[:6])
    if kind not in (0, 1) or division == 0 or division & 0x8000:
        raise ValueError("Only type 0/1 PPQN MIDI files are supported")
    if count == 0 or (kind == 0 and count != 1):
        raise ValueError("Invalid MIDI track count")
    tracks = []
    for _ in range(count):
        track = Reader(reader.chunk(b"MTrk"))
        events, tick, running, port = [], 0, None, 0
        ended = False
        while track.pos < len(track.data):
            tick += track.vlq()
            status = track.byte()
            if status < 128:
                if running is None:
                    raise ValueError("Running status without channel status")
                track.pos -= 1
                status = running
            if status == 0xFF:
                meta = track.byte()
                payload = track.take(track.vlq())
                if meta == 0x21:
                    if len(payload) != 1:
                        raise ValueError("Invalid MIDI port meta event")
                    port = payload[0]
                if meta == 0x2F:
                    if payload or track.pos != len(track.data):
                        raise ValueError("Invalid MIDI end-of-track")
                    ended = True
                    break
                continue
            if status in (0xF0, 0xF7):
                track.take(track.vlq())
                running = None
                continue
            if not 0x80 <= status <= 0xEF:
                raise ValueError("Unsupported MIDI status")
            running = status
            kind = status >> 4
            payload = track.take(1 if kind in (0xC, 0xD) else 2)
            if any(v >= 128 for v in payload):
                raise ValueError("Invalid channel-event data byte")
            events.append((tick, port, (status & 15) + 1, kind, tuple(payload)))
        if not ended:
            raise ValueError("Missing MIDI end-of-track")
        tracks.append(events)
    if reader.pos != len(reader.data):
        raise ValueError("Unexpected trailing MIDI data")
    return division, tracks


def load_decoder(path):
    decoder = json.loads(path.read_text())
    if decoder.get("schema_version") not in (1, 2, 3, 4) or decoder.get("runtime_compatible") is not False:
        raise ValueError("Expected experimental decoder schema 1, 2, 3 or 4")
    if decoder["schema_version"] in (2, 3, 4):
        low, high = decoder["musical_range"]
        keys = decoder["key_switches"]
        pitches = [k[field] for k in keys for field in ("on_note", "off_note")]
        codes = pitches
        if decoder["schema_version"] >= 3:
            velocities = [k[field] for k in keys for field in ("on_velocity", "off_velocity")]
            if any(type(v) is not int or not 1 <= v <= 127 for v in velocities):
                raise ValueError("Invalid command velocity")
            codes = list(zip(pitches, velocities))
        if (any(type(n) is not int for n in (low, high)) or not 0 <= low <= high <= 127
                or not keys or len(set(codes)) != len(codes)
                or any(type(n) is not int or not 0 <= n <= 127 or low <= n <= high for n in pitches)
                or len({k["technique"] for k in keys}) != len(keys)
                or any(not k["technique"].startswith("pt.") for k in keys)):
            raise ValueError("Invalid keyswitch decoder")
    elif decoder.get("key_switches"):
        raise ValueError("Keyswitches require decoder schema 2")
    if decoder["schema_version"] == 4:
        dimensions = decoder["key_dimensions"]
        used_pitches = set(pitches)
        used_axes = set()
        used_pts = {k["technique"] for k in keys}
        used_pts.update(pt for table in decoder["tables"].values() for entry in table["values"].values() for pt in entry["techniques"])
        if not dimensions:
            raise ValueError("Missing scalar keyswitch dimensions")
        for dimension in dimensions:
            note, axis, values = dimension["note"], dimension["axis_id"], dimension["values"]
            pts = list(values.values())
            if (type(note) is not int or not 0 <= note <= 127 or low <= note <= high or note in used_pitches
                    or not isinstance(axis, str) or not axis or axis in used_axes
                    or not values or any(str(int(v)) != v or not 1 <= int(v) <= 127 for v in values)
                    or pts.count(None) != 1 or len(set(pts)) != len(pts)
                    or any(p is not None and (not isinstance(p, str) or not p.startswith("pt.") or p in used_pts) for p in pts)):
                raise ValueError("Invalid scalar keyswitch dimension")
            used_pitches.add(note)
            used_axes.add(axis)
            used_pts.update(p for p in pts if p)
    elif decoder.get("key_dimensions"):
        raise ValueError("Scalar keyswitch dimensions require schema 4")
    name = decoder["map_file"]
    if Path(name).name != name:
        raise ValueError("Map filename must be local to the decoder directory")
    if hashlib.sha256((path.parent / name).read_bytes()).hexdigest() != decoder["map_sha256"]:
        raise ValueError("Map/decoder digest mismatch; regenerate the pair")
    return decoder


def key_commands(decoder):
    if decoder["schema_version"] >= 3:
        commands = {}
        for key in decoder["key_switches"]:
            for prefix, active in (("on", True), ("off", False)):
                commands.setdefault(key[prefix + "_note"], {})[key[prefix + "_velocity"]] = (key["technique"], active)
        for dimension in decoder.get("key_dimensions", []):
            commands[dimension["note"]] = {int(v): (dimension["axis_id"], pt) for v, pt in dimension["values"].items()}
        return commands
    return {key[field]: (key["technique"], active)
            for key in decoder.get("key_switches", [])
            for field, active in (("on_note", True), ("off_note", False))}


def musical_note_on(event, commands):
    return event[3] == 0x9 and event[4][1] > 0 and event[4][0] not in commands


def decode_notes(decoder, events):
    states, notes, warnings, last_notes = {}, [], [], {}
    tables = decoder["tables"]
    commands, key_states = key_commands(decoder), {}
    dimension_pitches = {d["note"] for d in decoder.get("key_dimensions", [])}
    dimension_states = {}
    for tick, port, channel, kind, data in events:
        state = states.setdefault((port, channel), {})
        keys = key_states.setdefault((port, channel), {})
        dimensions = dimension_states.setdefault((port, channel), {})
        if kind in (0x8, 0x9) and data[0] in commands:
            if kind == 0x9 and data[1] > 0:
                command = commands[data[0]]
                if decoder["schema_version"] >= 3:
                    command = command.get(data[1])
                    if command is None:
                        warnings.append(f"Tick {tick}: unknown keyswitch pitch/velocity {data[0]}/{data[1]} on port {port}, channel {channel}; state not guessed")
                        continue
                pt, active = command
                target = dimensions if data[0] in dimension_pitches else keys
                previous_note = last_notes.get((port, channel))
                if previous_note and previous_note["tick"] == tick and (pt not in target or target[pt] != active):
                    warnings.append(f"Tick {tick}: keyswitch {data[0]} follows note {previous_note['index']} at the same tick; file-order state retained, live note association unverified")
                target[pt] = active
            # Both command pitches and their releases are protocol, not music.
            continue
        if kind == 0xB:
            cc, value = data
            if str(cc) in tables:
                previous_note = last_notes.get((port, channel))
                if (previous_note and previous_note["tick"] == tick
                        and state.get(str(cc)) != value):
                    warnings.append(f"Tick {tick}: CC{cc}={value} follows note {previous_note['index']} at the same tick; file-order state retained, live note association unverified")
                state[str(cc)] = value
                if str(value) not in tables[str(cc)]["values"]:
                    warnings.append(f"Tick {tick}: unknown CC{cc}={value} on port {port}, channel {channel}")
            elif 102 <= cc <= 119:
                warnings.append(f"Tick {tick}: CC{cc} is not owned by this probe; wrong map or mixed capture?")
        if kind == 0x9 and data[1] > 0:
            if commands and not decoder["musical_range"][0] <= data[0] <= decoder["musical_range"][1]:
                warnings.append(f"Tick {tick}: musical pitch {data[0]} outside this probe's declared range")
            pts, missing, invalid, length = set(), [], [], None
            for cc, table in tables.items():
                if cc not in state:
                    missing.append(int(cc))
                    continue
                entry = table["values"].get(str(state[cc]))
                if entry is None:
                    invalid.append(int(cc))
                    continue
                pts.update(entry["techniques"])
                if "length" in entry:
                    length = entry["length"]
            pts.update(pt for pt, active in keys.items() if active)
            pts.update(pt for pt in dimensions.values() if pt is not None)
            notes.append({"index": len(notes) + 1, "tick": tick, "port": port, "channel": channel,
                          "pitch": data[0], "techniques": sorted(pts), "length": length,
                          "missing_ccs": missing, "invalid_ccs": invalid})
            if commands:
                notes[-1]["missing_keys"] = sorted(k["technique"] for k in decoder["key_switches"] if k["technique"] not in keys)
            if dimension_pitches:
                notes[-1]["missing_dimensions"] = sorted(d["axis_id"] for d in decoder["key_dimensions"] if d["axis_id"] not in dimensions)
            last_notes[(port, channel)] = notes[-1]
    return notes, warnings


def compare(notes, expectations):
    failures = []
    if len(notes) != len(expectations):
        failures.append(f"Expected {len(expectations)} notes, received {len(notes)}")
    for note, expected in zip(notes, expectations):
        prefix = f"Note {note['index']} ({expected['label']})"
        if note["missing_ccs"] or note["invalid_ccs"] or note.get("missing_keys") or note.get("missing_dimensions"):
            failures.append(prefix + ": incomplete/invalid controller state")
        if set(note["techniques"]) != set(expected["techniques"]):
            failures.append(prefix + f": expected {expected['techniques']}, received {note['techniques']}")
        if "pitch" in expected and note["pitch"] != expected["pitch"]:
            failures.append(prefix + ": unexpected pitch")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("midi", type=Path)
    parser.add_argument("--variant", choices=("separate", "packed", "keyswitch", "velocity", "dimensions", "high-vibrato"), required=True)
    parser.add_argument("--track", type=int, help="Zero-based track index; required if several tracks contain notes")
    parser.add_argument("--expect", type=Path, help="Compare every note to a JSON expectation list")
    args = parser.parse_args()
    try:
        decoder = load_decoder(HERE / "generated" / (args.variant + ".decoder.json"))
        division, tracks = read_midi(args.midi.read_bytes())
        commands = key_commands(decoder)
        candidates = [i for i, events in enumerate(tracks) if any(musical_note_on(e, commands) for e in events)]
        selected = args.track
        if selected is None:
            if len(candidates) != 1:
                raise ValueError(f"Choose --track from note-containing tracks {candidates}; cross-track event ordering is not inferred")
            selected = candidates[0]
        if not 0 <= selected < len(tracks):
            raise ValueError("Track index out of range")
        # Do not silently ignore controller-only tracks that might affect this track.
        endpoints = {(e[1], e[2]) for e in tracks[selected] if musical_note_on(e, commands)}
        for i, events in enumerate(tracks):
            if i != selected and any((e[1], e[2]) in endpoints and (
                    (e[3] == 0xB and 102 <= e[4][0] <= 119)
                    or (e[3] == 0x9 and e[4][1] > 0 and e[4][0] in commands)) for e in events):
                raise ValueError("Relevant protocol events span multiple tracks; use a single-stream capture (do not guess same-tick ordering)")
        notes, warnings = decode_notes(decoder, tracks[selected])
        failures = compare(notes, json.loads(args.expect.read_text())) if args.expect else []
        incomplete = any(n["missing_ccs"] or n["invalid_ccs"] or n.get("missing_keys") or n.get("missing_dimensions") for n in notes)
        passed = bool(notes) and not (warnings or failures or incomplete)
        print(json.dumps({"variant": args.variant, "ppqn": division, "track": selected,
                          "kind": "exported-midi-observation-not-live-playback-proof",
                          "expectations_checked": bool(args.expect), "passed": passed,
                          "notes": notes, "warnings": warnings, "failures": failures}, indent=2))
        return 0 if passed else 1
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
