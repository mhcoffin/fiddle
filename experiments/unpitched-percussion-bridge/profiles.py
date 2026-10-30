"""Orchestral unpitched percussion r1 (candidate) and the Phase 0 probe.

Dorico resolves an unpitched note's techniques through a percussion map
(instrument + techniques -> pitch) paired with the expression map on the
endpoint. Here the two are one library: strokes (what the note is: natural,
rim, side stick, bell, closed/open, the roll family...) are percussion-map
pitch codes, the same table for every instrument, so the pitch names the
stroke and the chair names the instrument. Modifiers (accent, dynamic marks,
ghost, damping, beaters, snares) stay add-on dimensions on the pitched
percussion conduits. Hit bases take velocity dynamics; the roll family also
has CC102 bases with CC1 dynamics so a hairpin rides a roll.

Which of this Dorico honours is unknown until the probe is played
(README.md, unknowns U0-U6); the r1 candidate assumes both maps see every
technique and is regenerated from results-01.md.
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

# Pitch codes (MIDI notes 21..114; keyswitch conduits and the beacon lie
# outside). 21-35 and 90-114 are reserved: drum-set kit components, and the
# stroke x modifier combinations the probe may show Dorico needs.
STROKES = ("centre edgeDrums edgeCymbals rim rimShot rimKnock crossStick sideStick shell bell frame skin hiHat "
           "stickShot stickClick stickSnap backstick standClick tap click ping ding foot scrape swish slap "
           "slapCongas bassTone shake crush sizzle zing").split()
OPENNESS = ["closed", "halfOpen", "open2", "pedal"]
CODES = [(36, "pt.natural")]
CODES += [(37 + i, "pt." + pt) for i, pt in enumerate(STROKES)]      # 37..68
CODES += [(72 + i, pt) for i, pt in enumerate(ROLL_FAMILY)]           # 72..81
CODES += [(84 + i, "pt." + pt) for i, pt in enumerate(OPENNESS)]      # 84..87

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
]
pitched_dims = {d["id"]: d for d in engine.dimensions(pitched.PITCHED_PERCUSSION_R2)}
by_id = {d[0]: d for d in pitched.DIMENSIONS}
DIMENSIONS = [by_id[key] for key in COMMON_IDS + PITCHED_IDS] + MECHANISMS
# Shared dimensions keep the pitched conduits and values; new ones take
# conduits no pitched dimension uses, so one CC means one thing in both maps.
wire = {key: (pitched_dims[key]["kind"], pitched_dims[key]["number"]) for key in COMMON_IDS + PITCHED_IDS}
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
         "add codes when a score test needs them.",
    "R": "Specialized gesture not in the r1 code table; add a code when a score test needs it.",
    "K": "Keyboard pedal PT; pedal controllers pass through as MIDI.",
    "E": "Electronic/FX control outside this acoustic section profile.",
}

UNPITCHED_R1 = SimpleNamespace(
    REVISION=1, WIRE_SCHEMA=2, WRITTEN_DURATIONS=True, SECTION="UnpitchedPercussion",
    LABEL="Orchestral unpitched percussion", TITLE="Orchestral unpitched percussion",
    NAME="Fiddle Bridge - Orchestral Unpitched Percussion r1 (host test)",
    ENTITY_ID="xmap.user.Fiddle.OrchestralUnpitchedPercussion.r1",
    BASES=BASES, BASE_IDS=set(strings.BASE_IDS) | set(ROLL_FAMILY), ALIASES={},
    DIMENSIONS=DIMENSIONS, CONDUITS=wire, VALUES=values,
    INSTRUMENTS=INSTRUMENTS, EXTRA_MUTEX_GROUPS=[], EXCLUSIONS={}, FAMILY_EXCLUSIONS=FAMILY_EXCLUSIONS,
    PERCUSSION=SimpleNamespace(
        ENTITY_ID="drumkitmap.user.Fiddle.OrchestralUnpitchedPercussion.r1",
        NAME="Fiddle Bridge - Orchestral Unpitched Percussion r1 (host test)",
        INSTRUMENTS=INSTRUMENTS, CODES=CODES, FOR_SINGLE_INSTRUMENT=False),
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

REVISIONS = [UNPITCHED_R1]
