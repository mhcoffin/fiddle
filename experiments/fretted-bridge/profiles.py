"""Fretted instruments r1: exact built-in Dorico IDs, wire schema 2.

Guitars, basses, lutes and Dorico's other fretted instruments (every
instrument.fretted.* entity, plus the guitarrón, which Dorico files under
strings). Techniques shared with strings keep the strings r3-r5 conduit,
reset and On values; the excitation and harmonic dimensions keep every
strings value and append the guitar ones (Guitar pizzicato; pinch and harp
harmonics). Fretted-only dimensions take the conduits the bowing dimensions
leave free. Note durations are pinned to 100% from the start, as in
strings r5 and pitched percussion r2.
"""
from pathlib import Path
from types import SimpleNamespace
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "orchestral-strings-bridge"))
import generate as engine  # noqa: E402
import profile_r3 as strings  # noqa: E402

# Strings dimensions a fretted player shares, unchanged.
COMMON_IDS = (
    "connection", "accent", "vibrato", "mute", "tremolo", "trill", "arpeggio",
    "dynamic_level", "dynamic_attack", "dynamic_shape", "fade_in", "fade_out",
    "soft", "ghost_note", "tremblement", "muffled_strings", "glissando",
    "portamento", "strum", "damp", "laissez_vibrer", "simile",
)
# Strings dimensions kept with every strings value and guitar values appended.
EXTENDED = {
    "excitation": ("guitarPizzicato",
                   "Plucking/pizzicato method. The strings values keep their numbers (Plucked, Pizzicato, "
                   "Bartók/snap, buzz, nail, alla chitarra, secco); Guitar pizzicato is appended. "
                   "Explicit Natural returns to the normal stroke."),
    "harmonic": ("pinchHarmonic harpHarmonic",
                 "Harmonic production. The strings values keep their numbers (artificial, natural 1/2, "
                 "half); pinch and harp harmonics are appended."),
}
STRING_DIMENSIONS = {d[0]: d for d in strings.DIMENSIONS}

FRETTED_ONLY = [
    ("bend", "bend slightBend dipbend", False,
     "Bend shape of one note: full bend, slight (blues) bend, or dip. Dorico may also play a "
     "notated bend as pitch bend; Fiddle must not bend twice."),
    ("pre_bend", "preBend", False,
     "Pre-bent attack, independent of the bend that may release it."),
    ("vibrato_bar", "vibratoBarDip vibratoBarScoop", False,
     "Vibrato (whammy) bar gesture on one note: dip or scoop."),
    ("vibrato_bar_line", "vibratoBarLine", False,
     "Vibrato bar line: a sustained whammy-bar passage, independent of single dips and scoops."),
    ("palm_muting", "palmMuting", False,
     "Palm muting (P.M. line). Independent of the String mute dimension, strumming and harmonics; "
     "ended by its own line, not by Natural."),
    ("tapping", "leftHandTap rightHandTap rightHandTapTapping", False,
     "Tapped note: left-hand tap or one of the two right-hand tap IDs (kept distinct, not aliased)."),
    ("slide", "slide", False,
     "Slide into or between notes, distinct from the explicit Glissando and Portamento IDs."),
    ("pick_scrape", "pickScrape", False, "Pick scrape along the wound strings."),
    ("skank", "skank", False, "Skank: the clipped off-beat rhythm-guitar stroke."),
    ("drive", "overdrive distortion", False,
     "Amp/pedal gain: overdrive or distortion. Alternatives; independent of modulation and delay."),
    ("modulation", "chorus flange phaser", False,
     "Modulation effect: chorus, flanger or phaser, one at a time."),
    ("delay", "delay", False, "Delay/echo effect, independent of drive and modulation."),
    ("wah_pedal", "wahWahGuitar", False,
     "Wah-wah pedal engaged (direction), independent of the per-note open/stopped marks."),
    ("wah_position", "wahWahOpen wahWahStopped", False,
     "Wah-wah open (o) or stopped (+) on one note."),
]

EXCLUSIONS = {
    "pt.lhPizzicato": "Bowed-string left-hand pizzicato; a fretted left-hand note is a pull-off or tap.",
    "pt.sonsXylophoniques": "Harp technique; not a fretted-instrument technique.",
    "pt.pince": "Harp technique; not a fretted-instrument technique.",
    "pt.smear": "Jazz wind pitch effect; fretted slides use Slide, Glissando or Portamento.",
    "pt.scoop": "Jazz wind entry gesture; fretted entries use Slide, Bend or Vibrato bar scoop.",
    "pt.plop": "Jazz wind entry gesture outside this profile.",
    "pt.fallDrop": "Jazz wind exit gesture; fretted fall-offs use Slide or Glissando down.",
    "pt.doit": "Jazz wind exit gesture outside this profile.",
    "pt.flip": "Jazz wind ornament outside this profile.",
    "pt.jazzTurn": "Jazz wind ornament outside this profile.",
    "pt.rip": "Jazz wind gesture outside this profile.",
    "pt.squeeze": "Jazz wind gesture outside this profile.",
    "pt.shake": "Jazz wind/brass shake; fretted players use Trill or Wide vibrato.",
    "pt.bowed": "Bowed aliases pt.natural in the built-in catalog; fretted instruments are not bowed.",
}
FAMILY_EXCLUSIONS = {
    "A06": "Percussion roll variant; tremolo picking uses pt.tremolo.",
    "A": "Articulation or ornament without a distinct fretted realization in this profile.",
    "S": "Bowed-string technique outside fretted instruments.",
    "W": "Wind/brass/vocal technique outside fretted instruments.",
    "P": "Percussion mechanism or gesture; Damp and l.v. are included explicitly.",
    "R": "Unpitched-percussion event outside fretted instruments.",
    "K": "Keyboard pedal PT; pedal controllers are passed through as MIDI rather than encoded here.",
}

DIMENSIONS = [STRING_DIMENSIONS[key] for key in COMMON_IDS]
for key, (extra, rationale) in EXTENDED.items():
    _, members, natural, _ = STRING_DIMENSIONS[key]
    DIMENSIONS.append((key, members + " " + extra, natural, rationale))
DIMENSIONS += FRETTED_ONLY

reference = {d["id"]: d for d in engine.dimensions(strings)}
SHARED_IDS = COMMON_IDS + tuple(EXTENDED)
POOL = engine.conduit_pool(SimpleNamespace(WIRE_SCHEMA=2))
wire = {key: (reference[key]["kind"], reference[key]["number"]) for key in SHARED_IDS}
free = iter(pair for pair in POOL if pair not in wire.values())
for key, *_ in FRETTED_ONLY:
    wire[key] = next(free)
values = {key: reference[key]["values"] for key in COMMON_IDS}
for key, (extra, _) in EXTENDED.items():
    shared = dict(reference[key]["values"])
    top = max(shared.values())
    shared.update({"pt." + pt: top + 1 + i for i, pt in enumerate(extra.split())})
    values[key] = shared

FRETTED = SimpleNamespace(
    REVISION=1, WIRE_SCHEMA=2, WRITTEN_DURATIONS=True, PREFIX="", SECTION="Fretted",
    LABEL="Fretted instruments", TITLE="Fretted instruments",
    NAME="Fiddle Bridge - Fretted r1 (host test)",
    ENTITY_ID="xmap.user.Fiddle.Fretted.r1",
    BASE_IDS=strings.BASE_IDS, ALIASES={}, DIMENSIONS=DIMENSIONS,
    CONDUITS=wire, VALUES=values,
    INSTRUMENTS=["Classical, steel-string, 12-string, jazz, semi-acoustic, electric and resonator guitars",
                 "Electric, fretless, acoustic and semi-acoustic bass guitars; guitarrón",
                 "Renaissance and Baroque lutes; theorbo",
                 "Banjo; tenor banjo; mandolin; ukulele; dulcimer",
                 "Hawaiian and pedal steel guitars",
                 "Balalaikas; domras; bandurria; bandola; bordonúa; cavaquinho; charango; cuatros; "
                 "requinto; tiple; tres; vihuelas; Portuguese guitar"],
    EXTRA_MUTEX_GROUPS=[], EXCLUSIONS=EXCLUSIONS, FAMILY_EXCLUSIONS=FAMILY_EXCLUSIONS,
)
REVISIONS = [FRETTED]

# The Dorico instruments the section covers, as BridgeSections.h stems: every
# instrument.fretted.* entity (keyed variants and aliases match the stem) and
# the guitarrón. A test keeps BridgeSections.h in step.
STEMS = ["instrument.fretted", "instrument.strings.guitarrón"]
