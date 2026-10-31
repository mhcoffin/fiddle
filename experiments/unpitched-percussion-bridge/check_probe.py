#!/usr/bin/env python3
"""How does Dorico play an unpitched instrument through a percussion map
paired with a bridge expression map?

Reads a closed Fiddle MIDI Probe capture of unpitched-probe.musicxml
(make_probe_score.py; map A on channel 1, map B on channel 2, as README.md
sets Dorico up) and prints one verdict per unknown:

  U1 routing    does a technique the percussion map resolves also select the
                expression map's switches?  A1 both maps see every technique;
                A2 only techniques the percussion map left alone reach the
                expression map; B the expression map never switches.
  U2 rolls      one sustained note at the roll's pitch with CC1 following the
                hairpin, or repeated strokes; and the same without an entry.
  U3 combos     which entry a two-technique note picks, and what a technique
                without an entry becomes.
  U4 keyswitch  what an entry's own <keyswitches> field sends.
  U5 directions beaters and snares: add-on, pitch, both or neither.
  U6 velocity   the dynamics ladder on velocity, beat stress, and whether CC1
                arrives on hits too.

    python3 check_probe.py /path/to/capture.jsonl

Times are ms from the first musical note on channel 1 (bar 1 beat 1).
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "dorico-switch-order"))
from check_switch_order import NOTE_OFF, NOTE_ON, CONTROL_CHANGE, read_capture  # noqa: E402

TEMPO = 100
BEAT = 60000.0 / TEMPO
BAR = 4 * BEAT
TOLERANCE = 200.0   # ms: a note "at" a beat
LEAD = 120.0        # ms: a switch "before" a note
MUSICAL = (21, 114)
# Expected velocity (or CC1) for the ladder ppp..fff on the pinned linear curve.
LADDER = [40, 48, 56, 60, 68, 72, 80, 88]
CC = dict(base=102, dynamics=1, accent=104, beater=105, strike=106, dead_stroke=108, snares=112)
KS_GHOST = 4
PITCH = dict(natural=60, rim=61, sideStick=62, crossStick=63, roll=64, tremolo=65, buzzRoll=66,
             rim_roll=67, rods=68, rods_rim=69, accent=70, entry_keyswitch=15)
BEATER_RODS, SNARES_ON, SNARES_OFF = 2, 1, 2


def beat(bar, beat_number):
    return (bar - 1) * BAR + (beat_number - 1) * BEAT


def musical(pitch):
    return MUSICAL[0] <= pitch <= MUSICAL[1]


def last_pass(blocks):
    """Events [host, type, channel, data1, data2] of every channel in the last
    playing pass that has musical notes on channel 1."""
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
                  for block in run for e in block["events"]]
        if any(t == NOTE_ON and v > 0 and c == 1 and musical(p) for _, t, c, p, v in events):
            return events
    return []


class Channel:
    """One channel's events in ms from the pass origin, with note pairing."""

    def __init__(self, events, sample_rate, channel, origin):
        ms = lambda samples: (samples - origin) * 1000.0 / sample_rate
        self.events = [(ms(at), t, d1, d2) for at, t, c, d1, d2 in events if c == channel]
        self.notes = []  # (on_ms, pitch, velocity, off_ms or None)
        open_notes = {}
        for at, t, d1, d2 in self.events:
            if t == NOTE_ON and d2 > 0:
                open_notes.setdefault(d1, []).append(len(self.notes))
                self.notes.append([at, d1, d2, None])
            elif (t == NOTE_OFF or (t == NOTE_ON and d2 == 0)) and open_notes.get(d1):
                self.notes[open_notes[d1].pop(0)][3] = at

    def notes_between(self, start, end, musical_only=True):
        return [n for n in self.notes if start <= n[0] < end and (musical(n[1]) or not musical_only)]

    def note_at(self, t):
        hits = [n for n in self.notes if abs(n[0] - t) <= TOLERANCE and musical(n[1])]
        return min(hits, key=lambda n: abs(n[0] - t)) if hits else None

    def cc_before(self, number, t, lead=LEAD):
        """The last value of CC `number` sent in the lead before t (or up to 5 ms after)."""
        values = [d2 for at, ty, d1, d2 in self.events if ty == CONTROL_CHANGE and d1 == number and t - lead <= at <= t + 5]
        return values[-1] if values else None

    def cc_values(self, number, start, end):
        return [d2 for at, ty, d1, d2 in self.events if ty == CONTROL_CHANGE and d1 == number and start <= at < end]

    def cc_after(self, number, t, within=15.0):
        values = [d2 for at, ty, d1, d2 in self.events if ty == CONTROL_CHANGE and d1 == number and t - 5 <= at <= t + within]
        return values[-1] if values else None

    def keyswitch_before(self, pitch, t, lead=LEAD):
        hits = [n for n in self.notes if n[1] == pitch and t - lead <= n[0] <= t + 5]
        return hits[-1][2] if hits else None

    def cc_lane(self, number, start, end):
        return [(round(at - start, 1), d2) for at, ty, d1, d2 in self.events
                if ty == CONTROL_CHANGE and d1 == number and start - 5 <= at < end]


def u6_velocity(a):
    lines, deltas, levels = [], [], []
    for i, expected in enumerate(LADDER):
        t = beat(1 + i // 4, 1 + i % 4)
        note = a.note_at(t)
        if note is None:
            lines.append(f"  ladder {i + 1}: no note at {t:.0f} ms")
            deltas.append(None)
            continue
        cc1 = a.cc_after(CC["dynamics"], note[0])
        lines.append(f"  ladder {i + 1}: velocity {note[2]} (expected {expected}), CC1 {cc1}")
        deltas.append(note[2] - expected)
        levels.append(cc1)
    eighths = [a.note_at(beat(3, 1) + k * BEAT / 2) for k in range(8)]
    velocities = [n[2] if n else None for n in eighths]
    lines.append(f"  bar 3 eighths: velocities {velocities}")
    if any(d is None for d in deltas):
        verdict = "INCOMPLETE"
    elif all(d == 0 for d in deltas):
        verdict = "EXACT"
    else:
        verdict = "OFF " + " ".join(f"{d:+d}" for d in deltas)
    stress = ("NO-BEAT-STRESS" if all(v == 68 for v in velocities) else
              "BEAT-STRESS " + str(velocities) if all(v is not None for v in velocities) else "INCOMPLETE")
    cc1_on_hits = "CC1 also sent on hits" if any(v is not None for v in levels) else "no CC1 on hits"
    return dict(verdict=f"{verdict}; {stress}; {cc1_on_hits}", lines=lines)


ROLL_BASES = {0: "natural base", 1: "roll base", 2: "tremolo base", 3: "buzz roll base"}


def roll_base_name(code):
    """The probe's bases are natural 0-4, roll 5-9, tremolo 10-14, buzz roll 15-19."""
    if code is None:
        return "no base switch"
    return ROLL_BASES.get(code // 5, f"unknown base {code}") + (" - a roll-family base" if 5 <= code <= 19 else "")


def u1_routing(a):
    accent, marcato, ghost, plain = (a.note_at(beat(4, k)) for k in (1, 2, 3, 4))
    dead = a.note_at(beat(7, 4))
    roll = a.note_at(beat(5, 1))
    facts = dict(
        accent_pitch=accent[1] if accent else None,
        accent_cc=a.cc_before(CC["accent"], accent[0]) if accent else None,
        marcato_cc=a.cc_before(CC["accent"], marcato[0]) if marcato else None,
        ghost_ks=a.keyswitch_before(KS_GHOST, ghost[0]) if ghost else None,
        dead_pitch=dead[1] if dead else None,
        dead_cc=a.cc_before(CC["dead_stroke"], dead[0]) if dead else None,
        roll_base=a.cc_before(CC["base"], roll[0]) if roll else None)
    lines = [f"  bar 4 accent: pitch {facts['accent_pitch']}, CC104 {facts['accent_cc']}; "
             f"marcato: CC104 {facts['marcato_cc']}; ghost: KS4 velocity {facts['ghost_ks']}",
             f"  bar 7 dead stroke: pitch {facts['dead_pitch']}, CC108 {facts['dead_cc']}",
             f"  bar 5 roll: CC102 {facts['roll_base']} before the note ({roll_base_name(facts['roll_base'])})"]
    any_addon = any([facts["accent_cc"], facts["marcato_cc"], facts["ghost_ks"] == 2, facts["dead_cc"]])
    if accent is None or dead is None:
        verdict = "INCOMPLETE"
    elif facts["accent_pitch"] == PITCH["accent"] and facts["accent_cc"] == 1:
        verdict = "A1"
    elif facts["accent_pitch"] == PITCH["accent"] and facts["dead_cc"] == 1:
        verdict = "A2"
    elif not any_addon:
        verdict = "B"
    else:
        verdict = "MIXED"
    return dict(verdict=verdict, lines=lines, facts=facts)


def roll_shape(channel, start, end, label):
    notes = channel.notes_between(start, end)
    if not notes:
        return "DROPPED", [f"  {label}: no note"]
    longest = max(notes, key=lambda n: (n[3] or start) - n[0])
    length = (longest[3] - longest[0]) if longest[3] is not None else None
    lane = channel.cc_lane(CC["dynamics"], start, end)
    lines = [f"  {label}: {len(notes)} note-on(s); longest pitch {longest[1]} velocity {longest[2]} "
             f"length {length if length is None else round(length)} ms; CC1 {lane[:3]}..{lane[-3:]} ({len(lane)} values)"]
    if len(notes) == 1 and length is not None and length >= 2300:
        return "SUSTAINED", lines
    if len(notes) >= 4:
        return "STROKES", lines
    return "OTHER", lines


def u2_rolls(a, b):
    start, end = beat(5, 1), beat(6, 1)
    va, la = roll_shape(a, start, end, "map A bar 5 roll")
    vb, lb = roll_shape(b, start, end, "map B bar 5 roll (no entry)")
    return dict(verdict=f"A {va}; B FALLBACK-{vb}" if vb != "DROPPED" else f"A {va}; B DROPPED", lines=la + lb)


def u3_combos(a):
    combo = a.note_at(beat(6, 3))
    combo_strokes = a.notes_between(beat(6, 3) - TOLERANCE, beat(7, 1) - TOLERANCE)
    buzz = a.note_at(beat(6, 1))
    dead = a.note_at(beat(7, 4))
    xhead = a.note_at(beat(7, 3))
    roll = a.note_at(beat(5, 1))
    # What Dorico made of the written three-stroke tremolo decides which entry
    # the bar 6 combination could match: rim+roll exists, rim+tremolo does not.
    written = "tremolo" if roll and roll[1] == PITCH["tremolo"] else "roll"
    pitches9 = [n[1] for n in a.notes_between(beat(9, 1) - TOLERANCE, beat(10, 1) - TOLERANCE)]
    lines = [f"  bar 5 written roll resolves to pt.{written}",
             f"  bar 6 buzz roll: pitch {buzz[1] if buzz else None}",
             f"  bar 6 rim+{written}: pitch {combo[1] if combo else None}, {len(combo_strokes)} note-on(s) over the half note",
             f"  bar 7 x notehead: pitch {xhead[1] if xhead else None} (63 = imported as cross stick)",
             f"  bar 7 dead stroke (no entry): pitch {dead[1] if dead else None}"]
    if combo is None:
        verdict = "INCOMPLETE"
    elif combo[1] == PITCH["rim_roll"]:
        verdict = "EXACT-COMBO"
    elif combo[1] in (PITCH["roll"], PITCH["tremolo"]):
        verdict = "BEST-SUBSET (roll wins)"
    elif combo[1] == PITCH["rim"]:
        verdict = "BEST-SUBSET (rim wins)"
    elif combo[1] == PITCH["natural"]:
        verdict = "NATURAL-FALLBACK"
    else:
        verdict = f"OTHER pitch {combo[1]}"
    if written == "tremolo":
        verdict += " - the rim+roll entry never applied"
        if PITCH["rods_rim"] in pitches9:
            verdict += "; EXACT-COMBO shown by bar 9 rods+rim"
    if combo and combo[1] != PITCH["rim_roll"]:
        verdict += "; the combination's roll played as " + ("STROKES" if len(combo_strokes) >= 4 else "one note")
    verdict += "; unmatched technique " + ("DROP-WHEN-UNMATCHED" if dead is None else
                                           "falls back to Natural" if dead[1] == PITCH["natural"] else f"pitch {dead[1]}")
    return dict(verdict=verdict, lines=lines)


def u4_entry_keyswitch(a):
    start, end = beat(8, 1) - LEAD, beat(10, 1)
    sent = [n for n in a.notes if n[1] == PITCH["entry_keyswitch"] and start <= n[0] < end]
    hits = a.notes_between(beat(8, 1) - TOLERANCE, end)
    lines = []
    for n in sent:
        next_hit = min((h[0] for h in hits if h[0] >= n[0] - 5), default=None)
        length = None if n[3] is None else round(n[3] - n[0], 1)
        lead = None if next_hit is None else round(next_hit - n[0], 1)
        lines.append(f"  KS15 at {n[0]:.1f} ms: velocity {n[2]}, length {length} ms, {lead} ms before the next hit")
    if not sent:
        return dict(verdict="NOT-SENT", lines=["  no note-on at pitch 15 in bars 8-9"])
    keyed = [h for h in hits if h[1] == PITCH["rods"]]  # hits that used the keyswitched entry
    how = "once" if len(sent) == 1 else "per note" if len(sent) >= len(keyed) else "per change"
    return dict(verdict=f"SENT x{len(sent)} ({how})", lines=lines)


def u5_directions(a):
    rods = a.cc_values(CC["beater"], beat(8, 1) - LEAD, beat(10, 1))
    pitches8 = [n[1] for n in a.notes_between(beat(8, 1) - TOLERANCE, beat(9, 1) - TOLERANCE)]
    pitches9 = [n[1] for n in a.notes_between(beat(9, 1) - TOLERANCE, beat(10, 1) - TOLERANCE)]
    snares = a.cc_values(CC["snares"], beat(10, 1) - LEAD, beat(12, 1))
    pitches10 = [n[1] for n in a.notes_between(beat(10, 1) - TOLERANCE, beat(12, 1))]
    lines = [f"  bars 8-9 Rods: CC105 values {rods}; pitches bar 8 {pitches8}, bar 9 (rim) {pitches9}",
             f"  bars 10-11 Snares off/on: CC112 values {snares}; pitches {pitches10}"]

    def kind(addon, pitch):
        return "BOTH" if addon and pitch else "ADDON" if addon else "PITCH" if pitch else "NEITHER"
    beater = kind(BEATER_RODS in rods, any(p in (PITCH["rods"], PITCH["rods_rim"]) for p in pitches8 + pitches9))
    combo = ("rods+rim entry" if PITCH["rods_rim"] in pitches9 else "rim entry" if PITCH["rim"] in pitches9
             else "rods entry" if PITCH["rods"] in pitches9 else "natural" if PITCH["natural"] in pitches9 else "none")
    snare = kind(any(v in (SNARES_ON, SNARES_OFF) for v in snares), False)
    return dict(verdict=f"beater {beater} (bar 9 rods+rim -> {combo}); snares {snare}", lines=lines)


def analyze(events, sample_rate):
    origin = min((at for at, t, c, p, v in events if t == NOTE_ON and v > 0 and c == 1 and musical(p)), default=None)
    if origin is None:
        return None
    a, b = Channel(events, sample_rate, 1, origin), Channel(events, sample_rate, 2, origin)
    return dict(U6=u6_velocity(a), U1=u1_routing(a), U2=u2_rolls(a, b), U3=u3_combos(a),
                U4=u4_entry_keyswitch(a), U5=u5_directions(a))


def report(results):
    order = ["U1", "U2", "U3", "U4", "U5", "U6"]
    titles = dict(U1="technique routing", U2="rolls", U3="combinations and fallback",
                  U4="entry keyswitch", U5="direction techniques", U6="velocity dynamics")
    lines = []
    for key in order:
        lines.append(f"{key} {titles[key]}: {results[key]['verdict']}")
        lines.extend(results[key]["lines"])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capture", help="capture.jsonl from the newest fiddle-midi-probe-* folder in $TMPDIR")
    parser.add_argument("--extract", metavar="JSON", help="also save this pass's events (for test fixtures)")
    args = parser.parse_args()
    try:
        header, blocks = read_capture(args.capture)
    except (OSError, ValueError) as error:
        sys.exit(f"Can't read {args.capture}: {error}")
    events = last_pass(blocks)
    if args.extract:
        Path(args.extract).write_text(json.dumps(dict(sample_rate=header["sample_rate"], events=events)))
    results = analyze(events, header["sample_rate"])
    if results is None:
        sys.exit("No musical notes on channel 1: is map A's snare on channel 1 of the probe port?")
    print(report(results))


if __name__ == "__main__":
    main()
