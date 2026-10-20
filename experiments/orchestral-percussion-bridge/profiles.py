"""Orchestral pitched percussion r1: exact built-in Dorico IDs, wire schema 2.

Timpani and the orchestral mallet instruments. Shared PTs keep the strings
r3/r4 conduit, reset and On values; percussion-only dimensions take free pool
entries. Unpitched percussion and keyboards stay outside this profile.
"""
from pathlib import Path
from types import SimpleNamespace
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "orchestral-strings-bridge"))
import generate as engine  # noqa: E402
import profile_r3 as strings  # noqa: E402

COMMON_IDS = (
    "connection", "accent", "tremolo", "trill", "arpeggio",
    "dynamic_level", "dynamic_attack", "dynamic_shape", "fade_in", "fade_out",
    "soft", "ghost_note", "glissando", "damp", "laissez_vibrer", "simile",
)
STRING_DIMENSIONS = {d[0]: d for d in strings.DIMENSIONS}
COMMON = [STRING_DIMENSIONS[key] for key in COMMON_IDS]

PERCUSSION_ONLY = [
    ("beater", "beaterFelt beaterRods beaterPuffy beaterDreads sticks", False,
     "Beater/mallet choice for one stroke. Built-in beater alternatives plus generic Sticks; "
     "ord. does not change mallets, so Natural does not cancel this group."),
    ("strike_location", "centre edgeDrums rim", True,
     "Playing spot, chiefly timpani (centre, edge, rim). Proposed: explicit nat./ord. returns to the normal spot."),
    ("roll", "roll", False,
     "Explicit percussion roll, independent of the bowed/measured pt.tremolo dimension. Snare roll variants are out of scope."),
    ("dead_stroke", "deadStroke", False,
     "Stroke that is stopped immediately, independent of later damping or l.v."),
    ("muffled", "muffledDamped", False,
     "Muffled/damped resonance (e.g. covered timpani), kept separate from the Damp action."),
    ("motor", "motorOn motorOff", False,
     "Vibraphone motor state. Explicit Off is not the unspecified state."),
    ("pedal", "pedal", False,
     "Percussion pedal PT (vibraphone damper indication). Dorico pedal lines may send CC64 instead; that controller is passed through separately."),
]

FAMILY_EXCLUSIONS = {
    "A06": "Unpitched-drum roll variant; mallet and timpani rolls use pt.roll and pt.tremolo.",
    "A": "Articulation or ornament without a distinct struck-percussion realization in this profile.",
    "S": "Bowed/plucked-string technique outside pitched percussion.",
    "W": "Wind/brass/vocal technique outside pitched percussion.",
    "G": "Guitar or specialized pitch gesture outside this profile; explicit Glissando is retained.",
    "P": "Unpitched-percussion mechanism or gesture; beaters, playing spot, motor, pedal and damping are included explicitly.",
    "R": "Unpitched-percussion event outside this profile.",
    "K": "Keyboard pedal PT; pedal controllers are passed through as MIDI rather than encoded here.",
    "E": "Electronic/FX control outside this acoustic section profile.",
}

DIMENSIONS = COMMON + PERCUSSION_ONLY
reference = {d["id"]: d for d in engine.dimensions(strings)}
SCHEMA_TWO_POOL = [("CC", n) for n in range(103, 120)]
SCHEMA_TWO_POOL += [("KS", n) for n in list(range(21)) + list(range(115, 126))]
wire = {key: (reference[key]["kind"], reference[key]["number"]) for key in COMMON_IDS}
free = iter(pair for pair in SCHEMA_TWO_POOL if pair not in wire.values())
for key, *_ in PERCUSSION_ONLY:
    wire[key] = next(free)
values = {key: reference[key]["values"] for key in COMMON_IDS}

PITCHED_PERCUSSION = SimpleNamespace(
    REVISION=1, WIRE_SCHEMA=2, SECTION="PitchedPercussion",
    LABEL="Orchestral pitched percussion", TITLE="Orchestral pitched percussion",
    NAME="Fiddle Bridge - Orchestral Pitched Percussion r1 (host test)",
    ENTITY_ID="xmap.user.Fiddle.OrchestralPitchedPercussion.r1",
    BASE_IDS=strings.BASE_IDS, ALIASES={}, DIMENSIONS=DIMENSIONS,
    CONDUITS=wire, VALUES=values,
    INSTRUMENTS=["Timpani", "Glockenspiel; crotales", "Xylophone; xylorimba", "Marimba",
                 "Vibraphone", "Tubular bells"],
    EXTRA_MUTEX_GROUPS=[], EXCLUSIONS={}, FAMILY_EXCLUSIONS=FAMILY_EXCLUSIONS,
)

# r1 is frozen; r2 pins Dorico's note-duration options to 100% (written lengths).
PITCHED_PERCUSSION_R2 = SimpleNamespace(**{
    **vars(PITCHED_PERCUSSION), "REVISION": 2, "WRITTEN_DURATIONS": True,
    "NAME": "Fiddle Bridge - Orchestral Pitched Percussion r2 (host test)",
    "ENTITY_ID": "xmap.user.Fiddle.OrchestralPitchedPercussion.r2"})
REVISIONS = [PITCHED_PERCUSSION, PITCHED_PERCUSSION_R2]
