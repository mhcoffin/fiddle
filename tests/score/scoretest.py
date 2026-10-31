"""Score tests: what Dorico should send Fiddle for a small score.

A test spec (specs/<id>.json) describes each part's music in a compact
notation. From it this module produces the MusicXML to load in Dorico and the
expected result: every sounding note with its onset, duration, dynamic level
and playing techniques as Fiddle's bridge decoder should receive them. The
spec is the single source of truth for both. See README.md.
"""
import functools
import hashlib
import json
import re
import xml.etree.ElementTree as ET
from fractions import Fraction
from pathlib import Path

DIVISIONS = 12  # per quarter note: sixteenths and eighth-note triplets fit
LENGTHS = {"w": 48, "h": 24, "q": 12, "e": 6, "s": 3}
TYPES = {48: ("whole", 0), 72: ("whole", 1), 24: ("half", 0), 36: ("half", 1), 12: ("quarter", 0),
         18: ("quarter", 1), 6: ("eighth", 0), 9: ("eighth", 1), 3: ("16th", 0)}
STEPS = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

# Instruments a spec may use. "dorico" is Dorico's instrument ID (Fiddle's
# playback template routes by it); "sound" is the MusicXML sound ID Dorico's
# import uses to choose that instrument. A transposing instrument gives its
# MusicXML <transpose> (sounding = written + chromatic) and its written key;
# specs always give sounding pitches.
INSTRUMENTS = {
    "violin": dict(name="Violin", sound="strings.violin", dorico="instrument.strings.violin",
                   clef=("G", 2), program=41),
    "viola": dict(name="Viola", sound="strings.viola", dorico="instrument.strings.viola",
                  clef=("C", 3), program=42),
    "flute": dict(name="Flute", sound="wind.flutes.flute", dorico="instrument.wind.flute",
                  clef=("G", 2), program=74),
    "horn": dict(name="Horn in F", sound="brass.french-horn", dorico="instrument.brass.horn.f",
                 clef=("G", 2), program=61, transpose=dict(diatonic=-4, chromatic=-7, fifths=1)),
    "timpani": dict(name="Timpani", sound="drum.timpani", dorico="instrument.pitchedpercussion.timpani",
                    clef=("F", 4), program=48),
    "marimba": dict(name="Marimba", sound="pitched-percussion.marimba",
                    dorico="instrument.pitchedpercussion.marimba", clef=("G", 2), program=13),
    # Unpitched instruments: notes are written as x heads ("x/q"), shown at
    # "unpitched" (display step, octave) on a percussion clef. Dorico sends
    # the pitch of the unpitched bridge's percussion-map entry it picks, so
    # each expected note's pitch comes from that map (percussion_code); r1
    # puts every entry on one pitch.
    "snare": dict(name="Snare Drum", sound="drum.snare-drum", dorico="instrument.orchestral.unpitched.snare.drum",
                  clef=("percussion", 2), program=1, unpitched=("C", 5)),
    "bass-drum": dict(name="Bass Drum", sound="drum.bass-drum", dorico="instrument.orchestral.unpitched.bass.drum",
                      clef=("percussion", 2), program=1, unpitched=("E", 4)),
    "suspended-cymbal": dict(name="Suspended Cymbal", sound="metal.cymbal.suspended",
                             dorico="instrument.orchestral.unpitched.suspended.cymbal",
                             clef=("percussion", 2), program=1, unpitched=("C", 5)),
    "triangle": dict(name="Triangle", sound="metal.triangle", dorico="instrument.orchestral.unpitched.triangle",
                     clef=("percussion", 2), program=1, unpitched=("C", 5)),
}
CODES_SOURCE = (Path(__file__).resolve().parents[2]
                / "experiments/unpitched-percussion-bridge/unpitched-percussion-r2/allocation.json")

# Dorico's dynamic levels (experiments/dynamics-curve/results-01.md).
LEVELS = dict(pppppp=-6, ppppp=-5, pppp=-4, ppp=-3, pp=-2, p=-1, mp=-0.5,
              mf=0.5, f=1, ff=2, fff=3, ffff=4, fffff=5, ffffff=6)

# Articulation names in specs -> (MusicXML element, Dorico playback technique).
ARTICULATIONS = {
    "stacc": ("staccato", "pt.staccato"),
    "staccmo": ("staccatissimo", "pt.staccatissimo"),
    "ten": ("tenuto", "pt.tenuto"),
    "acc": ("accent", "pt.accent"),
    "marc": ("strong-accent", "pt.marcato"),
}

# Marks that are MusicXML ornaments: (element, attributes, text) and the
# playback technique Dorico should send. A three-stroke tremolo on an
# unpitched note plays as pt.tremolo in Dorico 6 (results-01.md: not
# pt.roll); "unmeasured" with 0 marks is MusicXML 3.1's buzz roll (the
# tremolo-marks content is required, 0 meaning unmeasured).
ORNAMENTS = {
    "trem": (("tremolo", {"type": "single"}, "3"), "pt.tremolo"),
    "buzz": (("tremolo", {"type": "unmeasured"}, "0"), "pt.buzzRoll"),
}
# Noteheads carry no expected technique: whether Dorico's import turns them
# into one is what the unpitched probe finds out.
NOTEHEADS = {"xhead": ("x", {}), "ghost": ("normal", {"parentheses": "yes"})}
MARKS = set(ARTICULATIONS) | set(ORNAMENTS) | set(NOTEHEADS)


def articulation_elements(articulations):
    """MusicXML elements for a note's articulations. Tenuto with staccato is
    the single combined mark <detached-legato/>: Dorico imports separate
    <tenuto/> and <staccato/> elements as staccato alone."""
    names = [ARTICULATIONS[a][0] for a in articulations if a in ARTICULATIONS]
    if "tenuto" in names and "staccato" in names:
        names = ["detached-legato"] + [n for n in names if n not in ("tenuto", "staccato")]
    return names


@functools.lru_cache(maxsize=None)
def percussion_codes():
    """The unpitched bridge's percussion-map entries, [(pitch, frozenset of technique IDs)]."""
    manifest = json.loads(CODES_SOURCE.read_text())
    return [(c["pitch"], frozenset(c["techniques"])) for c in manifest["percussion"][0]["codes"]]


def percussion_code(techniques):
    """The pitch Dorico should send for an unpitched note: the entry whose
    techniques are all among the note's, with the most of them (ties: the
    fewest techniques, then table order), else Natural (results-01.md). In
    r1 every entry shares one pitch, so this is always that pitch; the rule
    is kept so a map with several pitches (kits) needs no new code."""
    wanted = set(techniques)
    best = None
    for pitch, ids in percussion_codes():
        if ids <= wanted and (best is None or (len(ids & wanted), -len(ids)) > (len(best[1] & wanted), -len(best[1]))):
            best = (pitch, ids)
    if best is None:
        best = next((pitch, ids) for pitch, ids in percussion_codes() if ids == {"pt.natural"})
    return best[0]


DEFAULT_TOLERANCES = dict(onset_ms=5.0, duration_ms=5.0, level=0.2)


class SpecError(ValueError):
    pass


def parse_pitch(text):
    m = re.fullmatch(r"([A-G])(#|b)?(-?\d)", text)
    if not m:
        raise SpecError(f"Bad pitch {text!r}")
    step, accidental, octave = m.groups()
    alter = {"#": 1, "b": -1, None: 0}[accidental]
    return step, alter, int(octave), 12 * (int(octave) + 1) + STEPS[step] + alter


def written_pitch(pitch, transpose):
    """(step, alter, octave) to write for a sounding pitch on a transposing
    instrument: the sounding note moved by the transposition's interval."""
    step, alter, octave, midi = pitch
    if not transpose:
        return step, alter, octave
    letters = "CDEFGAB"
    index = letters.index(step) - transpose["diatonic"]
    step, octave = letters[index % 7], octave + index // 7
    return step, midi - transpose["chromatic"] - (12 * (octave + 1) + STEPS[step]), octave


def key_alter(step, fifths):
    """The alteration a key signature of `fifths` gives a step."""
    if fifths > 0:
        return 1 if step in "FCGDAEB"[:fifths] else 0
    return -1 if step in "BEADGCF"[:-fifths] else 0


def parse_length(text):
    dots = len(text) - len(text.rstrip("."))
    base = text.rstrip(".")
    if base not in LENGTHS or dots > 1:
        raise SpecError(f"Bad length {text!r}")
    return LENGTHS[base] * (3 if dots else 2) // 2


def parse_music(text):
    """Events of one part, in order. Kinds: note, rest, dynamic, tempo,
    hairpin (start), hairpin_end, slur_start, slur_end, barline."""
    events = []
    for token in text.split():
        if token == "|":
            events.append(dict(kind="barline"))
        elif token.startswith("T="):
            events.append(dict(kind="tempo", bpm=int(token[2:])))
        elif token.startswith("!"):
            mark = token[1:]
            if mark not in LEVELS:
                raise SpecError(f"Unknown dynamic {token!r}")
            events.append(dict(kind="dynamic", mark=mark))
        elif token in ("<", ">"):
            events.append(dict(kind="hairpin", direction="up" if token == "<" else "down"))
        elif token == "]":
            events.append(dict(kind="hairpin_end"))
        elif token == "(":
            events.append(dict(kind="slur_start"))
        elif token == ")":
            events.append(dict(kind="slur_end"))
        else:
            head, _, length = token.partition("/")
            if not length:
                raise SpecError(f"Bad token {token!r}")
            tie = length.endswith("~")
            length = length.rstrip("~")
            length, *articulations = length.split("+")
            for a in articulations:
                if a not in MARKS:
                    raise SpecError(f"Unknown articulation {a!r} in {token!r}")
            if head == "r":
                events.append(dict(kind="rest", length=parse_length(length), token=token))
            else:
                # "x" is an unpitched hit.
                events.append(dict(kind="note", pitch=None if head == "x" else parse_pitch(head),
                                   length=parse_length(length), articulations=articulations, tie=tie, token=token))
    return events


def bar_length(spec):
    beats, beat_type = (int(x) for x in spec.get("time", "4/4").split("/"))
    return beats * DIVISIONS * 4 // beat_type


def timeline(spec):
    """Per part, events with their position (divisions); the tempo map; the
    score's total length. Validates bar lines and part lengths."""
    bar = bar_length(spec)
    parts, tempo, total = [], [(0, spec.get("tempo", 100))], None
    for index, part in enumerate(spec["parts"]):
        if part["instrument"] not in INSTRUMENTS:
            raise SpecError(f"Unknown instrument {part['instrument']!r}")
        unpitched = "unpitched" in INSTRUMENTS[part["instrument"]]
        position, placed = 0, []
        for event in parse_music(part["music"]):
            event = dict(event, position=position)
            if event["kind"] == "barline" and position % bar:
                raise SpecError(f"{part['id']}: bar line at {position} is not on a bar boundary")
            if event["kind"] == "note" and (event["pitch"] is None) != unpitched:
                raise SpecError(f"{part['id']}: {event['token']}: unpitched instruments take x heads, pitched ones pitches")
            if event["kind"] == "tempo":
                if index:
                    raise SpecError("Tempo changes belong in the first part")
                tempo.append((position, event["bpm"]))
            if event["kind"] in ("note", "rest"):
                if position // bar != (position + event["length"] - 1) // bar:
                    raise SpecError(f"{part['id']}: {event['token']} crosses a bar line; tie it")
                position += event["length"]
            placed.append(event)
        if position % bar:
            raise SpecError(f"{part['id']}: the last bar is incomplete")
        if total is not None and position != total:
            raise SpecError("All parts must be the same length")
        total = position
        parts.append(placed)
    tempo = sorted({p: b for p, b in tempo}.items())
    return parts, tempo, total


def milliseconds(position, tempo):
    """Time of a position (divisions) under a tempo map [(position, bpm)]."""
    ms = 0.0
    for i, (start, bpm) in enumerate(tempo):
        end = tempo[i + 1][0] if i + 1 < len(tempo) else None
        if position <= start:
            break
        span = (min(position, end) if end is not None else position) - start
        ms += span * 60000.0 / (bpm * DIVISIONS)
    return ms


def sounding_notes(spec):
    """The notes Dorico plays: tie chains merged, with dynamics, hairpins,
    slurs and articulations resolved into what Fiddle should receive."""
    parts, tempo, _ = timeline(spec)
    rows, hairpin_id = [], 0
    for part, events in zip(spec["parts"], parts):
        level, hairpin, in_slur, chain = None, None, False, None
        pending = []  # notes of the part in order, before slur ends are known
        hairpins = []
        for i, event in enumerate(events):
            kind = event["kind"]
            if kind == "dynamic":
                level = LEVELS[event["mark"]]
                if hairpin and hairpin.get("closed") and hairpin.get("to") is None:
                    hairpin["to"] = level
            elif kind == "hairpin":
                if level is None:
                    raise SpecError(f"{part['id']}: a hairpin needs a dynamic before it")
                hairpin_id += 1
                hairpin = dict(id=hairpin_id, direction=event["direction"], start=event["position"],
                               **{"from": level}, to=None, closed=False, end=None)
                hairpins.append(hairpin)
            elif kind == "hairpin_end":
                if not hairpin:
                    raise SpecError(f"{part['id']}: hairpin end without a hairpin")
                hairpin["closed"] = True
                hairpin["end"] = event["position"]
            elif kind == "slur_start":
                in_slur = True
            elif kind == "slur_end":
                if not pending:
                    raise SpecError(f"{part['id']}: slur end before any note")
                in_slur = False
            elif kind == "note":
                midi = event["pitch"][3] if event["pitch"] else None
                if chain is not None:  # continuation of a tie
                    if chain["pitch"] != midi:
                        raise SpecError(f"{part['id']}: tie to a different pitch at {event['token']}")
                    chain["length"] += event["length"]
                    if not event["tie"]:
                        chain = None
                    continue
                if level is None:
                    raise SpecError(f"{part['id']}: give a dynamic before the first note")
                note = dict(part=part["id"], pitch=midi, position=event["position"],
                            length=event["length"], articulations=event["articulations"],
                            slurred=in_slur, level=level, token=event["token"],
                            hairpin=hairpin if hairpin and not hairpin["closed"] else None)
                pending.append(note)
                chain = note if event["tie"] else None
        if chain is not None:
            raise SpecError(f"{part['id']}: the last tie has no following note")
        for h in hairpins:
            if not h["closed"]:
                raise SpecError(f"{part['id']}: hairpin {h['id']} is never ended")
        rows.extend(pending)
    rows.sort(key=lambda r: (r["position"], [p["id"] for p in spec["parts"]].index(r["part"]), r["pitch"] or 0))
    bar = bar_length(spec)
    unpitched = {p["id"] for p in spec["parts"] if "unpitched" in INSTRUMENTS[p["instrument"]]}
    result = []
    for r in rows:
        start = milliseconds(r["position"], tempo)
        dynamic = dict(level=r["level"])
        h = r["hairpin"]
        if h:
            # The dynamic keeps moving while the note sounds (live CC1).
            dynamic["during"] = dict(direction=h["direction"], to=h["to"])
        # Dorico moves the level in a straight line over time from the
        # hairpin's start to its end (hairpins-20261029-190413).
        along = None
        if h and h["to"] is not None:
            h_start, h_end = milliseconds(h["start"], tempo), milliseconds(h["end"], tempo)
            along = lambda ms: h["from"] + (h["to"] - h["from"]) * min(1.0, (ms - h_start) / (h_end - h_start))
            end = milliseconds(r["position"] + r["length"], tempo)
            dynamic["during"]["end_level"] = round(along(end), 3)
        if h and r["position"] > h["start"]:
            bounds = sorted([h["from"], h["to"] if h["to"] is not None else h["from"]])
            dynamic = dict(hairpin=h["id"], direction=h["direction"], **{"from": h["from"]}, to=h["to"],
                           during=dynamic["during"])
            if h["to"] is not None:
                dynamic["level"] = round(along(start), 3)
                dynamic["between"] = bounds
        includes = sorted(ARTICULATIONS[a][1] for a in r["articulations"] if a in ARTICULATIONS)
        includes += sorted(ORNAMENTS[a][1] for a in r["articulations"] if a in ORNAMENTS)
        # Every slurred note, the slur's last included (slurs-20261029-190133).
        if r["slurred"]:
            includes.append("pt.legato")
        excludes = sorted(pt for name, (_, pt) in ARTICULATIONS.items() if name not in r["articulations"])
        if r["part"] in unpitched:
            excludes += sorted(pt for name, (_, pt) in ORNAMENTS.items() if name not in r["articulations"])
        if not r["slurred"]:
            excludes.append("pt.legato")
        pitch = percussion_code(includes) if r["part"] in unpitched else r["pitch"]
        result.append(dict(part=r["part"], index=len(result), pitch=pitch,
                           bar=r["position"] // bar + 1,
                           beat=float(Fraction(r["position"] % bar, DIVISIONS)) + 1,
                           onset_ms=round(start, 3),
                           duration_ms=round(milliseconds(r["position"] + r["length"], tempo) - start, 3),
                           dynamic=dynamic,
                           techniques=dict(includes=sorted(includes), excludes=sorted(excludes)),
                           source=r["token"]))
    return result


def sub(parent, tag, value=None, **attrs):
    node = ET.SubElement(parent, tag, attrs)
    if value is not None:
        node.text = str(value)
    return node


def musicxml(spec):
    parts, tempo, total = timeline(spec)
    bar = bar_length(spec)
    beats, beat_type = spec.get("time", "4/4").split("/")
    root = ET.Element("score-partwise", version="3.1")
    sub(sub(root, "work"), "work-title", spec["title"])
    part_list = sub(root, "part-list")
    for channel, part in enumerate(spec["parts"], start=1):
        info = INSTRUMENTS[part["instrument"]]
        sp = sub(part_list, "score-part", id=part["id"])
        sub(sp, "part-name", part.get("name", info["name"]))
        si = sub(sp, "score-instrument", id=f"{part['id']}-I1")
        sub(si, "instrument-name", info["name"])
        sub(si, "instrument-sound", info["sound"])
        midi = sub(sp, "midi-instrument", id=f"{part['id']}-I1")
        sub(midi, "midi-channel", channel)
        sub(midi, "midi-program", info["program"])
    tempo_at = dict(tempo)
    for index, (part, events) in enumerate(zip(spec["parts"], parts)):
        info = INSTRUMENTS[part["instrument"]]
        fifths = info.get("transpose", {}).get("fifths", 0)
        p = sub(root, "part", id=part["id"])
        measures = {}

        def measure(number):
            if number not in measures:
                m = sub(p, "measure", number=str(number))
                measures[number] = m
                if number == 1:
                    attrs = sub(m, "attributes")
                    sub(attrs, "divisions", DIVISIONS)
                    sub(sub(attrs, "key"), "fifths", fifths)
                    time = sub(attrs, "time")
                    sub(time, "beats", beats)
                    sub(time, "beat-type", beat_type)
                    clef = sub(attrs, "clef")
                    sub(clef, "sign", info["clef"][0])
                    sub(clef, "line", info["clef"][1])
                    if "transpose" in info:
                        transpose = sub(attrs, "transpose")
                        sub(transpose, "diatonic", info["transpose"]["diatonic"])
                        sub(transpose, "chromatic", info["transpose"]["chromatic"])
            return measures[number]

        slur_open = tie_open = False
        for event in events:
            kind, position = event["kind"], event["position"]
            number = min(position // bar + 1, total // bar)
            m = measure(number)
            if index == 0 and kind in ("note", "rest") and position in tempo_at:
                d = sub(m, "direction", placement="above")
                metronome = sub(sub(d, "direction-type"), "metronome")
                sub(metronome, "beat-unit", "quarter")
                sub(metronome, "per-minute", tempo_at[position])
                sub(d, "sound", tempo=str(tempo_at.pop(position)))
            if kind == "dynamic":
                d = sub(m, "direction", placement="below")
                sub(sub(sub(d, "direction-type"), "dynamics"), event["mark"])
            elif kind == "hairpin":
                d = sub(m, "direction", placement="below")
                sub(sub(d, "direction-type"), "wedge",
                    type="crescendo" if event["direction"] == "up" else "diminuendo")
            elif kind == "hairpin_end":
                # At a bar boundary the wedge ends at the end of the previous bar.
                target = measures.get(position // bar) if position % bar == 0 and position else m
                d = sub(target if target is not None else m, "direction", placement="below")
                sub(sub(d, "direction-type"), "wedge", type="stop")
            elif kind == "slur_start":
                slur_open = "start"
            elif kind == "slur_end":
                last = p.findall(".//note")[-1]
                notations = last.find("notations")
                if notations is None:
                    notations = sub(last, "notations")
                sub(notations, "slur", type="stop", number="1")
            elif kind in ("note", "rest"):
                n = sub(m, "note")
                alter = 0
                if kind == "rest":
                    sub(n, "rest")
                elif event["pitch"] is None:
                    shown = sub(n, "unpitched")
                    sub(shown, "display-step", info["unpitched"][0])
                    sub(shown, "display-octave", info["unpitched"][1])
                else:
                    step, alter, octave = written_pitch(event["pitch"], info.get("transpose"))
                    pitch = sub(n, "pitch")
                    sub(pitch, "step", step)
                    if alter:
                        sub(pitch, "alter", alter)
                    sub(pitch, "octave", octave)
                sub(n, "duration", event["length"])
                tie_types = []
                if kind == "note":
                    if tie_open:
                        tie_types.append("stop")
                    if event["tie"]:
                        tie_types.append("start")
                    tie_open = event["tie"]
                for t in tie_types:
                    sub(n, "tie", type=t)
                sub(n, "voice", 1)
                note_type, dots = TYPES[event["length"]]
                sub(n, "type", note_type)
                for _ in range(dots):
                    sub(n, "dot")
                if kind == "note" and event["pitch"] is not None and alter != key_alter(step, fifths):
                    sub(n, "accidental", {1: "sharp", -1: "flat", 0: "natural"}[alter])
                heads = [NOTEHEADS[a] for a in event.get("articulations", []) if a in NOTEHEADS]
                if heads:
                    sub(n, "notehead", heads[0][0], **heads[0][1])
                if kind == "note" and (tie_types or event["articulations"] or slur_open):
                    notations = sub(n, "notations")
                    for t in tie_types:
                        sub(notations, "tied", type=t)
                    if slur_open:
                        sub(notations, "slur", type="start", number="1")
                        slur_open = False
                    if any(a in ARTICULATIONS for a in event["articulations"]):
                        arts = sub(notations, "articulations")
                        for element in articulation_elements(event["articulations"]):
                            sub(arts, element)
                    ornaments = [ORNAMENTS[a][0] for a in event["articulations"] if a in ORNAMENTS]
                    if ornaments:
                        container = sub(notations, "ornaments")
                        for element, attrs, text in ornaments:
                            sub(container, element, text, **attrs)
        last = measures[max(measures)]
        sub(sub(last, "barline", location="right"), "bar-style", "light-heavy")
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def expected(spec, spec_bytes):
    parts, tempo, total = timeline(spec)
    return dict(
        schema_version=1, id=spec["id"], title=spec["title"],
        description=spec.get("description", ""), musicxml=f"{spec['id']}.musicxml",
        spec_sha256=hashlib.sha256(spec_bytes).hexdigest(),
        time=spec.get("time", "4/4"),
        tempo_map=[dict(quarter=float(Fraction(p, DIVISIONS)), bpm=b) for p, b in tempo],
        length_ms=round(milliseconds(total, tempo), 3),
        parts=[dict(id=p["id"], name=p.get("name", INSTRUMENTS[p["instrument"]]["name"]),
                    instrument=INSTRUMENTS[p["instrument"]]["dorico"],
                    **({"unpitched": True} if "unpitched" in INSTRUMENTS[p["instrument"]] else {}))
               for p in spec["parts"]],
        tolerances={**DEFAULT_TOLERANCES, **spec.get("tolerances", {})},
        notes=sounding_notes(spec))


def load_spec(path):
    data = path.read_bytes()
    spec = json.loads(data)
    if spec.get("id") != path.stem:
        raise SpecError(f"{path.name}: id must be {path.stem!r}")
    return spec, data
