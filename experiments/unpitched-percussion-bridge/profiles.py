"""Orchestral unpitched percussion r2 (candidate) and the Phase 0 probe.

Dorico resolves an unpitched note's techniques through a percussion map
(instrument + techniques -> pitch) paired with the expression map on the
endpoint. Here the two are one library, and the expression map does the
work, exactly as for pitched instruments: strokes and surfaces (rim, side
stick, bell, closed/open...) and modifiers (accent, dynamic marks, ghost,
damping, beaters, snares) are add-on dimensions; hit bases take velocity
dynamics and every roll kind is a CC102 base with CC1 dynamics so a hairpin
rides a roll. The percussion map only makes Dorico play: every instrument
gets one pitch, for Natural and for each roll kind, because Dorico sends the
pitch of the entry it picks and sustains a roll only when that entry carries
the roll (results-01.md, finding 2). The pitch names nothing; the chair
names the instrument.

The probe (README.md, unknowns U0-U6) established that both maps see every
technique and that an unmatched technique falls back to the Natural entry,
which is what lets the percussion map be this small.
"""
import importlib.util
from pathlib import Path
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent


def load(name, path):
    """Import a sibling experiment's module by path (its name clashes with ours)."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


pitched = load("pitched_profiles", HERE.parent / "orchestral-percussion-bridge" / "profiles.py")
engine, strings = pitched.engine, pitched.strings

# Hit bases keep the universal map's rows (and Dorico's flags); every roll
# kind is a base too, so Dorico selects CC1 dynamics for it.
HIT_BASES = ["pt.natural", "pt.staccatissimo", "pt.staccato", "pt.staccato+pt.tenuto", "pt.tenuto"]
ROLL_FAMILY = ["pt.roll", "pt.tremolo", "pt.buzzRoll", "pt.pressRoll", "pt.flatRoll", "pt.circularRoll",
               "pt.swooshRoll", "pt.trill", "pt.trill.half", "pt.trill.whole"]
BASES = [(ids, "velocity") for ids in HIT_BASES] + [(ids, "cc1") for ids in ROLL_FAMILY]

# Strokes and surfaces extend the pitched map's strike_location dimension on
# its conduit: centre, edge and rim keep their pitched values, the rest
# follow. Openness (hi-hat) is its own mechanism. Dorico's mutual-exclusion
# groups make each one value at a time, like any add-on.
STROKES = ("centre edgeDrums rim "
           "edgeCymbals rimShot rimKnock crossStick sideStick shell bell frame skin hiHat "
           "stickShot stickClick stickSnap backstick standClick tap click ping ding foot scrape swish slap "
           "slapCongas bassTone shake crush sizzle zing").split()
OPENNESS = ["closed", "halfOpen", "open2", "pedal"]

# The percussion map: one pitch per instrument (MIDI 21..114, outside the
# keyswitch conduits and the beacon), carried by the Natural entry and by
# one entry per roll kind so rolls sustain. Drum-set kits would give each
# component its own pitch; r1 covers single instruments only.
PITCH = 36
ENTRIES = ["pt.natural"] + ROLL_FAMILY
CODES = [(PITCH, pt) for pt in ENTRIES]

# Dorico 6's built-in unpitched instruments in scope: the orchestral set,
# toms, bongos and congas, tambourine, blocks and cowbells, and the stray
# cymbals and kick drums filed under instrument.unpitched. From
# /Applications/Dorico 6.app/Contents/Resources/instruments.xml (2026-10-30);
# aliases are separate IDs there and get their own entries. Kits
# (instrument.kit.*) are later work. BridgeSections.h must match.
INSTRUMENTS = [
    "instrument.band.unpitched.floortom.high", "instrument.band.unpitched.floortom.low",
    "instrument.band.unpitched.roto.tom.high", "instrument.band.unpitched.roto.tom.low",
    "instrument.band.unpitched.roto.tom.medium", "instrument.band.unpitched.roto.tom.medium-high",
    "instrument.band.unpitched.roto.tom.medium-low",
    "instrument.band.unpitched.tomtom.high", "instrument.band.unpitched.tomtom.high.alias.tom-high",
    "instrument.band.unpitched.tomtom.low", "instrument.band.unpitched.tomtom.low.alias.tom-low",
    "instrument.band.unpitched.tomtom.medium", "instrument.band.unpitched.tomtom.medium.alias.tom-medium",
    "instrument.band.unpitched.tomtom.medium-high",
    "instrument.band.unpitched.tomtom.medium-high.alias.tom-medium-high",
    "instrument.band.unpitched.tomtom.medium-low",
    "instrument.band.unpitched.tomtom.medium-low.alias.tom-medium-low",
    "instrument.latin.unpitched.bongo.bell.high", "instrument.latin.unpitched.bongo.bell.low",
    "instrument.latin.unpitched.bongo.high", "instrument.latin.unpitched.bongo.low",
    "instrument.latin.unpitched.conga.high", "instrument.latin.unpitched.conga.low",
    "instrument.latin.unpitched.conga.medium", "instrument.latin.unpitched.conga.very.high",
    "instrument.latin.unpitched.conga.very.low",
    "instrument.orchestral.unpitched.anvil", "instrument.orchestral.unpitched.bass.drum",
    "instrument.orchestral.unpitched.belltree", "instrument.orchestral.unpitched.clash.cymbals",
    "instrument.orchestral.unpitched.crash.cymbal.high", "instrument.orchestral.unpitched.crash.cymbal.low",
    "instrument.orchestral.unpitched.hihat", "instrument.orchestral.unpitched.hihat.pedal",
    "instrument.orchestral.unpitched.large.gong", "instrument.orchestral.unpitched.opera.gong",
    "instrument.orchestral.unpitched.ratchet", "instrument.orchestral.unpitched.ride.cymbal.high",
    "instrument.orchestral.unpitched.ride.cymbal.low", "instrument.orchestral.unpitched.side.drum",
    "instrument.orchestral.unpitched.sizzle.cymbal", "instrument.orchestral.unpitched.sleigh.bells",
    "instrument.orchestral.unpitched.small.gong", "instrument.orchestral.unpitched.snare.drum",
    "instrument.orchestral.unpitched.suspended.cymbal", "instrument.orchestral.unpitched.tamtam",
    "instrument.orchestral.unpitched.temple.high", "instrument.orchestral.unpitched.temple.low",
    "instrument.orchestral.unpitched.temple.medium", "instrument.orchestral.unpitched.temple.medium.high",
    "instrument.orchestral.unpitched.temple.medium.low", "instrument.orchestral.unpitched.tenor.drum",
    "instrument.orchestral.unpitched.tenor.drum.high", "instrument.orchestral.unpitched.tenor.drum.medium",
    "instrument.orchestral.unpitched.tenor.drum.medium.high",
    "instrument.orchestral.unpitched.tenor.drum.medium.low", "instrument.orchestral.unpitched.thundersheet",
    "instrument.orchestral.unpitched.triangle", "instrument.orchestral.unpitched.whip",
    "instrument.orchestral.unpitched.whistle", "instrument.orchestral.unpitched.wind.gong",
    "instrument.percussion.other.cowbell.high", "instrument.percussion.other.cowbell.low",
    "instrument.percussion.other.tambourine",
    "instrument.percussion.other.wood.block.high", "instrument.percussion.other.wood.block.low",
    "instrument.percussion.other.wood.block.medium", "instrument.percussion.other.wood.block.medium.high",
    "instrument.percussion.other.wood.block.medium.low",
    "instrument.unpitched.china.cymbal", "instrument.unpitched.flexatone",
    "instrument.unpitched.kick.drum.low", "instrument.unpitched.kick.drum.very.low",
    "instrument.unpitched.ribboncrasher", "instrument.unpitched.splash.cymbal",
]

# Add-on dimensions: the strings/pitched ones whose PTs modify a stroke,
# on their pitched-percussion conduits, plus the percussion mechanisms.
COMMON_IDS = ("accent", "dynamic_level", "dynamic_attack", "dynamic_shape", "fade_in", "fade_out",
              "soft", "ghost_note", "damp", "laissez_vibrer", "simile")
PITCHED_IDS = ("beater", "dead_stroke", "muffled")
MECHANISMS = [
    ("snares", "snareOn snareOff", False,
     "Snare mechanism, a direction that holds until changed. An explicit Off is not the unspecified state."),
    ("throwoff", "throwoffOn throwoffOff", False,
     "Throw-off lever, kept apart from the snare state: verify what notation emits it before merging."),
    ("choke", "choke", False,
     "Cymbal choke: a damping action on one stroke, independent of dead stroke and muffling."),
    ("openness", " ".join(OPENNESS), False,
     "Hi-hat openness and the pedal stroke, a direction that holds until changed."),
]
STRIKE_LOCATION = ("strike_location", " ".join(STROKES), True,
                   "Where, or with what, the instrument is struck: the pitched map's centre/edge/rim on the same "
                   "conduit and values, then every other built-in stroke and surface. Natural returns to the normal spot.")
pitched_dims = {d["id"]: d for d in engine.dimensions(pitched.PITCHED_PERCUSSION_R2)}
by_id = {d[0]: d for d in pitched.DIMENSIONS}
DIMENSIONS = [by_id[key] for key in COMMON_IDS + PITCHED_IDS] + [STRIKE_LOCATION] + MECHANISMS
# Shared dimensions keep the pitched conduits and values (strike_location its
# conduit and first values); new ones take conduits no pitched dimension
# uses, so one CC means one thing in both maps.
wire = {key: (pitched_dims[key]["kind"], pitched_dims[key]["number"])
        for key in COMMON_IDS + PITCHED_IDS + ("strike_location",)}
taken = {(d["kind"], d["number"]) for d in pitched_dims.values()}
free = iter(pair for pair in pitched.SCHEMA_TWO_POOL if pair not in taken)
for key, *_ in MECHANISMS:
    wire[key] = next(free)
values = {key: pitched_dims[key]["values"] for key in COMMON_IDS + PITCHED_IDS}

FAMILY_EXCLUSIONS = {
    "A": "Articulation or ornament without a distinct unpitched realization; hits take the base articulations.",
    "C": "Core technique outside unpitched percussion.",
    "S": "Bowed/plucked-string technique outside unpitched percussion.",
    "W": "Wind/brass/vocal technique outside unpitched percussion.",
    "G": "Guitar or pitch gesture outside unpitched percussion.",
    "P": "Stick-part, hand, motion, tension and tabla variants: r1 keeps the generic stroke; "
         "add values when a score test needs them.",
    "R": "Specialized gesture not in r1's strike_location dimension; add a value when a score test needs it.",
    "K": "Keyboard pedal PT; pedal controllers pass through as MIDI.",
    "E": "Electronic/FX control outside this acoustic section profile.",
}

UNPITCHED_R2 = SimpleNamespace(
    REVISION=2, WIRE_SCHEMA=2, WRITTEN_DURATIONS=True, SECTION="UnpitchedPercussion",
    LABEL="Orchestral unpitched percussion", TITLE="Orchestral unpitched percussion",
    NAME="Fiddle Bridge - Orchestral Unpitched Percussion r2 (host test)",
    ENTITY_ID="xmap.user.Fiddle.OrchestralUnpitchedPercussion.r2",
    BASES=BASES, BASE_IDS=set(strings.BASE_IDS) | set(ROLL_FAMILY), ALIASES={},
    DIMENSIONS=DIMENSIONS, CONDUITS=wire, VALUES=values,
    INSTRUMENTS=INSTRUMENTS, EXTRA_MUTEX_GROUPS=[], EXCLUSIONS={}, FAMILY_EXCLUSIONS=FAMILY_EXCLUSIONS,
    PERCUSSION=SimpleNamespace(
        ENTITY_ID="drumkitmap.user.Fiddle.OrchestralUnpitchedPercussion.r2",
        NAME="Fiddle Bridge - Orchestral Unpitched Percussion r2 (host test)",
        INSTRUMENTS=INSTRUMENTS, CODES=CODES, FOR_SINGLE_INSTRUMENT=True),
)

# The Phase 0 probe: a snare drum through two percussion maps. Map A has
# strokes, rolls, a combination, an entry with Dorico's own keyswitch field
# and an accent entry; rim and accent are deliberately also add-ons, dead
# stroke deliberately only an add-on. Map B has Natural alone, so rolls
# through it show what Dorico does without an entry. README.md explains the
# score that exercises them.
SNARE = "instrument.orchestral.unpitched.snare.drum"
PROBE_DIMENSIONS = [by_id[key] for key in ("accent", "beater", "strike_location", "dead_stroke",
                                            "ghost_note", "damp")] + [MECHANISMS[0]]
PROBE = SimpleNamespace(
    REVISION=1, WIRE_SCHEMA=2, WRITTEN_DURATIONS=True, SECTION="UnpitchedProbe",
    LABEL="Unpitched percussion probe", TITLE="Unpitched percussion probe",
    NAME="Fiddle Bridge - Unpitched Percussion Probe r1",
    ENTITY_ID="xmap.user.Fiddle.UnpitchedProbe.r1",
    BASES=[("pt.natural", "velocity"), ("pt.roll", "cc1"), ("pt.tremolo", "cc1"), ("pt.buzzRoll", "cc1")],
    BASE_IDS={"pt.natural", "pt.roll", "pt.tremolo", "pt.buzzRoll"}, ALIASES={},
    DIMENSIONS=PROBE_DIMENSIONS,
    CONDUITS={**{k: (pitched_dims[k]["kind"], pitched_dims[k]["number"]) for k in
                 ("accent", "beater", "strike_location", "dead_stroke", "ghost_note", "damp")},
              "snares": wire["snares"]},
    VALUES={k: pitched_dims[k]["values"] for k in ("accent", "beater", "strike_location", "dead_stroke",
                                                   "ghost_note", "damp")},
    INSTRUMENTS=[SNARE], EXTRA_MUTEX_GROUPS=[], EXCLUSIONS={}, FAMILY_EXCLUSIONS=FAMILY_EXCLUSIONS,
    PERCUSSION=[
        SimpleNamespace(ENTITY_ID="drumkitmap.user.Fiddle.UnpitchedProbe.A", NAME="Fiddle Unpitched Probe A",
                        INSTRUMENTS=[SNARE], FOR_SINGLE_INSTRUMENT=True,
                        CODES=[(60, "pt.natural"), (61, "pt.rim"), (62, "pt.sideStick"), (63, "pt.crossStick"),
                               (64, "pt.roll"), (65, "pt.tremolo"), (66, "pt.buzzRoll"), (67, "pt.rim+pt.roll"),
                               (68, "pt.beaterRods", 15), (69, "pt.beaterRods+pt.rim"), (70, "pt.accent")]),
        SimpleNamespace(ENTITY_ID="drumkitmap.user.Fiddle.UnpitchedProbe.B", NAME="Fiddle Unpitched Probe B",
                        INSTRUMENTS=[SNARE], FOR_SINGLE_INSTRUMENT=True, CODES=[(60, "pt.natural")]),
    ],
)

REVISIONS = [UNPITCHED_R2]
