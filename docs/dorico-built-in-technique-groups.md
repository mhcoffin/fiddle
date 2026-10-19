# Built-in Dorico playback techniques: proposed musical dimensions

> **Retained inventory, not the active generation plan.** As of 2026-09-24,
> implement the [four orchestral section profiles](orchestral-bridge-maps.md).
> “Strings” means bowed orchestral strings. Other instrument families,
> unpitched percussion, and custom PTs are deferred. This all-instrument table
> remains reference material; neither every family nor every provisional mutex
> grouping below is approved for the current phase.

Status: **review draft — no protocol, map, decoder, or playback changes**  
Date: 2026-09-21  
Scope: built-in Dorico PTs first; user-defined/vendor PTs are a separate later project.

## Recommendation

Keep the vector-of-small-integers model. Start with musical properties that can
vary independently, and make values within a property mutually exclusive only
where they genuinely represent alternatives. Do not start by assigning one
controller per missing technique, or by squeezing all built-ins into the current
controller layout.

For example, a note can request **legato + tremolo + sul ponticello + muted +
non-vibrato + crescendo**. These belong to different dimensions. Similarly,
**staccato + tenuto + accent** must not be collapsed into a single choice.
Dorico explicitly supports technique combinations and recommends mutex groups
for techniques that cannot coexist. This proposal follows that principle, not
the limitations of any particular sample library.
[Source: Dorico playback techniques](https://www.steinberg.help/r/dorico-pro/6.2/en/dorico/topics/library/library_playback_techniques_c.html).

**This is a proposed semantic partition, not a claim that every row is already a
safe Dorico mutex group.** Firm alternatives are identified below. Ambiguous
families have explicit review notes; until resolved, their members must not
cancel one another. Independent members are separate two-state dimensions, not
a hidden catch-all mutex group.

## 1. Inventory and evidence

The installed **Dorico 6.2.40.6278** built-in definition file contains:

- **298 distinct PT IDs:** 205 Attributes and 93 Directions.
- **52 PTs with declared mutex membership; 246 without it.**
- **12 distinct declared mutex group IDs**, including two containing only Natural.
- **9 explicit aliases** and **27 explicit fallbacks**.

Source: `/Applications/Dorico 6.app/Contents/Resources/playingTechniqueDefinitions.xml`  
SHA-256: `8cf3fbaa7830dafe9447225dddc7b66eb4f13f028f0bd7c90f0010f683394e93`

A scan of bundled resource XML for `PlayingTechniqueDefinition` elements found
these definitions in that file only. This inventory is scoped to those bundled
definitions, not all possible implicit playback behavior, project-local
overrides, user additions, or vendor distributions. No Art Conductor or VSL
custom definitions were used to decide the groups.

The XML's `groupType` (Dynamics, Lengths, Ornaments, Techniques) is a UI/category
classification, **not** mutual exclusion. Missing mutex membership is likewise
not evidence that any two techniques can or cannot coexist.

Dorico distinguishes note-local **Attributes** from persistent **Directions**.
Their stored types are retained in the inventory, including surprising entries;
this proposal does not silently change them.
[Source: Edit Playback Techniques](https://www.steinberg.help/r/dorico-pro/6.2/en/dorico/topics/library/library_playback_techniques_edit_playback_techniques_dialog_r.html).

### Existing built-in mutex memberships

These are source evidence, not the proposed design. They are not a partition:
Natural is present in five groups.

| Source group (`ptmg.` prefix omitted) | Built-in members |
| --- | --- |
| beaters | Dreads (beater); Felt (beater); Puffy (beater); Rods (beater) |
| dynamicLevel | f; ff; fff; ffp; fp; mf; mp; p; pf; pp; ppp; sf; sfp; sfz |
| legato | Legato; Non legato |
| motor | Motor off; Motor on |
| primaryBrass | Bell mute; Bucket mute; Cup mute; Half-muted; Harmon mute, stem extended; Harmon mute, stem in; Harmon mute, stem out; Muted; Natural; Open 1; Plunger mute; Straight mute |
| primaryKeyboard | Natural |
| primaryPerc | Closed; Half-open; Natural; Open 2; Pedal |
| primaryString | Bowed; Col legno battuto; Col legno tratto; Natural; Pizzicato; Spiccato |
| primaryWind | Natural |
| snare | Snares off; Snares on |
| vibrato | Con vibrato; Non vibrato; Vibrato; Wide vibrato |
| wah | Wah-wah; Wah-wah open; Wah-wah stopped |

Consequential departures proposed here:

- Separate Spiccato/bow stroke from primary string excitation. Separate bow
  position and bow direction from both.
- Keep Tenuto independent of Staccato/Staccatissimo. A Steinberg developer has
  specifically described Dorico producing both PT IDs for staccato-tenuto;
  verify this in the acceptance score on the installed version.
  [Source: Staccato/Tenuto Technique](https://forums.steinberg.net/t/staccato-tenuto-technique/155231/2).
- Separate steady dynamics, force/compound dynamics, and dynamic shapes.
- Separate percussion pedal action from openness.
- Do not put sustain, sostenuto, and una corda in one mutually exclusive group.
- Do not combine electronic effects into a single enum: chorus and delay, for
  example, need not exclude one another.
- Do not treat Natural as an ordinary shared value that automatically erases
  all these independent dimensions.

## 2. Proposed grouping table

### Reading the table

| Status | Meaning |
| --- | --- |
| Mutex | Recommended alternatives for one property, at the same instant on one note/voice. Still needs Dorico transition testing. |
| Provisional | Candidate enum, but the row's identified scope, compounds, or generic/subtype issues must be resolved before enforcing exclusion. |
| Independent | **Each listed PT is its own independent dimension** with unspecified/inactive and present states; entries in the row do not exclude one another. |
| Review | Conservative holding classification: preserve each PT separately for now. This row is **not** a proposed common mutex group. |
| Control | Default/continuation semantics, not an ordinary musical-property enum. |

Every PT is assigned exactly once in the full inventory below. The 72 rows are
review families, **not 72 controllers**. They include multi-member Independent
and Review rows, which intentionally do not assert mutual exclusion.

Every musical dimension needs an **unspecified** state distinct from explicit
negative/default-looking PTs such as Non vibrato, Non legato, Motor off, and
Open 1. No synthetic PT such as "poco vibrato", "slurred", or "espressivo" has
been added to this built-in inventory.

The following table is a design proposal based on the built-in vocabulary and
musical combination reasoning. Except for the source memberships above, its
groupings are **not claimed to be Steinberg's definitions**.

### Common musical dimensions

| ID / proposed dimension | Status | Built-in PT values | Combination rationale / outstanding decision |
| --- | --- | --- | --- |
| A01 — Connection | Mutex | Legato; Non legato | Keep independent of shortening, tenuto, repetition, and bow stroke. |
| A02 — Shortening | Mutex | Staccato; Staccatissimo | Do not put Tenuto here: staccato + tenuto must survive. |
| A03 — Tenuto | Independent | Tenuto | Independent of shortening and accent; do not substitute Portato/Louré. |
| A04 — Accent class | Mutex | Accent; Marcato | Alternatives for attack emphasis; may combine with shortness, tenuto, and dynamics. |
| A05 — Vibrato | Mutex | Non vibrato; Con vibrato; Vibrato; Wide vibrato | Retain Con vibrato and Vibrato as distinct source IDs; unspecified is not explicit vibrato. |
| A06 — Repetition / roll style | Provisional | Tremolo; Roll; Buzz roll; Circular roll; Flat roll; Press roll; Swoosh roll | Proposed alternatives for one repetition action; keep independent of legato, pitch ornaments, mute, and contact. Verify generic + specific roll requests and percussion meanings. |
| A07 — Trill interval | Mutex | Trill; Trill (half-step); Trill (whole step) | Generic, half-step, and whole-step alternatives; do not infer an interval for generic Trill. |
| A08 — Other oscillating ornaments | Review | Shake; Tremblement | Keep separate pending verification of relation to trill/tremolo, rather than declaring either a synonym. |
| A09 — Arpeggio direction | Mutex | Arpeggio up; Arpeggio down | Direction of one arpeggio; independent of dynamics and playing method. |
| A10 — Steady dynamic designation | Mutex | ppp; pp; p; mp; mf; f; ff; fff | Metadata, not a second gain curve. Keep separate from force/compound dynamics and dynamic shape. |
| A11 — Force / compound dynamic | Provisional | ffp; fp; pf; sf; sfp; sfz | Alternatives for one dynamic gesture; ffp is a Direction in the source, unlike most of this row. Do not replace the underlying steady dynamic. |
| A12 — Dynamic shape | Provisional | Crescendo; Diminuendo; Crescendo-diminuendo; Diminuendo-crescendo; Volume swell | Crescendo and diminuendo are opposing shapes, not unrelated on/off flags. Composite shapes retain their own IDs; verify Volume swell overlap and Dorico activation. |
| A13 — Fade envelope | Provisional | Fade in; Fade out | Onset and release fades can coexist over one sustained note: a combined value or two axes may be needed. NOT a finalized mutex group. |
| A14 — Quiet / ghost modifiers | Independent | Soft; Ghost note | Two separate flags, not alternatives and not replacements for velocity/dynamic level. |

### Strings and harmonics

| ID / proposed dimension | Status | Built-in PT values | Combination rationale / outstanding decision |
| --- | --- | --- | --- |
| S01 — Primary string excitation | Provisional | Bowed; Plucked; Pizzicato; Bartok/snap pizzicato; Buzz pizzicato; Guitar pizzicato; Nail pizzicato; Pizzicato alla chitarra; Pizzicato secco | Proposed alternatives for the principal excitation. Review generic Plucked/Pizzicato plus subtype requests; Bowed aliases Natural in Dorico. LH pizzicato is deliberately separate. |
| S02 — Left-hand pizzicato | Independent | LH pizzicato | Can describe an additional action alongside bowing; do not let it automatically cancel Arco. |
| S03 — Col legno action | Provisional | Col legno battuto; Col legno tratto | Struck versus drawn action. Keep separate from main excitation/stroke for review; mixed wood-and-hair use must not be ruled out merely by a generic Bowed label. |
| S04 — Bow stroke | Provisional | Collé; Détaché; Détaché lancé; Jeté; Martellato; Martelé; Portato/Louré; Ricochet; Saltando; Sautillé; Spiccato | Candidate stroke alternatives. Jeté/Ricochet and Martellato/Martelé remain distinct; mixed or generic + specific requests need review. Independent of bow direction, position, vibrato, and accent. |
| S05 — Bow contact / sounding segment | Provisional | Sul ponticello; Sul tasto; P.d.l.t.; Bow on bridge; Bow on tailpiece | Candidate positions, not intensity levels. Confirm P.d.l.t. scope before equating it with any other label; retain all IDs. |
| S06 — Bow direction | Mutex | Upbow; Downbow | Independent of bow stroke and string excitation. |
| S07 — String color / attack modifiers | Independent | Flautando; Overpressure; Fouetté | Separate flags; not a single color enum. Verify coexistence with specific strokes instead of globally excluding them. |
| S08 — Harmonic production | Provisional | Artificial harmonic; Natural harmonic 1; Natural harmonic 2; Half-harmonic; Harp harmonic; Pinch harmonic | Alternatives for one harmonic production mode; names 1/2 do not establish interval semantics. Harmonics must combine with pizzicato, bow position, vibrato, and repetition. |
| S09 — Special string damping / attacks | Review | Muffled strings; Sons xylophoniques; Pincé | Preserve separately until meanings and overlap with palm muting, damping, and pizzicato are confirmed. |

### Winds, brass, and voice

| ID / proposed dimension | Status | Built-in PT values | Combination rationale / outstanding decision |
| --- | --- | --- | --- |
| W01 — Mute device / state | Provisional | Muted; Open 1; Half-muted; Bell mute; Bucket mute; Cup mute; Harmon mute, stem extended; Harmon mute, stem in; Harmon mute, stem out; Plunger mute; Straight mute | Extends Dorico primaryBrass without Natural. Open 1 and Muted are instrument-contextual; coexistence of plunger with another mute needs a compound state or separate axis, not silent exclusion. |
| W02 — Stopped tone | Independent | Stopped | Separate from mute device until instrument-context semantics are validated. |
| W03 — Tonguing pattern | Provisional | Double-tongue; Triple-tongue; Flutter-tongue | Candidate alternatives for the repeating tongue action; independent of vibrato, mute, and dynamic shape. |
| W04 — Wind special attacks | Review | Slap tongue; Key click; Key click with pitch; Mouthpiece pop; Tongue stop; Jet whistle | Key click with/without pitch are likely alternatives; grouping the whole row is NOT approved. Separate events/modifiers until action and sustain combinations are tested. |
| W05 — Independent tone modifiers | Independent | Cuivré; Growl; Multiphonic; Alternative fingering | Separate dimensions. In particular, a bright/brassy tone must not automatically cancel a mute or growl. |
| W06 — Mouth opening | Mutex | Mouth closed; Mouth slightly open; Mouth open; Mouth wide open | Extent of opening; separate from pursing and vocal production. |
| W07 — Mouth / vocal color | Independent | Mouth pursed; Nasal voice | Separate modifiers; pursing does not specify a unique degree of mouth opening. |
| W08 — Vocal production | Provisional | Spoken; Whisper; Whistle | Candidate alternatives for primary vocal sound; mixed phonation would require a compound value. Normal singing remains unspecified, not an invented PT. |
| W09 — Special air / vocal events | Review | Whistle tone; Dut (vocal); Tongue click; Finger click; Tongue and finger click | Whistle tone is not automatically Whistle. Keep clicks independent of sustained vocal/wind tone; explicit combined click ID must be preserved. |

### Pitch gestures and guitar

| ID / proposed dimension | Status | Built-in PT values | Combination rationale / outstanding decision |
| --- | --- | --- | --- |
| G01 — Pitch travel | Provisional | Glissando up; Glissando down; Portamento up; Portamento down; Slide; Smear | Candidate alternatives for a single travel gesture. Distinguish style from direction if concurrent source IDs require it; retain independent onset/release ornaments. |
| G02 — Entry ornament | Provisional | Scoop; Plop | Candidate alternative note entries; may combine with a release fall/doit. |
| G03 — Exit ornament | Provisional | Fall (drop); Doit | Candidate alternative note endings; never mutex with the entry ornament. |
| G04 — Other pitch gestures | Review | Bend; Dip/bend; Slight bend; Pre-bend; Flip; Jazz turn; Rip; Squeeze; Vibrato bar dip; Vibrato bar scoop; Vibrato bar line | No blanket ornament mutex. Pre-bend plus subsequent bend/release, or a bar indication plus a specific gesture, can need compound representations. Determine onset/sustain/release placement before grouping. |
| G05 — Strum direction | Mutex | Strum up; Strum down | Separate from arpeggio and playing method until notation behavior establishes whether both labels occur. |
| G06 — Tapping hand / variant | Provisional | Left-hand tap; Right hand tap; Right-hand tap (tapping) | Alternatives for one tapping action. Two-hand passages may need separate note/voice events; do not infer an alias between the two RH IDs. |
| G07 — Palm muting | Independent | Palm muting | Independent of strum, harmonics, and pitch gestures. |
| G08 — Other plucked-string events | Review | Pick scrape; Skank | Keep exact IDs and independent storage pending scope review. |

### Percussion dimensions

| ID / proposed dimension | Status | Built-in PT values | Combination rationale / outstanding decision |
| --- | --- | --- | --- |
| P01 — Openness | Mutex | Closed; Half-open; Open 2 | Separate from beater, strike location, hand, and pedal action. Open 2 is not merged with Open 1. |
| P02 — End opening | Mutex | Closed end; Open end | Independent pair; not automatically the same physical control as Openness. |
| P03 — Hole opening | Mutex | Closed hole; Open hole | Independent pair; not automatically the same physical control as End opening. |
| P04 — Beater material / tool | Provisional | Dreads (beater); Felt (beater); Puffy (beater); Rods (beater); Sticks | Four declared beater alternatives plus proposed Sticks; verify generic Sticks compatibility before exclusion. |
| P05 — Stick contact part / variant | Provisional | Stick (butt); Stick (shaft); Stick (shaft 1); Stick (shaft 2); Stick (shaft 3); Stick (shaft 4); Stick (shaft 5); Stick (shaft open); Stick (shoulder); Stick (tip); Stick (tip 1); Stick (tip 2); Stick (tip 3); Stick (tip 4); Stick (tip 5) | Candidate alternatives for one contact; numbered variants stay opaque, not inferred intensity levels. Shaft open may combine two properties; do not erase either. |
| P06 — Strike location / surface | Provisional | Bell; Centre; Edge (cymbals); Edge (drums); Frame; Rim; Rim only; Shell; Skin; Bowl hit; Cage hit | Candidate locations for one strike. Rim only is more specific than Rim; percussion mapping can overload these names, so combinations need checking. |
| P07 — Hand | Mutex | Left (LH); Right (RH) | One hand label per note event; simultaneous LH/RH can require separate notes, not a last-message-wins channel state. |
| P08 — Motion direction | Mutex | Up; Down | Retain independently of Upbow/Downbow; no automatic equivalence. |
| P09 — Push / pull | Mutex | Push; Pull | Alternatives for a single motion; independent of hand. |
| P10 — Scrape direction | Provisional | Scrape; Scrape (left); Scrape (right) | Generic or directed scrape; retain direction independently of strike surface. Review generic + specific requests. |
| P11 — Scratch direction | Mutex | Scratch in; Scratch out | Alternatives for one scratch action. |
| P12 — Hit tension / variant | Provisional | Hit (loose); Hit (medium); Hit (tight) | Candidate alternatives; keep separate from the generic Loose PT until scope is known. |
| P13 — Tap location / variant | Provisional | Tap; Tap at edge; Tap halfway; Tap choke | Candidate complete tap gestures, including location/choke compounds. Must define relationship to generic Tap and the separate damping dimension before implementation. |
| P14 — Crush variant | Provisional | Crush; Crush (dry); Crush (fat) | Candidate alternatives; generic plus specific may need normalization without losing identity. |
| P15 — Zing variant | Provisional | Zing; Zing (fast); Zing (slow) | Candidate alternatives; do not infer numeric speeds from labels. |
| P16 — Friction slide variant | Provisional | Friction slide 1; Friction slide 2 | Opaque numbered alternatives; no inferred pitch or direction. |
| P17 — Snare mechanism | Mutex | Snares off; Snares on | An explicit Off is not the unspecified state. |
| P18 — Throwoff mechanism | Mutex | Throwoff off; Throwoff on | Do not merge with Snares on/off solely on physical association; verify the emitting notation first. |
| P19 — Motor | Mutex | Motor off; Motor on | Independent of note technique, pedals, and dynamic state. |
| P20 — Percussion pedal action | Independent | Pedal | Separate from openness: compare with Dorico primaryPerc, which groups them. Not the keyboard Sustain pedal ID. |
| P21 — Damping / ringing | Review | Damp; Dead stroke; Choke; Muffled/Damped; Laissez vibrer; Release | Do not assume all are simple alternatives. Damping after a ringing attack is a timed action, and generic + compound choke IDs need reconciliation. |
| P22 — Tabla syllable / stroke | Provisional | Ga (open); Ge (open); Gi (muted open); Ke (closed); Na (open); Ta (muted open); Te (closed); Ti (muted closed); Tu (open) | Alternatives for one named stroke; source aliases cross other dimensions. Preserve identities in the inventory, but test what Dorico actually emits before promising all can be distinguished. |

### Specialized percussion gestures requiring review

| ID / proposed dimension | Status | Built-in PT values | Combination rationale / outstanding decision |
| --- | --- | --- | --- |
| R01 — Stick / rim gestures | Review | Backstick; Cross stick; Edge rebound; Rim knock; Rim shot; Side stick; Stand click; Stick click; Stick shot; Stick snap; Sticks in; Over the head double shot; Over the head hit; Over the head shot; Dress harness hit | Do not put every gesture in one global mutex. Many encode contact, stroke, or position together; inspect built-in percussion maps and notation associations. |
| R02 — Cymbal / friction / suction gestures | Review | Crunch; Crunch choke; Flat crash; Port crash; Sizzle; Sizzle suck; Suck; Vacuum suck; Swish; Whale call | Preserve individually; compound names such as Crunch choke should eventually agree with the general choke representation without inventing aliases. |
| R03 — Other specialized actions / sounds | Review | Bass tone; Click; Ding; Foot; Hi-hat; Loose; Nail; Ping; Pop; Rake; Slap; Slap (congas); Snenor; Stir; Throw | Review sound, tool, and gesture roles before assigning exclusion. Unclear names (especially Snenor) remain opaque rather than guessed. |

### Keyboard and electronic effects

| ID / proposed dimension | Status | Built-in PT values | Combination rationale / outstanding decision |
| --- | --- | --- | --- |
| K01 — Sustain pedal | Independent | Sustain pedal | Independent pedal state; presence PT is not a complete encoding of pedal depth, retakes, or release timing. |
| K02 — Sostenuto pedal | Independent | Sostenuto pedal | Can coexist with sustain and una corda; independent state. |
| K03 — Una corda | Independent | Una corda | Independent of sustain/sostenuto, dynamics, and note articulation. |
| E01 — Independent electronic effects | Independent | Chorus; Delay; Distortion; Flange; Overdrive; Phaser | Each effect is its own modifier, NOT six mutually exclusive sounds. The bridge does not implement the audio effect itself. |
| E02 — Wah state / action | Provisional | Wah-wah; Wah-wah open; Wah-wah stopped | Dorico declares one wah group, but guitar effect enable and open/stopped positions may be distinct concepts. Review before treating all as exclusive. |

### Control semantics, not ordinary note dimensions

| ID / proposed dimension | Status | Built-in PT values | Combination rationale / outstanding decision |
| --- | --- | --- | --- |
| C01 — Natural / default | Control | Natural | A reset/default role spanning several source mutex groups. Not a value to copy into every independent dimension, and not an instruction to erase every modifier. |
| C02 — Simile | Control | Simile | Continuation/control semantics need a Dorico test. Do not transmit as a new timbre or pretend it is an all-axes reset. |

## Combination and exclusion checks for the eventual implementation

These are **acceptance requirements / design examples**, not playback tests
already performed. They describe one note/voice; polyphonic notes can need
different states at the same time.

| Case | Required outcome |
| --- | --- |
| Staccato + Tenuto + Accent | Retain all three IDs: A02 + A03 + A04. |
| Legato + Tremolo + Sul ponticello + Non vibrato + Muted + Crescendo | Retain all six dimensions: A01 + A06 + S05 + A05 + W01 + A12. |
| Pizzicato + Natural harmonic 1 | Do not let excitation cancel harmonic production: S01 + S08. |
| Downbow + Spiccato + Sul tasto | Bow direction, stroke, and position are separate: S06 + S04 + S05. |
| Bowed + LH pizzicato | Do not make LH pizzicato globally cancel bowed intent: S01 + S02; verify score/note/voice realization. |
| Muted + Cuivré | Preserve mute and tone color separately: W01 + W05. |
| Scoop + Fall (drop) | Preserve entry and exit intent: G02 + G03; timing is still an implementation concern. |
| Half-open + Stick (tip) + Edge (cymbals) + Right (RH) | Openness, contact, surface, and hand survive together: P01 + P05 + P06 + P07. |
| Sustain pedal + Sostenuto pedal + Una corda | Three independent states: K01 + K02 + K03. |
| Chorus + Delay | Two independent effects in E01, not one shared choice. |
| p + Crescendo + Accent | Retain level designation, shape, and attack: A10 + A12 + A04; do not apply loudness twice. |
| Non vibrato followed by Vibrato | Replace the vibrato value only; do not change mute, bow position, or connection. |
| Staccato then an unmarked note under a persistent direction | Remove the note-local shortening and retain the ongoing direction. |
| Closed followed by Open 2 | Replace openness only; retain beater/contact/surface/hand. |
| Pre-bend plus a subsequent bend, or Fade in plus Fade out | Resolve compound/time-sequenced representation before marking the candidate family mutually exclusive. |
| Two overlapping voices with different techniques | Preserve per-note association or use independent channels/voices; a single channel's latest CC vector alone is insufficient. |

A full Cartesian product is a useful **representation**, not a promise that
every tuple is physically meaningful. Mutually exclusive alternatives must not
collide, while impossible cross-family combinations can be diagnosed without
silently dropping data. One chair may also deliberately layer unlike instruments:
family optimizations must not discard intent required by another layer.

## Defaults, lifetime, aliases, and built-in coverage

### Defaults and state lifetime

1. **Unspecified is not explicit normal.** Do not automatically decode an idle
   vibrato dimension as Vibrato, or erase an explicit Non vibrato/Motor off.
   Reserve the distinction in the logical model before choosing numeric values.
2. **Natural is contextual.** Its five declared group memberships demonstrate
   that source groups overlap. Define and test its precise reset scope in the
   generated expression map; it is not safe to implement "zero every controller"
   by assumption. Natural fallback in an instrument map is also not the same
   operation as resetting the bridge.
3. **Attributes need expiry; Directions need cancellation.** A transient
   attribute can temporarily override a persistent property and then restore
   the underlying state. Reset to unspecified versus restoration must be
   explicit. Mixed-type rows (e.g. Vibrato, Bow stroke, Force dynamics) deserve
   tests, not a blanket per-row lifetime.
4. **Cancellation is part of a complete design.** A Direction with no built-in
   opposite still needs a tested endpoint/reset path. Inventorying a PT does
   not establish that the matching score notation emits its cancellation.
5. **Simile is unresolved control behavior.** Test whether Dorico expands it
   into continuing PTs or needs any bridge representation. Do not invent a
   sound switch merely to check the ID off.
6. **Mid-passage starts and transport resets matter.** Reconstruct current
   intent on playback/seek; do not rely on having received an earlier keyswitch.
   Preserve note association when overlapping events share an input channel.

### Aliases and fallbacks

The appendix preserves exact IDs and source relationships. Aliases are not
proof that the two spellings can be independently observed on the wire:

- Bowed aliases Natural.
- Tabla Ga aliases Open 2.
- Tabla Ge and Tu alias Open 1.
- Tabla Ke and Te alias Closed.
- Tabla Na aliases Natural.
- Tabla Ta aliases Muted.
- Tabla Ti aliases Choke.

An enum may list both identities for inventory completeness while Dorico resolves
them to one effective technique. Verify this before promising distinct decoding
or assigning redundant protocol values. Do not override built-in alias semantics
as a side effect of this project. Fallbacks, such as Marcato → Accent, are
separate from aliases: preserving explicit Marcato remains useful even when a
particular VSTi falls back.

### What "out of the box" should mean

For each built-in PT, eventually record one of: transmitted with tested
combinations/lifetime; handled by Dorico's ordinary note/controller performance;
resolved through a documented built-in alias/control rule; or explicitly
unsupported with a reason. Being listed in an expression map is not sufficient.

Dynamic marks and shapes, pitch gestures, pedals, rolls, trills, and arpeggios
particularly need a **division-of-responsibility decision**: Dorico may already
render their effect into timing, pitch, velocity, or controller data. Preserve
useful sample-selection intent without asking the VSTi/Fiddle to perform the
same effect twice. A PT enum alone cannot convey a continuous pedal depth,
bend trajectory, or the duration/size of a crescendo.

Likewise, some built-in PTs are used through percussion maps and technique-
specific noteheads rather than solely through pitched expression-map switches.
The inventory is complete for the source file; the score-to-wire activation
paths have **not** been comprehensively audited or tested in this draft.

## Controller budget: deliberately the next step

CC102–119 gives 18 controllers, each with 128 MIDI values. This table defines
musical meaning before deciding how to encode it. A strict one-CC-per-logical-
dimension mapping for **all 298 built-ins**, preserving the independent
properties proposed here, exceeds 18 dimensions. Counting names alone would
miss this; declaring unrelated effects mutually exclusive would conceal it.

The original approach still fits many core dimensions directly. For the complete
vocabulary, the next pass should measure:

- Which dimensions are needed concurrently, including unlike layers in one chair.
- Whether a few small independent dimensions can use an enumerated compound
  value on one CC while retaining distinct logical properties.
- Whether Dorico can reliably emit those compound states and restore them.
  CC messages carry complete values, not arbitrary independent bit updates;
  packed states cannot be assumed to compose automatically.
- Whether semantic reuse is genuinely unambiguous in one universal profile;
  otherwise retain chair-level profiles as an explicit fallback.
- How the existing CC102 note-length category and old saved projects are preserved
  or versioned during migration.

No CC numbers, bit fields, value assignments, extra non-reserved CCs, or
per-layer upstream profiles are proposed here. Every layer in a chair continues
to require the same upstream bridge interpretation.

## Decisions to review before generation

1. Approve the common dimensions and the specific combinations above.
2. Resolve the high-impact Provisional rows first: excitation/stroke, harmonic
   production, mute scope, repetition, dynamic shapes, and tonguing.
3. Inspect built-in score/percussion associations for specialized Review rows.
   Until then, preserve them individually; do not install speculative mutexes.
4. Specify neutral states, cancellation/restoration, alias observability, and
   the division between PT intent and Dorico-rendered performance data.
5. Only then budget CC values and generate a matched map/decoder plus acceptance
   score. Custom PT placement remains the second project, not an excuse to
   delay the useful built-in core.

The previous assigned-map audit remains valuable as a regression check against
actual instruments. Its independent-on/off repair proposals are a diagnostic
preview, **not the architecture for this built-in specification**.

## Appendix: complete exact-ID inventory

All 298 IDs appear once in this appendix. The group column refers to the proposal
above; Independent/Review families do not imply mutual exclusion.
A = Attribute; D = Direction. Source category and mutex membership are transcribed,
not inferred. A dash means no declaration, not "known unrestricted."

| Exact PT ID | Built-in label | Type | Source category | Proposed family | Source mutex groups (`ptmg.` omitted) | Alias / fallback |
| --- | --- | --- | --- | --- | --- | --- |
| `pt.accent` | Accent | A | Dynamics | A04 | — | — |
| `pt.alternativeFingering` | Alternative fingering | A | Techniques | W05 | — | — |
| `pt.arpeggioDown` | Arpeggio down | A | Ornaments | A09 | — | — |
| `pt.arpeggioUp` | Arpeggio up | A | Ornaments | A09 | — | — |
| `pt.artificialHarmonic` | Artificial harmonic | A | Techniques | S08 | — | — |
| `pt.backstick` | Backstick | A | Techniques | R01 | — | — |
| `pt.bartoksnapPizzicato` | Bartok/snap pizzicato | A | Techniques | S01 | — | fallback: `pt.pizzicato` |
| `pt.bassTone` | Bass tone | A | Techniques | R03 | — | — |
| `pt.beaterDreads` | Dreads (beater) | D | Techniques | P04 | beaters | — |
| `pt.beaterFelt` | Felt (beater) | D | Techniques | P04 | beaters | — |
| `pt.beaterPuffy` | Puffy (beater) | D | Techniques | P04 | beaters | — |
| `pt.beaterRods` | Rods (beater) | D | Techniques | P04 | beaters | — |
| `pt.bell` | Bell | A | Techniques | P06 | — | — |
| `pt.bellMute` | Bell mute | D | Techniques | W01 | primaryBrass | fallback: `pt.muted` |
| `pt.bend` | Bend | A | Ornaments | G04 | — | — |
| `pt.bowed` | Bowed | D | Techniques | S01 | primaryString | alias: `pt.natural` |
| `pt.bowOnBridge` | Bow on bridge | D | Techniques | S05 | — | — |
| `pt.bowOnTailpiece` | Bow on tailpiece | D | Techniques | S05 | — | — |
| `pt.bucketMute` | Bucket mute | D | Techniques | W01 | primaryBrass | fallback: `pt.muted` |
| `pt.buzzPizzicato` | Buzz pizzicato | A | Techniques | S01 | — | fallback: `pt.pizzicato` |
| `pt.buzzRoll` | Buzz roll | A | Techniques | A06 | — | — |
| `pt.centre` | Centre | A | Techniques | P06 | — | — |
| `pt.choke` | Choke | A | Techniques | P21 | — | — |
| `pt.chorus` | Chorus | A | Techniques | E01 | — | — |
| `pt.circularRoll` | Circular roll | A | Techniques | A06 | — | — |
| `pt.click` | Click | A | Techniques | R03 | — | — |
| `pt.closed` | Closed | A | Techniques | P01 | primaryPerc | — |
| `pt.closedEnd` | Closed end | A | Techniques | P02 | — | — |
| `pt.closedHole` | Closed hole | A | Techniques | P03 | — | — |
| `pt.colle` | Collé | D | Lengths | S04 | — | — |
| `pt.colLegnoBattuto` | Col legno battuto | D | Techniques | S03 | primaryString | — |
| `pt.colLegnoTratto` | Col legno tratto | D | Techniques | S03 | primaryString | — |
| `pt.conVibrato` | Con vibrato | D | Ornaments | A05 | vibrato | — |
| `pt.crescendo` | Crescendo | D | Dynamics | A12 | — | — |
| `pt.crescendoDiminuendo` | Crescendo-diminuendo | D | Dynamics | A12 | — | — |
| `pt.crossStick` | Cross stick | A | Techniques | R01 | — | — |
| `pt.crunch` | Crunch | A | Techniques | R02 | — | — |
| `pt.crunchChoke` | Crunch choke | A | Techniques | R02 | — | — |
| `pt.crush` | Crush | A | Techniques | P14 | — | — |
| `pt.crushDry` | Crush (dry) | A | Techniques | P14 | — | — |
| `pt.crushFat` | Crush (fat) | A | Techniques | P14 | — | — |
| `pt.cuivre` | Cuivré | D | Techniques | W05 | — | — |
| `pt.cupMute` | Cup mute | D | Techniques | W01 | primaryBrass | fallback: `pt.muted` |
| `pt.damp` | Damp | A | Techniques | P21 | — | — |
| `pt.deadStroke` | Dead stroke | A | Techniques | P21 | — | — |
| `pt.delay` | Delay | A | Techniques | E01 | — | — |
| `pt.detache` | Détaché | D | Lengths | S04 | — | — |
| `pt.detacheLance` | Détaché lancé | D | Lengths | S04 | — | — |
| `pt.diminuendo` | Diminuendo | D | Dynamics | A12 | — | — |
| `pt.diminuendoCrescendo` | Diminuendo-crescendo | D | Dynamics | A12 | — | — |
| `pt.ding` | Ding | A | Techniques | R03 | — | — |
| `pt.dipbend` | Dip/bend | A | Ornaments | G04 | — | — |
| `pt.distortion` | Distortion | A | Techniques | E01 | — | — |
| `pt.doit` | Doit | A | Ornaments | G03 | — | — |
| `pt.doubleShotOverTheHeader` | Over the head double shot | A | Techniques | R01 | — | — |
| `pt.doubleTongue` | Double-tongue | D | Techniques | W03 | — | — |
| `pt.down` | Down | A | Techniques | P08 | — | — |
| `pt.downbow` | Downbow | A | Techniques | S06 | — | — |
| `pt.edgeCymbals` | Edge (cymbals) | A | Techniques | P06 | — | — |
| `pt.edgeDrums` | Edge (drums) | A | Techniques | P06 | — | — |
| `pt.edgeRebound` | Edge rebound | A | Techniques | R01 | — | — |
| `pt.f` | f | D | Dynamics | A10 | dynamicLevel | — |
| `pt.fadeIn` | Fade in | A | Dynamics | A13 | — | — |
| `pt.fadeOut` | Fade out | A | Dynamics | A13 | — | — |
| `pt.fallDrop` | Fall (drop) | A | Ornaments | G03 | — | — |
| `pt.ff` | ff | D | Dynamics | A10 | dynamicLevel | — |
| `pt.fff` | fff | D | Dynamics | A10 | dynamicLevel | — |
| `pt.ffp` | ffp | D | Dynamics | A11 | dynamicLevel | — |
| `pt.fingerClick` | Finger click | A | Techniques | W09 | — | — |
| `pt.flange` | Flange | A | Techniques | E01 | — | — |
| `pt.flatCrash` | Flat crash | A | Techniques | R02 | — | — |
| `pt.flatRoll` | Flat roll | A | Techniques | A06 | — | — |
| `pt.flautando` | Flautando | D | Techniques | S07 | — | — |
| `pt.flip` | Flip | A | Ornaments | G04 | — | — |
| `pt.flutterTongue` | Flutter-tongue | D | Techniques | W03 | — | — |
| `pt.foot` | Foot | A | Techniques | R03 | — | — |
| `pt.fouette` | Fouetté | A | Dynamics | S07 | — | — |
| `pt.fp` | fp | A | Dynamics | A11 | dynamicLevel | — |
| `pt.frame` | Frame | D | Techniques | P06 | — | — |
| `pt.frictionSlide1` | Friction slide 1 | A | Techniques | P16 | — | — |
| `pt.frictionSlide2` | Friction slide 2 | A | Techniques | P16 | — | — |
| `pt.ghostNote` | Ghost note | A | Techniques | A14 | — | — |
| `pt.glissandoDown` | Glissando down | A | Ornaments | G01 | — | — |
| `pt.glissandoUp` | Glissando up | A | Ornaments | G01 | — | — |
| `pt.growl` | Growl | D | Ornaments | W05 | — | — |
| `pt.guitarPizzicato` | Guitar pizzicato | A | Techniques | S01 | — | fallback: `pt.pizzicato` |
| `pt.halfHarmonic` | Half-harmonic | A | Techniques | S08 | — | — |
| `pt.halfMuted` | Half-muted | D | Techniques | W01 | primaryBrass | fallback: `pt.muted` |
| `pt.halfOpen` | Half-open | A | Techniques | P01 | primaryPerc | — |
| `pt.harmonMuteStemExtended` | Harmon mute, stem extended | D | Techniques | W01 | primaryBrass | fallback: `pt.muted` |
| `pt.harmonMuteStemIn` | Harmon mute, stem in | D | Techniques | W01 | primaryBrass | fallback: `pt.muted` |
| `pt.harmonMuteStemOut` | Harmon mute, stem out | D | Techniques | W01 | primaryBrass | fallback: `pt.muted` |
| `pt.harnessHit` | Dress harness hit | A | Techniques | R01 | — | — |
| `pt.harpHarmonic` | Harp harmonic | D | Techniques | S08 | — | — |
| `pt.hiHat` | Hi-hat | A | Techniques | R03 | — | — |
| `pt.hitBowl` | Bowl hit | A | Techniques | P06 | — | — |
| `pt.hitCage` | Cage hit | A | Techniques | P06 | — | — |
| `pt.hitLoose` | Hit (loose) | A | Techniques | P12 | — | — |
| `pt.hitMedium` | Hit (medium) | A | Techniques | P12 | — | — |
| `pt.hitOverTheHead` | Over the head hit | A | Techniques | R01 | — | — |
| `pt.hitTight` | Hit (tight) | A | Techniques | P12 | — | — |
| `pt.jazzTurn` | Jazz turn | A | Ornaments | G04 | — | — |
| `pt.jete` | Jeté | A | Techniques | S04 | — | — |
| `pt.jetWhistle` | Jet whistle | A | Techniques | W04 | — | — |
| `pt.keyClick` | Key click | A | Techniques | W04 | — | — |
| `pt.keyClickWithPitch` | Key click with pitch | A | Techniques | W04 | — | — |
| `pt.laissezVibrer` | Laissez vibrer | A | Techniques | P21 | — | — |
| `pt.left` | Left (LH) | A | Techniques | P07 | — | — |
| `pt.leftHandTap` | Left-hand tap | A | Techniques | G06 | — | — |
| `pt.legato` | Legato | D | Lengths | A01 | legato | — |
| `pt.lhPizzicato` | LH pizzicato | A | Techniques | S02 | — | fallback: `pt.pizzicato` |
| `pt.loose` | Loose | A | Techniques | R03 | — | — |
| `pt.marcato` | Marcato | A | Dynamics | A04 | — | fallback: `pt.accent` |
| `pt.martele` | Martelé | D | Techniques | S04 | — | — |
| `pt.martellato` | Martellato | D | Lengths | S04 | — | — |
| `pt.mf` | mf | D | Dynamics | A10 | dynamicLevel | — |
| `pt.motorOff` | Motor off | D | Techniques | P19 | motor | — |
| `pt.motorOn` | Motor on | D | Techniques | P19 | motor | — |
| `pt.mouthClosed` | Mouth closed | D | Techniques | W06 | — | — |
| `pt.mouthOpen` | Mouth open | D | Techniques | W06 | — | — |
| `pt.mouthpiecePop` | Mouthpiece pop | A | Techniques | W04 | — | — |
| `pt.mouthPursed` | Mouth pursed | D | Techniques | W07 | — | — |
| `pt.mouthSlightlyOpen` | Mouth slightly open | D | Techniques | W06 | — | — |
| `pt.mouthWideOpen` | Mouth wide open | D | Techniques | W06 | — | — |
| `pt.mp` | mp | D | Dynamics | A10 | dynamicLevel | — |
| `pt.muffledDamped` | Muffled/Damped | A | Techniques | P21 | — | — |
| `pt.muffledStrings` | Muffled strings | A | Techniques | S09 | — | — |
| `pt.multiphonic` | Multiphonic | A | Techniques | W05 | — | — |
| `pt.muted` | Muted | D | Techniques | W01 | primaryBrass | — |
| `pt.nail` | Nail | A | Techniques | R03 | — | — |
| `pt.nailPizzicato` | Nail pizzicato | A | Techniques | S01 | — | fallback: `pt.pizzicato` |
| `pt.nasalVoice` | Nasal voice | D | Techniques | W07 | — | — |
| `pt.natural` | Natural | D | Techniques | C01 | primaryBrass, primaryKeyboard, primaryPerc, primaryString, primaryWind | — |
| `pt.naturalHarmonic1` | Natural harmonic 1 | A | Techniques | S08 | — | — |
| `pt.naturalHarmonic2` | Natural harmonic 2 | A | Techniques | S08 | — | — |
| `pt.nonLegato` | Non legato | D | Lengths | A01 | legato | — |
| `pt.nonVibrato` | Non vibrato | D | Techniques | A05 | vibrato | — |
| `pt.open1` | Open 1 | D | Techniques | W01 | primaryBrass | — |
| `pt.open2` | Open 2 | A | Techniques | P01 | primaryPerc | — |
| `pt.openEnd` | Open end | A | Techniques | P02 | — | — |
| `pt.openHole` | Open hole | A | Techniques | P03 | — | — |
| `pt.overdrive` | Overdrive | A | Techniques | E01 | — | — |
| `pt.overpressure` | Overpressure | D | Techniques | S07 | — | — |
| `pt.p` | p | D | Dynamics | A10 | dynamicLevel | — |
| `pt.palmMuting` | Palm muting | D | Techniques | G07 | — | fallback: `pt.muted` |
| `pt.pdlt` | P.d.l.t. | D | Techniques | S05 | — | — |
| `pt.pedal` | Pedal | A | Techniques | P20 | primaryPerc | — |
| `pt.pf` | pf | A | Dynamics | A11 | dynamicLevel | — |
| `pt.phaser` | Phaser | A | Techniques | E01 | — | — |
| `pt.pickScrape` | Pick scrape | A | Techniques | G08 | — | — |
| `pt.pince` | Pincé | D | Techniques | S09 | — | — |
| `pt.pinchHarmonic` | Pinch harmonic | D | Techniques | S08 | — | — |
| `pt.ping` | Ping | A | Techniques | R03 | — | — |
| `pt.pizzicato` | Pizzicato | D | Techniques | S01 | primaryString | — |
| `pt.pizzicatoAllaChitarra` | Pizzicato alla chitarra | D | Techniques | S01 | — | fallback: `pt.pizzicato` |
| `pt.pizzicatoSecco` | Pizzicato secco | D | Techniques | S01 | — | fallback: `pt.pizzicato` |
| `pt.plop` | Plop | A | Ornaments | G02 | — | — |
| `pt.plucked` | Plucked | D | Techniques | S01 | — | — |
| `pt.plungerMute` | Plunger mute | D | Techniques | W01 | primaryBrass | fallback: `pt.muted` |
| `pt.pop` | Pop | A | Techniques | R03 | — | — |
| `pt.portamentoDown` | Portamento down | A | Ornaments | G01 | — | — |
| `pt.portamentoUp` | Portamento up | A | Ornaments | G01 | — | — |
| `pt.portatoloure` | Portato/Louré | D | Lengths | S04 | — | — |
| `pt.portCrash` | Port crash | A | Techniques | R02 | — | — |
| `pt.pp` | pp | D | Dynamics | A10 | dynamicLevel | — |
| `pt.ppp` | ppp | D | Dynamics | A10 | dynamicLevel | — |
| `pt.preBend` | Pre-bend | A | Ornaments | G04 | — | — |
| `pt.pressRoll` | Press roll | A | Techniques | A06 | — | — |
| `pt.pull` | Pull | A | Techniques | P09 | — | — |
| `pt.push` | Push | A | Techniques | P09 | — | — |
| `pt.rake` | Rake | A | Techniques | R03 | — | — |
| `pt.release` | Release | A | Techniques | P21 | — | — |
| `pt.ricochet` | Ricochet | A | Lengths | S04 | — | — |
| `pt.right` | Right (RH) | A | Techniques | P07 | — | — |
| `pt.rightHandTap` | Right hand tap | A | Techniques | G06 | — | — |
| `pt.rightHandTapTapping` | Right-hand tap (tapping) | A | Techniques | G06 | — | — |
| `pt.rim` | Rim | A | Techniques | P06 | — | — |
| `pt.rimKnock` | Rim knock | A | Techniques | R01 | — | — |
| `pt.rimOnly` | Rim only | A | Techniques | P06 | — | — |
| `pt.rimShot` | Rim shot | A | Techniques | R01 | — | — |
| `pt.rip` | Rip | A | Ornaments | G04 | — | — |
| `pt.roll` | Roll | A | Ornaments | A06 | — | — |
| `pt.saltando` | Saltando | D | Lengths | S04 | — | — |
| `pt.sautille` | Sautillé | D | Lengths | S04 | — | — |
| `pt.scoop` | Scoop | A | Ornaments | G02 | — | — |
| `pt.scrape` | Scrape | A | Techniques | P10 | — | — |
| `pt.scrapeLeft` | Scrape (left) | A | Techniques | P10 | — | — |
| `pt.scrapeRight` | Scrape (right) | A | Techniques | P10 | — | — |
| `pt.scratchIn` | Scratch in | A | Techniques | P11 | — | — |
| `pt.scratchOut` | Scratch out | A | Techniques | P11 | — | — |
| `pt.sf` | sf | A | Dynamics | A11 | dynamicLevel | — |
| `pt.sfp` | sfp | A | Dynamics | A11 | dynamicLevel | — |
| `pt.sfz` | sfz | A | Dynamics | A11 | dynamicLevel | — |
| `pt.shake` | Shake | A | Ornaments | A08 | — | — |
| `pt.shell` | Shell | A | Techniques | P06 | — | — |
| `pt.shotOverTheHead` | Over the head shot | A | Techniques | R01 | — | — |
| `pt.sideStick` | Side stick | A | Techniques | R01 | — | — |
| `pt.simile` | Simile | D | Techniques | C02 | — | — |
| `pt.sizzle` | Sizzle | A | Techniques | R02 | — | — |
| `pt.sizzleSuck` | Sizzle suck | A | Techniques | R02 | — | — |
| `pt.skank` | Skank | A | Techniques | G08 | — | — |
| `pt.skin` | Skin | D | Techniques | P06 | — | — |
| `pt.slap` | Slap | A | Techniques | R03 | — | — |
| `pt.slapCongas` | Slap (congas) | A | Techniques | R03 | — | — |
| `pt.slapTongue` | Slap tongue | D | Techniques | W04 | — | — |
| `pt.slide` | Slide | A | Ornaments | G01 | — | — |
| `pt.slightBend` | Slight bend | A | Ornaments | G04 | — | — |
| `pt.smear` | Smear | A | Ornaments | G01 | — | — |
| `pt.snareOff` | Snares off | D | Techniques | P17 | snare | — |
| `pt.snareOn` | Snares on | D | Techniques | P17 | snare | — |
| `pt.snenor` | Snenor | A | Techniques | R03 | — | — |
| `pt.soft` | Soft | A | Dynamics | A14 | — | — |
| `pt.sonsXylophoniques` | Sons xylophoniques | D | Techniques | S09 | — | — |
| `pt.sostenutoPedal` | Sostenuto pedal | D | Techniques | K02 | — | — |
| `pt.spiccato` | Spiccato | D | Lengths | S04 | primaryString | fallback: `pt.staccato` |
| `pt.spoken` | Spoken | D | Techniques | W08 | — | — |
| `pt.squeeze` | Squeeze | A | Ornaments | G04 | — | — |
| `pt.staccatissimo` | Staccatissimo | A | Lengths | A02 | — | fallback: `pt.staccato` |
| `pt.staccato` | Staccato | A | Lengths | A02 | — | — |
| `pt.standClick` | Stand click | A | Techniques | R01 | — | — |
| `pt.stickButt` | Stick (butt) | A | Techniques | P05 | — | — |
| `pt.stickClick` | Stick click | A | Techniques | R01 | — | — |
| `pt.sticks` | Sticks | D | Techniques | P04 | — | — |
| `pt.stickShaft` | Stick (shaft) | A | Techniques | P05 | — | — |
| `pt.stickShaft1` | Stick (shaft 1) | A | Techniques | P05 | — | — |
| `pt.stickShaft2` | Stick (shaft 2) | A | Techniques | P05 | — | — |
| `pt.stickShaft3` | Stick (shaft 3) | A | Techniques | P05 | — | — |
| `pt.stickShaft4` | Stick (shaft 4) | A | Techniques | P05 | — | — |
| `pt.stickShaft5` | Stick (shaft 5) | A | Techniques | P05 | — | — |
| `pt.stickShaftOpen` | Stick (shaft open) | A | Techniques | P05 | — | — |
| `pt.stickShot` | Stick shot | A | Techniques | R01 | — | — |
| `pt.stickShoulder` | Stick (shoulder) | A | Techniques | P05 | — | — |
| `pt.sticksIn` | Sticks in | A | Techniques | R01 | — | — |
| `pt.stickSnap` | Stick snap | A | Techniques | R01 | — | — |
| `pt.stickTip` | Stick (tip) | A | Techniques | P05 | — | — |
| `pt.stickTip1` | Stick (tip 1) | A | Techniques | P05 | — | — |
| `pt.stickTip2` | Stick (tip 2) | A | Techniques | P05 | — | — |
| `pt.stickTip3` | Stick (tip 3) | A | Techniques | P05 | — | — |
| `pt.stickTip4` | Stick (tip 4) | A | Techniques | P05 | — | — |
| `pt.stickTip5` | Stick (tip 5) | A | Techniques | P05 | — | — |
| `pt.stir` | Stir | A | Techniques | R03 | — | — |
| `pt.stopped` | Stopped | A | Techniques | W02 | — | — |
| `pt.straightMute` | Straight mute | D | Techniques | W01 | primaryBrass | fallback: `pt.muted` |
| `pt.strumDown` | Strum down | A | Ornaments | G05 | — | — |
| `pt.strumUp` | Strum up | A | Ornaments | G05 | — | — |
| `pt.suck` | Suck | A | Techniques | R02 | — | — |
| `pt.sulPonticello` | Sul ponticello | D | Techniques | S05 | — | — |
| `pt.sulTasto` | Sul tasto | D | Techniques | S05 | — | — |
| `pt.sustainPedal` | Sustain pedal | A | Techniques | K01 | — | — |
| `pt.swish` | Swish | A | Techniques | R02 | — | — |
| `pt.swooshRoll` | Swoosh roll | A | Techniques | A06 | — | — |
| `pt.tabla-ga` | Ga (open) | A | Techniques | P22 | — | alias: `pt.open2`; fallback: `pt.natural` |
| `pt.tabla-ge` | Ge (open) | A | Techniques | P22 | — | alias: `pt.open1`; fallback: `pt.natural` |
| `pt.tabla-gi` | Gi (muted open) | A | Techniques | P22 | — | fallback: `pt.muted` |
| `pt.tabla-ke` | Ke (closed) | A | Techniques | P22 | — | alias: `pt.closed`; fallback: `pt.natural` |
| `pt.tabla-na` | Na (open) | A | Techniques | P22 | — | alias: `pt.natural` |
| `pt.tabla-ta` | Ta (muted open) | A | Techniques | P22 | — | alias: `pt.muted` |
| `pt.tabla-te` | Te (closed) | A | Techniques | P22 | — | alias: `pt.closed`; fallback: `pt.natural` |
| `pt.tabla-ti` | Ti (muted closed) | A | Techniques | P22 | — | alias: `pt.choke`; fallback: `pt.muted` |
| `pt.tabla-tu` | Tu (open) | A | Techniques | P22 | — | alias: `pt.open1`; fallback: `pt.natural` |
| `pt.tap` | Tap | A | Techniques | P13 | — | — |
| `pt.tapChoke` | Tap choke | A | Techniques | P13 | — | — |
| `pt.tapEdge` | Tap at edge | A | Techniques | P13 | — | — |
| `pt.tapHalfway` | Tap halfway | A | Techniques | P13 | — | — |
| `pt.tenuto` | Tenuto | A | Lengths | A03 | — | — |
| `pt.throw` | Throw | A | Techniques | R03 | — | — |
| `pt.throwoffOff` | Throwoff off | D | Techniques | P18 | — | — |
| `pt.throwoffOn` | Throwoff on | D | Techniques | P18 | — | — |
| `pt.tongueClick` | Tongue click | A | Techniques | W09 | — | — |
| `pt.tongueFingerClick` | Tongue and finger click | A | Techniques | W09 | — | — |
| `pt.tongueStop` | Tongue stop | A | Techniques | W04 | — | — |
| `pt.tremblement` | Tremblement | A | Ornaments | A08 | — | — |
| `pt.tremolo` | Tremolo | A | Ornaments | A06 | — | — |
| `pt.trill` | Trill | A | Ornaments | A07 | — | — |
| `pt.trill.half` | Trill (half-step) | A | Ornaments | A07 | — | — |
| `pt.trill.whole` | Trill (whole step) | A | Ornaments | A07 | — | — |
| `pt.tripleTongue` | Triple-tongue | D | Techniques | W03 | — | — |
| `pt.unaCorda` | Una corda | D | Techniques | K03 | — | — |
| `pt.up` | Up | A | Techniques | P08 | — | — |
| `pt.upbow` | Upbow | A | Techniques | S06 | — | — |
| `pt.vacuumSuck` | Vacuum suck | A | Techniques | R02 | — | — |
| `pt.vibrato` | Vibrato | D | Ornaments | A05 | vibrato | — |
| `pt.vibratoBarDip` | Vibrato bar dip | A | Ornaments | G04 | — | — |
| `pt.vibratoBarLine` | Vibrato bar line | D | Techniques | G04 | — | — |
| `pt.vibratoBarScoop` | Vibrato bar scoop | A | Ornaments | G04 | — | — |
| `pt.vocalDut` | Dut (vocal) | A | Techniques | W09 | — | — |
| `pt.volumeSwell` | Volume swell | D | Dynamics | A12 | — | — |
| `pt.wahWahGuitar` | Wah-wah | D | Techniques | E02 | wah | — |
| `pt.wahWahOpen` | Wah-wah open | A | Techniques | E02 | wah | — |
| `pt.wahWahStopped` | Wah-wah stopped | A | Techniques | E02 | wah | — |
| `pt.whaleCall` | Whale call | A | Techniques | R02 | — | — |
| `pt.whisper` | Whisper | D | Techniques | W08 | — | — |
| `pt.whistle` | Whistle | D | Techniques | W08 | — | — |
| `pt.whistleTone` | Whistle tone | D | Techniques | W09 | — | — |
| `pt.wideVibrato` | Wide vibrato | A | Ornaments | A05 | vibrato | — |
| `pt.zing` | Zing | A | Techniques | P15 | — | — |
| `pt.zingFast` | Zing (fast) | A | Techniques | P15 | — | — |
| `pt.zingSlow` | Zing (slow) | A | Techniques | P15 | — | — |
