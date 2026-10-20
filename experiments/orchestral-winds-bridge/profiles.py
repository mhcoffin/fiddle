"""Curated woodwind/brass r1 inspection profiles; exact built-in Dorico IDs.

Use the existing base encoding and shared strings-r3 musical dimensions.
Common PTs have the same conduit, reset and On value in every current profile.
Section-specific techniques remain explicit; neither profile is a decoder.
"""
from types import SimpleNamespace

import generate as engine
import profile_r3 as strings

COMMON_IDS = (
    "connection", "accent", "vibrato", "tremolo", "trill",
    "dynamic_level", "dynamic_attack", "dynamic_shape", "fade_in", "fade_out",
    "soft", "ghost_note", "tremblement", "glissando", "portamento", "simile",
)
STRING_DIMENSIONS = {d[0]: d for d in strings.DIMENSIONS}
COMMON = [STRING_DIMENSIONS[key] for key in COMMON_IDS]

TONGUING = [
    ("tonguing", "doubleTongue tripleTongue", False,
     "Double and triple articulation patterns are alternatives; independent of flutter tongue and note shortening."),
    ("flutter_tongue", "flutterTongue", False,
     "Flutter tonguing is its own dimension, not a global replacement for tonguing, vibrato, or mute state."),
    ("growl", "growl", False,
     "Independent rough/growling colour; no inferred equivalence to flutter tongue or multiphonics."),
    ("multiphonic", "multiphonic", False,
     "Intent to produce multiphonics; no fingering, chord pitches or added notes are synthesized by this encoding."),
    ("alternative_fingering", "alternativeFingering", False,
     "Alternate fingering/valve or slide-position intent, independent of multiphonics and ordinary articulation."),
]
PITCH_GESTURES = [
    ("scoop", "scoop", False, "Upward entry gesture; independent of a later exit gesture."),
    ("plop", "plop", False, "Downward entry gesture; preserved independently until generic/compound use is host-reviewed."),
    ("fall", "fallDrop", False, "Falling exit gesture, including saxophone/trumpet effects; not an alias for glissando."),
    ("doit", "doit", False, "Rising exit gesture; independent of entry ornaments."),
    ("bend", "bend", False, "Embouchure/pitch-bending intent, not guitar pre-bending. Does not encode a continuous pitch curve."),
]
MUTE = ("mute", "muted open1", False,
        "Generic muted versus explicitly open intent. Unspecified is not Open; Natural does not automatically remove a mute.")

WOODWIND_ONLY = [
    MUTE,
    ("harmonic", "naturalHarmonic1 naturalHarmonic2", False,
     "Flute-family overblown harmonics. Exact numbered canonical IDs retained without claiming numbered partial semantics."),
    ("slap_tongue", "slapTongue", False,
     "Reed/flute percussive tongue attack, separate from multiple tonguing and key clicks."),
    ("key_click", "keyClick keyClickWithPitch", False,
     "Unpitched versus pitched key-click variants; proposed alternatives for the same keyed attack."),
    ("jet_whistle", "jetWhistle", False,
     "Flute jet-whistle effect, independent of keyed attacks and pitched whistle tones."),
    ("mouthpiece_pop", "mouthpiecePop", False,
     "Instrument/mouthpiece percussive pop; retain independently of slap tongue and tongue stop."),
    ("tongue_stop", "tongueStop", False,
     "Tongue-stop/ram effect, with instrument-specific realization; not an automatic note-length change."),
    ("tongue_click", "tongueClick", False,
     "Tongue click through the flute/wind instrument; not a generic finger click or vocal Dut."),
    ("whistle_tone", "whistleTone", False,
     "Flute whistle-tone technique; distinct from whistling or vocal production."),
]
BRASS_ONLY = [
    ("mute", "muted open1 halfMuted bellMute bucketMute cupMute straightMute harmonMuteStemExtended harmonMuteStemIn harmonMuteStemOut", False,
     "Inserted/generic mute choices including exact Harmon stem variants. Plunger is separate so it can coexist with an inserted mute."),
    ("plunger", "plungerMute", False,
     "Independent external plunger, including plunger with straight mute. Explicit Open cancels it via an extra mutex group."),
    ("stopped", "stopped", False,
     "Hand-stopped horn, independent of the inserted-mute enum. Explicit Open cancels it; no automatic pitch transposition is added."),
    ("cuivre", "cuivre", False,
     "Brassy/forced tone colour, especially horn; not synonymous with accent, stopped horn or louder dynamics."),
    ("mouthpiece_pop", "mouthpiecePop", False,
     "Mouthpiece percussive attack; independent of continuous tone modifiers."),
    ("tongue_stop", "tongueStop", False,
     "Explicit tongue-stop effect; no guessed pitch or release synthesis."),
    ("shake", "shake", False,
     "Lip shake/trill gesture, independent of the explicit fingered trill interval PTs."),
    ("slide", "slide", False,
     "Trombone/valve slide gesture retained as its own exact PT; not silently aliased to Portamento or Glissando."),
    ("smear", "smear", False,
     "Brass smear, especially trombone; independent storage without a guessed combination rule."),
    ("rip", "rip", False,
     "Brass upward rip across partials; does not encode or generate intermediate pitches."),
]

WOODWIND_DIMENSIONS = COMMON + TONGUING + WOODWIND_ONLY + PITCH_GESTURES
BRASS_DIMENSIONS = COMMON + TONGUING + BRASS_ONLY + PITCH_GESTURES

# Pin every common dimension and named value to the existing strings encoding.
# New dimensions shared by both sections get common assignments too. Unique
# section dimensions reuse the remaining pool independently, so an absent flute
# key-click dimension cannot strand a free brass CC.
reference = {d["id"]: d for d in engine.dimensions(strings)}
union = {}
for row in WOODWIND_DIMENSIONS + BRASS_DIMENSIONS:
    union.setdefault(row[0], row)
pinned = {key: (reference[key]["kind"], reference[key]["number"])
          for key in union if key in reference}
pool = [("CC", n) for n in range(103, 120)]
pool += [("KS", n) for n in list(range(21)) + list(range(115, 128))]
shared_ids = ({d[0] for d in WOODWIND_DIMENSIONS}
              & {d[0] for d in BRASS_DIMENSIONS})
free = iter(pair for pair in pool if pair not in pinned.values())
wire = dict(pinned)
for key in union:
    if key in shared_ids and key not in wire:
        wire[key] = next(free)

FAMILY_EXCLUSIONS = {
    "A06": "Percussion roll variant outside this section; explicit Tremolo retained.",
    "A09": "Arpeggiated chord-spreading PT; outside this default one-voice wind/brass profile.",
    "S": "Bowed/plucked-string technique outside this section (explicit flute harmonic exceptions are listed as add-ons).",
    "W": "Wind/brass/vocal technique not relevant to this section's default instrument vocabulary; see explicit inclusions/exclusions.",
    "G": "Guitar or specialized pitch gesture deferred; explicit relevant wind/brass gestures are included separately.",
    "P": "Percussion mechanism/gesture outside this section.",
    "R": "Percussion-specific event outside this section.",
    "K": "Keyboard/pedal mechanism outside this section.",
    "E": "Electronic/FX control outside this acoustic section profile.",
}
SHARED_EXCLUSIONS = {
    "pt.fingerClick": "Generic finger-click/foley event; not an instrument key click. Deferred to customization.",
    "pt.tongueFingerClick": "Combined generic tongue/finger click event; deferred rather than inventing a wind realization.",
    "pt.vocalDut": "Vocal Dut syllable, outside these instrumental profiles.",
    "pt.spoken": "Spoken voice production, not a wind/brass playing method.",
    "pt.whisper": "Generic vocal whisper PT; not silently treated as an instrumental air-tone command.",
    "pt.whistle": "Generic whistling; distinct from the explicit flute Whistle tone technique.",
    "pt.mouthClosed": "Vocal mouth-opening vocabulary, not a wind embouchure/airflow protocol.",
    "pt.mouthSlightlyOpen": "Vocal mouth-opening vocabulary, not a wind embouchure/airflow protocol.",
    "pt.mouthOpen": "Vocal mouth-opening vocabulary, not a wind embouchure/airflow protocol.",
    "pt.mouthWideOpen": "Vocal mouth-opening vocabulary, not a wind embouchure/airflow protocol.",
    "pt.mouthPursed": "Vocal mouth-shape PT; not silently interpreted as an instrumental embouchure instruction.",
    "pt.nasalVoice": "Vocal timbre instruction outside these instrumental profiles.",
}


def make(section, dimensions, instruments, exclusions, extra_groups=()):
    assignments = {key: wire[key] for key, *_ in dimensions if key in wire}
    free = iter(pair for pair in pool if pair not in assignments.values())
    for key, *_ in dimensions:
        if key not in assignments:
            assignments[key] = next(free)
    values = {}
    for key, members, _, _ in dimensions:
        if key in reference:
            old = reference[key]["values"]
            # Preserve values of shared PTs and append section-specific choices.
            next_value = max(old.values()) + 1
            values[key] = {}
            for suffix in members.split():
                pt = "pt." + suffix
                if pt in old:
                    values[key][pt] = old[pt]
                else:
                    values[key][pt] = next_value
                    next_value += 1
    return SimpleNamespace(
        REVISION=1, SECTION=section, LABEL=f"Orchestral {section.lower()}",
        TITLE=f"Orchestral {section.lower()}",
        NAME=f"Fiddle Bridge - Orchestral {section} r1 - INSPECTION ONLY",
        ENTITY_ID=f"xmap.user.Fiddle.Orchestral{section}.Inspection.r1",
        BASE_IDS=strings.BASE_IDS, ALIASES={}, DIMENSIONS=dimensions,
        CONDUITS=assignments, VALUES=values,
        INSTRUMENTS=instruments, EXTRA_MUTEX_GROUPS=list(extra_groups),
        EXCLUSIONS={**SHARED_EXCLUSIONS, **exclusions}, FAMILY_EXCLUSIONS=FAMILY_EXCLUSIONS,
    )


WOODWINDS = make("Woodwinds", WOODWIND_DIMENSIONS, [
    "Piccolo; concert, alto and bass flutes",
    "Oboe; oboe d'amore; English horn; bass oboe/heckelphone",
    "E-flat, B-flat and A clarinets; basset horn; bass and contrabass clarinets",
    "Bassoon; contrabassoon",
    "Soprano, alto, tenor, baritone and bass saxophones when used orchestrally",
], {
    "pt.cuivre": "Brassy horn/brass colour; not assigned a default woodwind interpretation.",
    "pt.stopped": "Hand-stopped horn technique, outside woodwinds.",
    "pt.shake": "Lip shake belongs to the brass candidate; woodwinds retain explicit Trill and Tremblement IDs.",
    **{"pt." + pt: "Brass-specific inserted/external mute type; woodwinds retain generic Muted/Open only."
       for pt in "halfMuted bellMute bucketMute cupMute straightMute harmonMuteStemExtended harmonMuteStemIn harmonMuteStemOut plungerMute".split()},
})

BRASS = make("Brass", BRASS_DIMENSIONS, [
    "French horn; Wagner tuba",
    "Piccolo, B-flat, C, D and E-flat trumpets; cornet; flugelhorn; bass trumpet",
    "Alto, tenor, bass and contrabass trombones",
    "Euphonium/baritone horn; bass and contrabass tubas",
], {
    **{"pt." + pt: "Woodwind keyed/flute/reed attack technique, outside the default brass vocabulary."
       for pt in "keyClick keyClickWithPitch jetWhistle whistleTone tongueClick slapTongue".split()},
    "pt.naturalHarmonic1": "Explicit flute/string harmonic PT; ordinary brass partial selection is not this articulation.",
    "pt.naturalHarmonic2": "Explicit flute/string harmonic PT; ordinary brass partial selection is not this articulation.",
}, extra_groups=[
    ("open_stopped", ["pt.open1", "pt.stopped"]),
    ("open_plunger", ["pt.open1", "pt.plungerMute"]),
])

PROFILES = {"woodwinds": WOODWINDS, "brass": BRASS}


def schema2(spec):
    """Same allocation and grouping on wire schema 2 (typed Off + beacon)."""
    section = spec.SECTION
    return SimpleNamespace(**{**vars(spec), "REVISION": 2, "WIRE_SCHEMA": 2,
                              "NAME": f"Fiddle Bridge - Orchestral {section} r2 (host test)",
                              "ENTITY_ID": f"xmap.user.Fiddle.Orchestral{section}.r2"})


# r1 artifacts are frozen schema-1 inspection candidates; r2 is current.
CURRENT = {key: schema2(spec) for key, spec in PROFILES.items()}
