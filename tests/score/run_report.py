#!/usr/bin/env python3
"""Explain a score test run from its run record.

    python3 tests/score/run_report.py basic-rhythm-20261021-143200
    python3 tests/score/run_report.py basic-rhythm-20261021-143200 --note 3
    python3 tests/score/run_report.py --list [test-id]

A run ID names ~/Library/Application Support/Fiddle/ScoreTests/runs/<ID>.json
(FIDDLE_SCORE_TEST_RUNS_DIR overrides the folder); a path to a record works
too. The report shows the result, how parts were bound to endpoints, each
bridge's state, then every failed, missing and extra note with what was
expected and what arrived. --note N adds the MIDI Fiddle saw around note N.
"""
import argparse
import json
import os
import sys
from pathlib import Path

DEFAULT_RUNS = Path.home() / "Library" / "Application Support" / "Fiddle" / "ScoreTests" / "runs"
PITCHES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def runs_folder():
    return Path(os.environ.get("FIDDLE_SCORE_TEST_RUNS_DIR", DEFAULT_RUNS))


def pitch_name(pitch):
    return f"{PITCHES[pitch % 12]}{pitch // 12 - 1}"


def load(run):
    path = Path(run)
    if not path.suffix == ".json" or not path.exists():
        path = runs_folder() / f"{run}.json"
    if not path.exists():
        sys.exit(f"No run record {path}. Runs are in {runs_folder()}; list them with --list.")
    return path, json.loads(path.read_text())


def list_runs(test_id=None, limit=20):
    rows = []
    for path in runs_folder().glob("*.json"):
        try:
            record = json.loads(path.read_text())
        except (OSError, ValueError):
            continue
        if test_id and record.get("test_id") != test_id:
            continue
        rows.append(record)
    rows.sort(key=lambda r: r.get("finished_at", ""), reverse=True)
    return rows[:limit]


def level_text(dynamic):
    if "level" in dynamic and "hairpin" not in dynamic:
        text = f"level {dynamic['level']}"
    else:
        text = f"in hairpin {dynamic.get('hairpin')} ({dynamic.get('direction')})"
        if "between" in dynamic:
            text += f", level between {dynamic['between'][0]} and {dynamic['between'][1]}"
    if "during" in dynamic:
        text += f", {'rising' if dynamic['during']['direction'] == 'up' else 'falling'} while held"
        if dynamic["during"].get("to") is not None:
            text += f" toward {dynamic['during']['to']}"
    return text


def received_text(r):
    lines = [f"received {r['name']} at {r['onset_ms']} ms for "
             + (f"{r['duration_ms']} ms" if r.get("duration_ms") is not None else "(never ended)")
             + f", port {r['port']} channel {r['channel']}, note ID {r['id']}"]
    dynamic = f"dynamics on {r['dynamics']}"
    if "level" in r:
        dynamic += f", starting level {r['level']}"
    dynamic += f", velocity {r['velocity']}"
    if not r.get("pinned"):
        dynamic += ", NOT from a pinned bridge"
    lines.append(dynamic)
    if r.get("cc1"):
        lane = ", ".join(f"{ms}:{v}" for ms, v in r["cc1"][:24])
        more = f" (+{len(r['cc1']) - 24} more)" if len(r["cc1"]) > 24 else ""
        lines.append(f"CC1 (ms after onset:value) {lane}{more}")
    lines.append("techniques " + (", ".join(r.get("techniques", [])) or "none"))
    return lines


def note_report(record, note, expected_note):
    out = [f"#{note['index']} {note['part']} bar {note['bar']} beat {note['beat']:g} {note['source']}: {note['status'].upper()}"]
    out.append(f"  expected {pitch_name(expected_note['pitch'])} at {expected_note['onset_ms']} ms for "
               f"{expected_note['duration_ms']} ms, {level_text(expected_note['dynamic'])}; techniques "
               + (", ".join(expected_note["techniques"]["includes"]) or "none required"))
    if note.get("received"):
        out += ["  " + line for line in received_text(note["received"])]
    for check in note.get("checks", []):
        if not check["ok"]:
            detail = f" ({check['detail']})" if check.get("detail") else ""
            out.append(f"  FAILED {check['check']}: expected {json.dumps(check['expected'])}, "
                       f"received {json.dumps(check['received'])}{detail}")
    return out


def midi_around(record, note, expected_note, window_ms):
    """MIDI events within window_ms of the note's onset, on the record's clock."""
    offset = record["comparison"]["offset_ms"] - record.get("origin_ms", 0)
    centre = expected_note["onset_ms"] + offset
    if note.get("received"):
        centre = note["received"]["onset_ms"] + offset
    rows = [e for e in record.get("midi", []) if abs(e[0] - centre) <= window_ms]
    out = [f"MIDI within {window_ms} ms of note #{note['index']} (ms relative to the score's start):"]
    for ms, port, channel, kind, a, b in rows:
        what = {"on": f"note on {pitch_name(a)} ({a}) vel {b}", "off": f"note off {pitch_name(a)} ({a})",
                "cc": f"CC{a} = {b}", "pb": f"pitch bend {a}", "pc": f"program {a}"}.get(kind.replace("bridge ", ""), kind)
        if kind.startswith("bridge "):
            what = "bridge command: " + ({"on": f"key {a} vel {b}", "off": f"key {a} off",
                                          "cc": f"CC{a} = {b}"}.get(kind[7:], kind[7:]))
        out.append(f"  {ms - offset:10.3f}  port {port} ch {channel}  {what}")
    return out if rows else out + ["  (none)"]


def report(record, notes=(), window_ms=50.0, limit=25):
    c = record["comparison"]
    s = record["summary"]
    expected = {n["index"]: n for n in record["expected"]["notes"]}
    out = [f"Run {record['run_id']}: {record['title']} ({record['test_id']}) "
           + ("PASSED" if record["pass"] else "FAILED"),
           f"  {s['passed']} of {s['expected']} notes passed; {s['failed']} failed, {s['missing']} missing, "
           f"{s['extra']} extra; {s['received']} received",
           f"  finished {record.get('finished_at')}; sample rate {record.get('sample_rate')}; "
           f"clock offset {c['offset_ms']} ms; tolerances onset ±{c['tolerances']['onset_ms']} ms, "
           f"duration ±{c['tolerances']['duration_ms']} ms, level ±{c['tolerances']['level']}"]
    if record.get("discarded_takes"):
        out.append(f"  {record['discarded_takes']} earlier playthrough(s) discarded")
    if record.get("dropped_midi_events"):
        out.append(f"  {record['dropped_midi_events']} MIDI events not recorded (limit reached)")
    out.append("Parts:")
    for b in c["bindings"]:
        if "port" in b:
            out.append(f"  {b['part']} ({b['instrument']}) <- port {b['port']} channel {b['channel']}, "
                       f"chair {b.get('chair_instrument')}, bound by {b['bound_by']}")
        else:
            out.append(f"  {b['part']} ({b['instrument']}) <- nothing: no notes from a chair playing it")
    bridges = (record.get("context") or {}).get("bridges") or []
    if bridges:
        out.append("Bridges at Done:")
        for b in bridges:
            if not b.get("bridge"):
                out.append(f"  port {b['port']} channel {b['channel']} ({b['instrument']}): no bridge map assigned")
                continue
            withheld = ", ".join(f"{k}: {v}" for k, v in (b.get("withheld_total") or {}).items()) or "none"
            out.append(f"  port {b['port']} channel {b['channel']} ({b['instrument']}): identity check "
                       f"{b.get('beacon')}, attested {'yes' if b.get('attested') else 'no'}, invalid commands "
                       f"{b.get('invalid_commands')}, notes withheld since load: {withheld}")
    bad = [n for n in c["notes"] if n["status"] != "pass"]
    if bad:
        out.append(f"Failed and missing notes ({len(bad)}):")
        for n in bad[:limit]:
            out += ["  " + line for line in note_report(record, n, expected[n["index"]])]
        if len(bad) > limit:
            out.append(f"  ... {len(bad) - limit} more (use --limit)")
    if c["extras"]:
        out.append(f"Extra notes ({len(c['extras'])}):")
        for r in c["extras"][:limit]:
            out += ["  " + line for line in received_text(r)]
    for index in notes:
        note = next((n for n in c["notes"] if n["index"] == index), None)
        if note is None:
            out.append(f"No note #{index} in this test.")
            continue
        if note["status"] == "pass":
            out += note_report(record, note, expected[index])
        out += midi_around(record, note, expected[index], window_ms)
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run", nargs="?", help="run ID or path to a run record")
    parser.add_argument("--note", type=int, action="append", default=[], help="show MIDI around this note index")
    parser.add_argument("--window", type=float, default=50.0, help="MIDI window for --note, in ms (default 50)")
    parser.add_argument("--limit", type=int, default=25, help="notes to show per section (default 25)")
    parser.add_argument("--list", nargs="?", const="", metavar="TEST_ID", help="list recent runs, newest first")
    args = parser.parse_args()
    if args.list is not None:
        rows = list_runs(args.list or None, args.limit)
        if not rows:
            print(f"No runs in {runs_folder()}")
        for r in rows:
            s = r["summary"]
            print(f"{r['run_id']}  {'pass' if r['pass'] else 'FAIL'}  {s['passed']}/{s['expected']} passed, "
                  f"{s['failed']} failed, {s['missing']} missing, {s['extra']} extra")
        return
    if not args.run:
        parser.error("give a run ID, a path, or --list")
    _, record = load(args.run)
    print(report(record, args.note, args.window, args.limit))


if __name__ == "__main__":
    main()
