#!/usr/bin/env python3
"""Does Dorico still send a switch's Off after restating its On?

Reads a closed Fiddle MIDI Probe capture of one of this folder's scores and
reports, for the add-on controller (Flautando, CC112 in the Strings r2 map):

  * every base change (CC102) while the add-on should stay active, since a
    base change is what triggered the bug;
  * every Off (CC112=0) while the add-on should stay active, with the On it
    followed: the reported bug is "On for the next note, then Off for the
    previous one", 16 ms apart;
  * the add-on value in force at each note-on.

The add-on should be active from the first note until the final onset group
(the notes marked nat.), so the Off that ends it just before that group is
expected and not counted.

    python3 check_switch_order.py /path/to/capture.jsonl

Verdict: FIXED (the trigger occurred and no Off cut the add-on), STILL
PRESENT, or INCONCLUSIVE (no base change happened while the add-on was
active, so this capture can't tell).
"""
import argparse
import json
import sys
from pathlib import Path

NOTE_ON, NOTE_OFF, CONTROL_CHANGE = 0, 1, 3
GROUP_MS = 50.0  # note-ons this close together form one onset group


def read_capture(path):
    """Validated (header, blocks) from a closed MIDI Probe schema-1 capture."""
    rows = [json.loads(line) for line in Path(path).read_text().splitlines() if line.strip()]
    if (len(rows) < 2 or rows[0].get("kind") != "header" or rows[0].get("schema") != 1
            or rows[-1].get("kind") != "end"):
        raise ValueError("Expected a closed MIDI Probe schema-1 capture; quit Dorico so the probe "
                         "writes its footer")
    if rows[-1]["capture_dropped"]:
        raise ValueError("The capture overflowed; this take is incomplete")
    blocks = []
    for row in rows[1:-1]:
        if row["kind"] == "block":
            blocks.append({**row, "events": []})
        elif row["kind"] == "input" and blocks and row["block"] == blocks[-1]["block"]:
            blocks[-1]["events"].append(row)
        else:
            raise ValueError("Malformed or out-of-order capture record")
    for block in blocks:
        if block["input_dropped"] or block["input_invalid"]:
            raise ValueError(f"Block {block['block']}: dropped or invalid input")
    return rows[0], blocks


def playback_events(blocks, channel, musical):
    """Events [host, type, channel, data1, data2] of the last playing pass that
    has musical notes on this channel, in capture order."""
    passes, current = [], []
    for block in blocks:
        if block["playing"]:
            current.append(block)
        elif current:
            passes.append(current)
            current = []
    if current:
        passes.append(current)
    for run in reversed(passes):
        events = [[e["host_samples"], e["type"], e["channel"], e["data1"], e["data2"]]
                  for block in run for e in block["events"] if e["channel"] == channel]
        if any(t == NOTE_ON and v > 0 and musical(p) for _, t, _, p, v in events):
            return events
    return []


def analyze(events, sample_rate, addon_cc=112, base_cc=102, musical=lambda pitch: 21 <= pitch <= 114):
    ms = lambda samples: samples * 1000.0 / sample_rate
    notes = [(at, pitch) for at, t, _, pitch, v in events if t == NOTE_ON and v > 0 and musical(pitch)]
    if not notes:
        return dict(verdict="INCONCLUSIVE", reason="no musical notes in the capture on this channel", lines=[])
    # The add-on should be active from the first note to the last note before
    # the final onset group (nat.).
    last = notes[-1][0]
    final_group = [n for n in notes if ms(last - n[0]) <= GROUP_MS]
    active = [n for n in notes if n not in final_group]
    if not active:
        return dict(verdict="INCONCLUSIVE", reason="only one onset group; nothing should carry the add-on", lines=[])
    start, end = active[0][0], final_group[0][0]
    lines, offs, base_changes = [], [], []
    addon, base, last_on = None, None, None
    note_index = 0
    for at, t, _, number, value in events:
        if t == CONTROL_CHANGE and number == addon_cc:
            # Offs from just before the first note (Dorico's lead) to the
            # final group; the switch before it is checked below.
            if value == 0 and start - sample_rate * 0.1 < at < end:
                offs.append((at, last_on))
            elif value != 0:
                last_on = at
            addon = value
        elif t == CONTROL_CHANGE and number == base_cc:
            if base is not None and value != base and start <= at < end:
                base_changes.append((at, base, value))
            base = value
        elif t == NOTE_ON and value > 0 and musical(number):
            state = "active" if note_index < len(active) else "final (nat.)"
            lines.append(f"  {ms(at - start):9.2f} ms  note {number:3d}  CC{addon_cc}={addon}  CC{base_cc}={base}  ({state})")
            note_index += 1
    # The last Off before the final group ends the add-on for nat.: expected,
    # if nothing turned the add-on back on after it.
    if offs and all(not (offs[-1][0] < at < end) for at, t, _, n, v in events
                    if t == CONTROL_CHANGE and n == addon_cc and v != 0):
        offs.pop()
    missing = [line for line in lines if "(active)" in line
               and (f"CC{addon_cc}=0 " in line or f"CC{addon_cc}=None " in line)]
    # An Off that an On repairs before the next note is the correct order
    # (old Off, new On); one left in force is the bug (new On, old Off).
    note_ons = [at for at, t, _, p, v in events if t == NOTE_ON and v > 0 and musical(p)]
    ons = [at for at, t, _, n, v in events if t == CONTROL_CHANGE and n == addon_cc and v != 0]
    harmful, repaired = [], []
    for at, on in offs:
        next_note = min((n for n in note_ons if n >= at), default=float("inf"))
        (repaired if any(at <= o <= next_note for o in ons) else harmful).append((at, on))
    report = [f"Base changes while the add-on should be active: {len(base_changes)}"]
    report += [f"  {ms(at - start):9.2f} ms  CC{base_cc} {old} -> {new}" for at, old, new in base_changes]
    report.append(f"Offs (CC{addon_cc}=0) left in force while the add-on should be active "
                  f"(On, then Off: the bug): {len(harmful)}")
    for at, on in harmful:
        gap = f", {ms(at - on):.2f} ms after the On at {ms(on - start):.2f} ms" if on is not None else ""
        report.append(f"  {ms(at - start):9.2f} ms  CC{addon_cc}=0{gap}")
    report.append(f"Offs followed by an On before the next note (Off, then On: harmless): {len(repaired)}")
    report += [f"  {ms(at - start):9.2f} ms  CC{addon_cc}=0" for at, _ in repaired]
    report.append(f"Add-on and base at each note-on (ms from the first note):")
    report += lines
    if harmful or missing:
        verdict = "STILL PRESENT"
        reason = (f"{len(harmful)} Off(s) cut the add-on while it should stay active"
                  + (f"; {len(missing)} note(s) started without it" if missing else ""))
    elif not base_changes:
        verdict, reason = "INCONCLUSIVE", "no base change while the add-on was active, so the trigger never happened"
    else:
        verdict, reason = "FIXED", f"{len(base_changes)} base change(s) under the add-on and no Off cut it"
    return dict(verdict=verdict, reason=reason, lines=report, offs=harmful, repaired=repaired,
                base_changes=base_changes, missing=missing)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capture", help="capture.jsonl from the newest fiddle-midi-probe-* folder in $TMPDIR")
    parser.add_argument("--channel", type=int, default=1)
    parser.add_argument("--addon-cc", type=int, default=112, help="add-on controller (Flautando: 112)")
    parser.add_argument("--base-cc", type=int, default=102, help="base switch controller (102)")
    parser.add_argument("--extract", metavar="JSON", help="also save this pass's events (for test fixtures)")
    args = parser.parse_args()
    try:
        header, blocks = read_capture(args.capture)
    except (OSError, ValueError) as error:
        sys.exit(f"Can't read {args.capture}: {error}")
    events = playback_events(blocks, args.channel, lambda p: 21 <= p <= 114)
    if args.extract:
        Path(args.extract).write_text(json.dumps(dict(sample_rate=header["sample_rate"], events=events)) + "\n")
    result = analyze(events, header["sample_rate"], args.addon_cc, args.base_cc)
    print(f"{result['verdict']}: {result['reason']}")
    print("\n".join(result["lines"]))


if __name__ == "__main__":
    main()
