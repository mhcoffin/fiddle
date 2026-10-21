#!/usr/bin/env python3
"""Analyse a closed MIDI Probe capture of the dynamics measurement score.

Reports, per pass of the score: the CC1 value (channel 1, CC1 map) and note
velocity (both channels) Dorico sent for every written dynamic, then fits
Dorico's level-to-MIDI curve, the articulation adjustments, and 7-bit
resolution. Values are the probe's raw normalized floats (0..1), i.e. what
Dorico sent before MIDI rounding. Optionally prints the playback-override
formats found in an expression map exported from Dorico.

Read-only: no installation, routing or playback.
"""
import argparse
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
NOTE_ON, CONTROL_CHANGE = 0, 3  # RealtimeMidiEventType
CC_CHANNEL, VELOCITY_CHANNEL = 1, 2
# Dorico sends a new dynamic's CC1 up to a few ms after the note-on it
# belongs to (observed 0-7 ms), so a note's CC1 is the value in force this
# long after it starts, unless the next note comes sooner.
CC1_SETTLE_MS = 15.0

# Dorico's level for each written dynamic (results-01.md): the p family
# counts down from -1, the f family up from +1, mp/mf are -/+0.5.
LEVELS = dict(pppppp=-6, ppppp=-5, pppp=-4, ppp=-3, pp=-2, p=-1, mp=-0.5,
              mf=0.5, f=1, ff=2, fff=3, ffff=4, fffff=5, ffffff=6)


def read_capture(path):
    """Validated (header, blocks); each block carries its input events."""
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


def passes(blocks):
    """Runs of consecutive playing blocks, in order."""
    result, current = [], []
    for block in blocks:
        if block["playing"]:
            current.append(block)
        elif current:
            result.append(current)
            current = []
    if current:
        result.append(current)
    return result


def channel_events(blocks, channel):
    """Note-ons [(host, pitch, velocity)] and CC1 values [(host, value)]."""
    notes, cc1 = [], []
    for block in blocks:
        for e in block["events"]:
            if e["channel"] != channel:
                continue
            if e["type"] == NOTE_ON and e["data2"] > 0:
                notes.append((e["host_samples"], e["data1"], e["raw_value"]))
            elif e["type"] == CONTROL_CHANGE and e["data1"] == 1:
                cc1.append((e["host_samples"], e["raw_value"]))
    return notes, cc1


def measure(blocks, expected, channel, sample_rate=48000.0):
    """Per expected note: velocity, CC1 in force just after its start, and
    (for the held hairpin note) the CC1 values sent while it sounds. None if
    this pass does not contain the complete score on this channel."""
    notes, cc1 = channel_events(blocks, channel)
    if [pitch for _, pitch, _ in notes] != [n["pitch"] for n in expected]:
        return None
    rows = []
    settle = CC1_SETTLE_MS * sample_rate / 1000.0
    for i, ((host, _, velocity), spec) in enumerate(zip(notes, expected)):
        next_on = notes[i + 1][0] if i + 1 < len(notes) else math.inf
        before = [value for at, value in cc1 if at <= min(host + settle, next_on - 1)]
        row = dict(spec, velocity=velocity, cc1=before[-1] if before else None)
        if spec["kind"] == "hairpin":
            end = notes[i + 1][0] if i + 1 < len(notes) else math.inf
            row["cc1_track"] = [value for at, value in cc1 if host < at < end]
        rows.append(row)
    return rows


def ladder(rows, key):
    """Mean value per written dynamic over the ladder bars, and the spread."""
    values = {}
    for r in rows:
        if r["kind"] == "ladder" and r[key] is not None:
            values.setdefault(r["dynamic"], []).append(r[key])
    return {mark: (sum(v) / len(v), max(v) - min(v)) for mark, v in values.items()}


def shape(x, power, form):
    """Candidate curve shapes on [0, 1]. "s-curve" bends each half toward the
    middle: Dorico's dynamicCurveParams "0.5,p,p" (midpoint, lower and upper
    powers)."""
    if form == "power":
        return x ** power
    if form == "root":
        return x ** (1.0 / power)
    if x < 0.5:
        return 0.5 * (2 * x) ** power
    return 1 - 0.5 * (2 * (1 - x)) ** power


def unshape(y, power, form):
    if form == "power":
        return y ** (1.0 / power)
    if form == "root":
        return y ** power
    if y < 0.5:
        return 0.5 * (2 * y) ** (1.0 / power)
    return 1 - 0.5 * (2 * (1 - y)) ** (1.0 / power)


def model(level, setting, form):
    """Normalized output (0..1, before MIDI rounding) of a candidate curve."""
    span = setting["max_level"] - setting["min_level"]
    x = min(1.0, max(0.0, (level - setting["min_level"]) / span))
    return shape(x, setting["curve_power"], form)


def midi_code(level, setting, form):
    """Dorico's 7-bit output: 1 + 126 x curve, rounded (results-01.md)."""
    return int(math.floor(1 + 126 * model(level, setting, form) + 0.5))


def inverse(value, setting, form):
    span = setting["max_level"] - setting["min_level"]
    return setting["min_level"] + unshape(value, setting["curve_power"], form) * span


FORMS = ("power", "root", "s-curve")


def fit(measured, settings, ladder_marks):
    """Levels from the linear pass, then which curve form explains the rest.
    `measured` maps pass name -> {key: ladder means}; key is cc1 or velocity."""
    report = {}
    for key in ("cc1", "velocity"):
        linear = next((s for s in settings if s["curve_power"] == 1.0
                       and (s["min_level"], s["max_level"]) == (-6, 6) and s["name"] in measured), None)
        if linear is None:
            continue
        base = measured[linear["name"]][key]
        if not base:
            continue
        levels = {m: linear["min_level"] + base[m][0] * (linear["max_level"] - linear["min_level"])
                  for m in ladder_marks if m in base}
        forms = {}
        for form in FORMS:
            errors = []
            for s in settings:
                if s["name"] not in measured or s is linear:
                    continue
                values = measured[s["name"]][key]
                errors += [values[m][0] - model(levels[m], s, form) for m in levels if m in values]
            forms[form] = max((abs(e) for e in errors), default=None)
        best = min((f for f in forms if forms[f] is not None), key=lambda f: forms[f], default=None)
        report[key] = dict(levels=levels, max_error_by_form=forms, best_form=best)
    return report


def articulation_deltas(rows, setting, form):
    """Level added by each articulation: inverse(articulated) - inverse(plain
    note before it), using the fitted curve."""
    deltas = {}
    for previous, row in zip(rows, rows[1:]):
        if row["articulation"] and previous["pitch"] == row["pitch"] and not previous["articulation"]:
            for key in ("cc1", "velocity"):
                if row[key] is not None and previous[key] is not None:
                    deltas.setdefault(row["articulation"], {})[key] = (
                        inverse(row[key], setting, form) - inverse(previous[key], setting, form))
    return deltas


def exact_matches(measured, settings, form):
    """How many ladder values equal midi_code() of the LEVELS table, per key."""
    result = {}
    for key in ("cc1", "velocity"):
        hits = total = 0
        for s in settings:
            for mark, (mean, _) in measured.get(s["name"], {}).get(key, {}).items():
                total += 1
                hits += round(mean * 127) == midi_code(LEVELS[mark], s, form)
        result[key] = (hits, total)
    return result


def resolution(levels, setting, form):
    """Smallest gap between adjacent dynamics after 7-bit rounding."""
    codes = sorted(round(127 * model(level, setting, form)) for level in levels.values())
    return min((b - a for a, b in zip(codes, codes[1:])), default=None)


def override_formats(path):
    """option -> value strings in an exported map's playback overrides."""
    root = ET.parse(path).getroot()
    result = {}
    for row in root.iter("optionsOverride"):
        result[row.findtext("option")] = row.findtext("value")
    return result


def analyze(blocks, expected, select=None, sample_rate=48000.0):
    complete = []
    for index, pass_blocks in enumerate(passes(blocks), start=1):
        per_channel = {ch: measure(pass_blocks, expected["notes"], ch, sample_rate)
                       for ch in (CC_CHANNEL, VELOCITY_CHANNEL)}
        if all(per_channel.values()):
            complete.append((index, per_channel))
    if select:
        chosen = [c for c in complete if c[0] in select]
    else:
        chosen = complete[-len(expected["passes"]):]
    if len(chosen) != len(expected["passes"]):
        raise ValueError(f"Found {len(complete)} complete passes {[c[0] for c in complete]}; "
                         f"expected {len(expected['passes'])}. Use --select to name them.")
    settings = expected["passes"]
    result = dict(passes=[], fit=None)
    measured = {}
    for setting, (index, per_channel) in zip(settings, chosen):
        cc_rows, velocity_rows = per_channel[CC_CHANNEL], per_channel[VELOCITY_CHANNEL]
        measured[setting["name"]] = dict(cc1=ladder(cc_rows, "cc1"),
                                         velocity=ladder(velocity_rows, "velocity"))
        hairpin = next(r for r in cc_rows if r["kind"] == "hairpin")
        result["passes"].append(dict(
            name=setting["name"], capture_pass=index, setting=setting,
            cc1_map=dict(ladder_cc1=measured[setting["name"]]["cc1"],
                         ladder_velocity=ladder(cc_rows, "velocity")),
            velocity_map=dict(ladder_velocity=measured[setting["name"]]["velocity"]),
            hairpin=dict(start=hairpin["cc1"], points=len(hairpin["cc1_track"]),
                         values=hairpin["cc1_track"]),
            rows=dict(cc1_map=cc_rows, velocity_map=velocity_rows)))
    fitted = fit(measured, settings, expected["ladder"])
    result["fit"] = fitted
    result["exact"] = {form: exact_matches(measured, settings, form) for form in FORMS}
    for p in result["passes"]:
        s = p["setting"]
        p["articulations"] = {}
        p["resolution_codes"] = {}
        for key, channel_rows in (("cc1", p["rows"]["cc1_map"]), ("velocity", p["rows"]["velocity_map"])):
            if key not in fitted or not fitted[key]["best_form"]:
                continue
            form = fitted[key]["best_form"]
            rows = [r for r in channel_rows if r["kind"] == "articulation"]
            for art, values in articulation_deltas(rows, s, form).items():
                if key in values:
                    p["articulations"].setdefault(art, {})[key] = values[key]
            p["resolution_codes"][key] = resolution(fitted[key]["levels"], s, form)
    return result


def print_report(result):
    for p in result["passes"]:
        s = p["setting"]
        print(f"Pass {p['name']} (capture pass {p['capture_pass']}): power {s['curve_power']}, "
              f"levels {s['min_level']}..{s['max_level']}, accent {s['accent']}, "
              f"marcato {s['marcato']}, tenuto {s['tenuto']}")
        cc, cv = p["cc1_map"]["ladder_cc1"], p["cc1_map"]["ladder_velocity"]
        vv = p["velocity_map"]["ladder_velocity"]
        print("  dynamic   CC1(map1)  vel(map1)  vel(map2)   spread")
        for mark in cc:
            spread = max(cc[mark][1], vv.get(mark, (0, 0))[1])
            print(f"  {mark:8} {cc[mark][0]:9.4f} {cv[mark][0]:10.4f} {vv.get(mark, (math.nan,))[0]:10.4f}"
                  f" {spread:8.4f}")
        h = p["hairpin"]
        print(f"  hairpin p->f: start CC1 {h['start']}, {h['points']} CC1 values while held")
        for art, values in p["articulations"].items():
            print(f"  {art}: level delta " + ", ".join(f"{k} {v:+.3f}" for k, v in values.items()))
        print(f"  smallest 7-bit gap between adjacent dynamics: {p['resolution_codes']}")
    for key, f in (result["fit"] or {}).items():
        print(f"Fit ({key}): levels from the linear pass: "
              + ", ".join(f"{m} {v:+.2f}" for m, v in f["levels"].items()))
        print(f"  max error by curve form: {f['max_error_by_form']} -> best: {f['best_form']}")
    for form, counts in result.get("exact", {}).items():
        print(f"Exact 7-bit matches with the LEVELS table, {form}: "
              + ", ".join(f"{k} {h}/{t}" for k, (h, t) in counts.items()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, nargs="?")
    parser.add_argument("--select", help="comma-separated capture pass numbers, one per kit pass")
    parser.add_argument("--json", type=Path, help="also write the full result as JSON")
    parser.add_argument("--overrides", type=Path,
                        help="an expression map exported from Dorico with playback overrides set")
    args = parser.parse_args()
    if args.overrides:
        for option, value in override_formats(args.overrides).items():
            print(f"{option} = {value}")
    if args.capture:
        expected = json.loads((HERE / "dynamics-probe.expected.json").read_text())
        select = [int(x) for x in args.select.split(",")] if args.select else None
        header, blocks = read_capture(args.capture)
        result = analyze(blocks, expected, select, header.get("sample_rate", 48000.0))
        print_report(result)
        if args.json:
            args.json.write_text(json.dumps(result, indent=2) + "\n")
    elif not args.overrides:
        parser.error("give a capture, --overrides, or both")


if __name__ == "__main__":
    main()
