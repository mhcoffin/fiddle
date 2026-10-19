"""Revision 2 review vocabulary, NOT a Fiddle runtime profile.

Tuple fields: stable dimension ID, exact built-in ID suffixes, explicit nat.
cancellation, musical rationale. Order fixes wire allocation; do not reorder
or extend a published revision in place.
"""

REVISION = 2
NAME = f"Fiddle Bridge - Orchestral Strings r{REVISION} - INSPECTION ONLY"
ENTITY_ID = f"xmap.user.Fiddle.OrchestralStrings.Inspection.r{REVISION}"

# Genuine alternatives share an enum. Singletons remain independent flags.
# Natural cancellation is a proposal for explicit notation, not a base reset.
DIMENSIONS = [
    ("connection", "legato nonLegato", False, "Connected versus detached phrasing; independent of stroke and articulation."),
    ("accent", "accent marcato", False, "Accent strengths; keep separate from staccato and tenuto bases."),
    ("vibrato", "nonVibrato conVibrato vibrato wideVibrato", False, "Four distinct canonical vibrato choices; unspecified is not vibrato."),
    ("excitation", "plucked pizzicato bartoksnapPizzicato buzzPizzicato guitarPizzicato nailPizzicato pizzicatoAllaChitarra pizzicatoSecco", True, "Alternative plucking/pizzicato methods; bowed aliases Natural. Guitar-named pizzicato retained as a strings colour."),
    ("left_hand_pizzicato", "lhPizzicato", False, "May coexist with a bowed right hand; not folded into excitation."),
    ("bow_material", "colLegnoBattuto colLegnoTratto", True, "Struck versus drawn wood; separate from bow contact and stroke."),
    ("bow_stroke", "colle detache detacheLance jete martellato martele portatoloure ricochet saltando sautille spiccato fouette", True, "Alternative strokes, including Fouetté (user-reviewed in r2). Exact IDs retained even for near-synonyms; review host cancellation in Dorico."),
    ("bow_contact", "sulPonticello sulTasto pdlt bowOnBridge bowOnTailpiece", True, "Proposed alternative contact regions; not exclusion with flautando or pressure."),
    ("bow_direction", "upbow downbow", False, "Direction of this stroke."),
    ("bow_pressure", "flautando overpressure", True, "User-reviewed r2 alternatives on one pressure dimension; independent of contact point and stroke, so flautando plus sul tasto remains possible."),
    ("harmonic", "artificialHarmonic naturalHarmonic1 naturalHarmonic2 halfHarmonic pinchHarmonic", False, "Proposed alternative harmonic production; includes pinch as an extended string technique, excludes harp-specific harmonic."),
    ("mute", "muted open1", False, "Con/senza sordino intent; explicit Open is not unspecified. Nat. must not remove a mute."),
    ("tremolo", "tremolo", False, "Independent repetition, including tremolo plus sul ponticello."),
    ("trill", "trill trill.half trill.whole", False, "Generic or specified interval; independent of vibrato and tremolo."),
    ("arpeggio", "arpeggioUp arpeggioDown", False, "Direction of chord spreading."),
    ("dynamic_level", "ppp pp p mp mf f ff fff", False, "Steady named levels; not a replacement for continuous CC1/7/11."),
    ("dynamic_attack", "ffp fp pf sf sfp sfz", False, "Alternative named compound/attack dynamics, separate from steady level."),
    ("dynamic_shape", "crescendo diminuendo crescendoDiminuendo diminuendoCrescendo volumeSwell", False, "Alternative complete envelopes; not a quantized loudness curve."),
    ("fade_in", "fadeIn", False, "Independent onset fade; may coexist with fade out."),
    ("fade_out", "fadeOut", False, "Independent release fade."),
    ("soft", "soft", False, "Independent soft colour; not equated with a written dynamic level."),
    ("ghost_note", "ghostNote", False, "Independent ghosting."),
    ("shake", "shake", False, "Preserved independently; not presumed identical to trill."),
    ("tremblement", "tremblement", False, "Preserved independently; not presumed identical to tremolo."),
    ("muffled_strings", "muffledStrings", False, "String-specific muffling; not collapsed with generic damping."),
    ("sons_xylophoniques", "sonsXylophoniques", True, "Independent special string colour pending combination review."),
    ("pince", "pince", True, "Preserve this less-common exact string PT independently."),
    ("glissando", "glissandoUp glissandoDown", False, "Direction of a glissando; retained independently of portamento."),
    ("portamento", "portamentoUp portamentoDown", False, "Direction of portamento."),
    ("slide", "slide", False, "Independent generic sliding gesture; no invented alias to portamento."),
    ("smear", "smear", False, "Independent pitch gesture."),
    ("scoop", "scoop", False, "Independent onset gesture; can coexist with an ending gesture."),
    ("plop", "plop", False, "Independent onset gesture."),
    ("fall", "fallDrop", False, "Independent ending gesture."),
    ("doit", "doit", False, "Independent ending gesture."),
    ("bend", "bend", False, "Independent general bend; no assumed exclusion with other gestures."),
    ("dip_bend", "dipbend", False, "Independent compound gesture."),
    ("slight_bend", "slightBend", False, "Preserved independently until gesture combinations are reviewed."),
    ("pre_bend", "preBend", False, "Independent pre-bend; may precede another pitch gesture."),
    ("flip", "flip", False, "Independent ornament."),
    ("jazz_turn", "jazzTurn", False, "Independent ornament; included for orchestral strings playing jazz idioms."),
    ("rip", "rip", False, "Independent ornament."),
    ("squeeze", "squeeze", False, "Independent ornament."),
    ("strum", "strumUp strumDown", False, "Directional strummed pizzicato; distinct from arpeggio."),
    ("damp", "damp", False, "Independent damping request for ringing strings."),
    ("muffled_damped", "muffledDamped", False, "Preserve generic damping separately from string-specific muffling."),
    ("laissez_vibrer", "laissezVibrer", False, "Independent ringing request; no guessed relationship to note length."),
    ("release", "release", False, "Independent release intent; no invented interpretation."),
    ("simile", "simile", False, "Transmit the explicit PT if emitted; does not itself encode previous state or restore a stack."),
]

BASE_IDS = {"pt.natural", "pt.staccatissimo", "pt.staccato", "pt.tenuto"}
ALIASES = {"pt.bowed": "pt.natural"}

# Every omitted canonical ID gets a visible scope disposition in inventory.md.
# These are scope choices, never omissions made to meet a wire budget.
EXCLUSIONS = {
    "pt.harpHarmonic": "Harp-specific; harp is outside the bowed-orchestral-strings profile.",
    "pt.vibratoBarDip": "Guitar vibrato-bar mechanism, not bowed orchestral strings.",
    "pt.vibratoBarLine": "Guitar vibrato-bar mechanism, not bowed orchestral strings.",
    "pt.vibratoBarScoop": "Guitar vibrato-bar mechanism, not bowed orchestral strings.",
}
FAMILY_EXCLUSIONS = {
    "A06": "Percussion roll variant; string repetition is represented by pt.tremolo.",
    "G06": "Guitar tapping vocabulary; dedicated finger/body-tapping extension is outside this first profile.",
    "G07": "Guitar palm-muting vocabulary; bowed-string damping has explicit separate entries.",
    "G08": "Guitar pick/scrape/skank vocabulary outside this profile.",
    "W": "Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included).",
    "P": "Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included).",
    "R": "Percussion-specific playing gestures outside this profile.",
    "K": "Keyboard/pedal mechanism outside this profile.",
    "E": "Electronic/FX control outside this acoustic technique profile.",
}
