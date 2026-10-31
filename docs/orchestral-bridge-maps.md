# Orchestral bridge maps — current plan

Status: **authoritative design direction**, agreed in discussion on 2026-09-24.
This document supersedes conflicting bridge-map generation proposals listed
below. It records decisions and remaining validation work; the four production
profiles have **not** been installed or activated. Standalone strings, woodwind
and brass candidates are available. The [authoring workspace and source-derived
decoder](bridge-map-authoring.md) are implemented; production host acceptance
and coordinated installation remain pending.

## Section maps in the playback template (2026-10-19)

All five profiles now exist. [Pitched percussion r1](../experiments/orchestral-percussion-bridge/README.md)
covers timpani and the orchestral mallet instruments (review pending).
[Unpitched percussion r2](../experiments/unpitched-percussion-bridge/README.md)
(2026-10-31) covers Dorico's orchestral unpitched set plus toms, congas,
bongos, tambourine, blocks and cowbells as single instruments: its library
also holds the percussion map Dorico needs to play them, one pitch per
instrument, so the expression map carries every technique as in the other
sections. Template installation assigns each chair its section's map from the
Dorico instrument, pins the five revisions with the branch/version, and leaves
every other instrument on the universal map. There is no manual assignment
step; see [bridge maps](bridge-map-authoring.md).

## Wire schema 2 (2026-10-19)

The current templates are **strings r4, woodwinds r2, brass r2 and pitched percussion r1**. They keep
r3/r1 vocabulary, allocation and grouping, and change only the wire encoding.
Schema-1 maps (strings r1–r3, winds r1) remain frozen and are still decoded.

- **Typed Off.** An add-on Off writes On + 64 on its conduit. The decoder clears
  a dimension only when the Off names the technique currently held, so a late
  Off from a replaced technique cannot erase its successor.
- **Off debounce.** An Off that arrives within 30 ms of its own technique's On
  is ignored, in both schemas. This covers the base-change sequence below,
  where Dorico pre-rolls a restated On about 16 ms before the note and then
  sends the old combination's Off. No real temporary technique lasts that briefly.
- **Revision beacon.** Init sends KS126/KS127 with velocities derived from the
  map's entity ID and version (reserved; the pool is 50 conduits including the
  base). A matching beacon at playback start verifies the assignment without a
  manual export. A mismatch withholds notes and is reported in Bridge maps.
- **Gating.** Notes need a verified map (beacon or attested export) and a known
  base. Add-on dimensions not yet received count as unspecified, which is what
  Init sends. Unknown values still invalidate their dimension.

The **On-before-Off clear is treated as in scope.** It is triggered by base
(note-length) changes under a persistent add-on, which also happen with one
voice. The [one-voice note-length fixture](../experiments/orchestral-strings-bridge/one-voice-length-test.md)
is the host check for the defence above. Bridge maps shows, per input, how many
stale Offs were ignored and how many early Offs were suppressed.

## Supported routing scope — one voice per channel

Decision agreed on 2026-10-15: bridge design, validation and playback-fidelity
claims are limited to **one Dorico voice per routed MIDI channel** (within its
endpoint/port). Different voices need separate channels and independent
articulation state throughout the playback path.

If a user routes multiple voices to one channel, Dorico's output is treated as
practically unpredictable for this project, and the bridge is not required to
exactly reproduce direct Dorico playback. This is a support boundary, not a
claim that MIDI event processing is random or that every shared-channel passage
will fail. The limitation includes conflicting note-length base selections even
when the voices share the same written playing technique.

The shared-channel two-voice overlap fixtures, including the d1/d2 diagnostic
sequence, are retained as historical evidence. Their discrepancies are outside
the acceptance criteria and do not block the bridge work. Further shared-voice
timing isolation and proposed repairs from those reports are removed from the
current work queue. Do not change Off actions or length transmission to repair
those out-of-scope cases. (The 2026-10-19 typed Off and debounce address the
base-change component, which also occurs with one voice; see above.) This decision does not establish that cancellation,
composition or length transitions are correct within the supported scope;
remaining validation must use one voice per channel. Ordinary note overlap
within that voice, such as legato playback, remains relevant.

## Current strings candidate: curated r3 (2026-10-16)

The current [r3 inspection map](../experiments/orchestral-strings-bridge/r3/README.md)
prunes the broad r2 vocabulary to built-in PTs appropriate to violin, viola,
cello and double bass, solo or section. It has **32 add-on dimensions plus one
base articulation/length dimension**, using **33/52 conduits: 18 CCs and 15
keyswitch pitches**, with **19 spare**. There are **25 bases + 87 add-ons**, 91
distinct encoded IDs plus the Bowed alias, and 22 explicit mutex groups.

Seventeen independent dimensions and two values in retained enums were removed
(19 PTs total). Generic guitar/jazz pitch effects and ambiguous percussion
controls are deferred. Genuine bowed-string nail, buzz and strummed/alla-chitarra
pizzicato remain, along with common dynamics and articulation. This curation
does not invent aliases or impose mutex groups on unrelated PTs to save space.
See [scope decisions and evidence](../experiments/orchestral-strings-bridge/r3/curation.md),
[current allocation](../experiments/orchestral-strings-bridge/r3/allocation.md),
and the [full 298-ID disposition](../experiments/orchestral-strings-bridge/r3/inventory.md).

A guitar bridge is planned for a later phase. Customization affordances will
allow profile-specific additions when needed; speculative extensions need not
consume a default strings conduit now. R3 has a new map/group identity and
compact allocation. Its 25 base rows and retained cancellation/grouping semantics
are unchanged. The source-derived compiler supports r3; no r3 Dorico host acceptance
has occurred. Historical r2 files and capture allocations remain frozen at their
original paths; use `r3/` for current allocation work.

## Current woodwind and brass candidates: r1 (2026-10-16)

The [woodwind and brass candidates](../experiments/orchestral-winds-bridge/README.md)
reuse the same 25 base articulation/length rows and exact built-in PT IDs. Their
common dimensions deliberately overlap. All shared PTs preserve the same
conduit/reset/On values across current strings, woodwinds and brass. Section-only
dimensions reuse free conduits independently instead of allocating unused actions.

| Candidate | Add-on dimensions | Total conduits | CCs / KS pitches | Spare of 52 | Add-on rows | Encoded PT IDs | Mutex groups |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| [Woodwinds r1](../experiments/orchestral-winds-bridge/woodwinds-r1/README.md) | 35 | 36 | 18 / 18 | 16 | 64 | 68 | 14 |
| [Brass r1](../experiments/orchestral-winds-bridge/brass-r1/README.md) | 36 | 37 | 18 / 19 | 15 | 71 | 75 | 14 |

Woodwinds cover flute/piccolo, oboe/English-horn, clarinet, bassoon families and
orchestrally used saxophones. Brass cover horns/Wagner tubas, trumpets/cornets/
flugelhorns, trombones, euphonium/baritone and tubas. Section READMEs list supported
variants and range caveats. Each has a complete 298-ID disposition inventory.

Both retain independent flutter tongue, growl, multiphonics, alternative fingering
and applicable pitch gestures. Woodwinds add keyed/tongue attacks, whistle tones
and flute harmonics. Brass add inserted-mute choices, independent plunger, stopped
horn, cuivré and lip/slide effects. Open explicitly cancels Plunger and Stopped
through separate mutex groups; an inserted mute and plunger can coexist.
Natural is not a blanket reset. All new grouping and Off/cancellation choices
still require bounded host acceptance within one voice per channel.

Local structural, cross-profile, reproduction and installed-catalog checks pass.
Neither new map has been host-tested or installed in Dorico. The authoring
workspace can compile and explicitly assign their decoders. Pitched percussion remains ungenerated. Current source and
generation commands are in the [candidate overview](../experiments/orchestral-winds-bridge/README.md).

## Historical strings r2 inspection and host evidence

At the user's request, generation of a complete candidate strings vocabulary
has been brought forward **before** settling all host cancellation semantics.
The [r2 inspection map and instructions](../experiments/orchestral-strings-bridge/r2-history.md)
provide a concrete Dorico import/inspection smoke test, with no Fiddle integration.

The documented vocabulary uses **50/52 conduits**: 18 CCs and 32 KS pitches,
including one base conduit. It has **25 bases + 106 add-ons**, 110 explicitly
encoded built-in PT IDs plus Bowed through the built-in Natural alias, and
22 mutex groups. There are **two spare conduits** in this broad candidate.
Revision 2 moves Fouetté into Bow stroke and combines Flautando/Overpressure
into one Bow pressure group, as requested in the user's review. All PTs remain;
Fouetté also acquires the bow-stroke group's explicit Natural cancellation.
The r1/r2 XML remains historical; r3 above is the current allocation.
The [allocation table](../experiments/orchestral-strings-bridge/allocation.md)
and [298-ID scope inventory](../experiments/orchestral-strings-bridge/inventory.md)
make the curation/grouping choices explicit. Counts establish representational
capacity for that vocabulary, not reliable host composition or universal coverage
of every conceivable extended string technique.

Local structural/reproducibility checks and comparison with the installed
built-in catalog pass. The existing native parser also reads the file and sees
all 110 encoded IDs. **The user reports successful r2 Dorico import/inspection.**
The first live capture is a partial success: expiry and replacements worked,
but Non vibrato never arrived and explicit Natural did not cancel. See
[first-pass results](../experiments/orchestral-strings-bridge/smoke-results-01.md).
The user subsequently confirmed the active PT display, and the round-trip map
is unchanged. The [MIDI export](../experiments/orchestral-strings-bridge/smoke-results-02.md)
matches all 16 corrected PT sets, including Non vibrato and Natural cancellation.
The [fresh live capture after reopening Dorico](../experiments/orchestral-strings-bridge/smoke-results-03.md)
also passes all 16 corrected PT sets with all 50 Init commands present. No map
change was needed. The first-run discrepancy is consistent with stale playback
state but its exact cause is unproven. Subsequent tests examined active KS groups
and overlapping notes. The KS artifact is the
[12-note active KS test](../experiments/orchestral-strings-bridge/keyswitch-test.md):
Sons xylophoniques on low KS8, Strum up/down on high KS120, and Vibrato on CC105.
It uses the unchanged r2 map, with no extended-duration editing or overlapping
notes. Its [first capture](../experiments/orchestral-strings-bridge/keyswitch-results-01.md)
matches 8/12 PT sets: the first KS enum replacement and low/high KS coexistence
work, but the four later strum commands are absent. After the user corrected
the score, the [second KS capture](../experiments/orchestral-strings-bridge/keyswitch-results-02.md)
passes all 12 PT sets, including KS/KS/CC coexistence, enum replacement and
expiry. No map changes were needed. The
[paired overlap fixture](../experiments/orchestral-strings-bridge/overlap-test.md)
has had two captures, including a fresh session. The
[scope correction](../experiments/orchestral-strings-bridge/overlap-results-02.md)
retires its acceptance expectations: physical overlaps are confirmed, but
shared-channel voice/PT resolution was not independently established. Do not
repeat the unchanged fixture or infer a map defect from its mismatch count.
The replacement [shared-PT overlap test](../experiments/orchestral-strings-bridge/shared-overlap-test.md)
uses three bars and 12 notes with intended Flautando shared by both voices
through control/overlap bars, then Natural for both. Its
[first capture](../experiments/orchestral-strings-bridge/shared-overlap-results-01.md)
confirms physical overlaps but shows an On followed by a clear at bar 2's start.
The user confirmed Flautando in the active PT display; a
[repeat](../experiments/orchestral-strings-bridge/shared-overlap-results-02.md)
reproduces the critical On-before-Off sequence at identical sample positions.
No more unchanged repeats are needed. A separate
[single-base d1 diagnostic](../experiments/orchestral-strings-bridge/single-base-diagnostic.md)
has a [passing first host capture](../experiments/orchestral-strings-bridge/single-base-results-01.md)
(2026-10-15): one unconditional Natural base, all original add-on On/Off/Init/mutex
data unchanged. All 12 PT sets match, the bar-2 clear disappears, and Natural
cancels Flautando at bar 3. This supports a link to base-selection configuration,
without proving the internal mechanism; d1 removes both other base rows and the
retained length condition. This remains historical diagnostic evidence; the
one-voice scope decision above retires further shared-voice isolation.
The installed Art Conductor `natural` maps to custom `pt.user.natural`; use
`nat`/`ord` for built-in `pt.natural`. This may explain earlier cleanup failures
but the old scores' identities are not yet verified. This tests persistent CC state only, not
distinct-technique replacement during overlap; independent voices on distinct
routed channels are a separate test. At the user's request, future MusicXML fixtures must omit instructional
text markers and use a separate detailed written guide instead.
Both this fixture and
the completed [16-note baseline](../experiments/orchestral-strings-bridge/smoke-test.md)
leave the expression-map XML and runtime unchanged. Broader Off-event
order, initialization, aliases, score activation and proposed natural-cancellation
groups retain their validation gates. This map must not be assigned to either
the legacy or experimental Fiddle decoder.

## Scope and decisions

Create four reusable Dorico-to-Fiddle **bridge maps**:

1. Orchestral woodwinds.
2. Orchestral brass.
3. Orchestral strings — the bowed orchestral string family, **not** a profile
   for every stringed instrument.
4. Orchestral pitched percussion.

Restrict this phase to orchestral instruments. Other instrument profiles can
be added later; do not silently broaden Strings to include harp/guitar or
Pitched Percussion to cover all keyboards/synthesizers. The allocation table
must explicitly list the instruments covered by each profile.

**Unpitched percussion** (2026-10-31) is the fifth section, single
instruments only: Dorico plays it through a percussion map, which the
section's library supplies as one pitch per instrument (Natural and the roll
kinds, so rolls sustain) while every technique still arrives through the
expression map's dimensions. Drum-set kits are later work; see
[the experiment](../experiments/unpitched-percussion-bridge/README.md) and
[bridge maps](bridge-map-authoring.md). User/vendor-defined playback
techniques are also a later project. Start with
the appropriate **built-in Dorico PTs** for each section. Retain exact canonical
IDs; aliases and score-notation activation require explicit review.

One profile is assigned to each chair, shared by **all** its layers. Each layer
keeps its own downstream **library expression map** for translating the decoded
musical intent into VSTi MIDI. Section profiles are not generated from the current
chair's vendor entries. Layer-map audits remain useful for detecting unmet needs;
adding a covered layer should not require a Dorico update. A layer requiring
another profile's vocabulary must produce a coverage warning, not silent loss.

## Base switches

Start with the **25 base switches in the hand-built
[Fiddle map](../resources/Fiddle_Universal.doricolib)**. They are five articulation
choices, each with five note-length conditions:

- Natural.
- Staccatissimo.
- Staccato.
- Staccato + Tenuto.
- Tenuto.

Encode the selected base/length entry using **one CC or one velocity-coded
keyswitch pitch**. One CC is the proposed simplest starting implementation;
the inspection prototype uses CC102. Do not multiply these bases by every
add-on combination. The existing 25 are the starting set, not proof that every
possible built-in articulation conjunction is already covered.

The current file already encodes the base in CC102, values 0–24. Its Natural
bases additionally send CC103=0 to clear legato; that second CC is **not extra
base encoding capacity**. The new proposed ownership rule is that the base
writes only its own dimension. Removing the cross-dimension reset must be paired
with tested legato cancellation, not treated as a behavior-free deletion.

## Add-ons, dimensions, and capacity

Use **add-on switches** for the other section-appropriate built-in PTs. Organize
genuine alternatives into meaningful mutual-exclusion groups so that Dorico
cancels a previous member when a new member becomes active. Mutex groups apply
to PTs used by add-ons, not only to base switches.

Allocate **one conduit per independent dimension**:

- An enum/mutex group uses one CC number or one reserved KS pitch, with a
  distinct value for each member.
- An independent PT uses its own binary dimension/conduit, even if there is
  only one named PT in that dimension.
- Do not force unrelated PTs into one mutex group to reduce the count.
- Simultaneously active dimensions must not share a CC number or KS pitch.
  Reusing a conduit among mutually exclusive values of the *same* dimension
  is the intended design.

The working capacity estimate is **52 conduits per profile**:
18 reserved CCs (102–119), 21 low pitches (0–20), and 13 high pitches (115–127).
Every profile can reuse the pool; it is not divided among the four sections.
One conduit is needed for the base. These are **candidate pitch reservations**,
not a verified musical-range guarantee. Check each profile's supported sounding
range, transposition, and unusual instruments before assigning them.

Value allocation proposal, including the reset state:

- CC: 0 = unspecified; 1–127 = named PT values.
- Velocity-coded KS: reserve positive velocity 1 = unspecified; 2–127 = named
  values (126 available). Velocity zero is not an ordinary data command.
- Physical KS NoteOff/velocity-zero events release command pulses; do not use
  them to infer which velocity-coded PT has ended. Consume command notes and
  their releases before any layer/VSTi sees them, under the matching profile only.

The user proposed values 1–127 for add-ons. Reserving one positive KS velocity
for unspecified is a refinement to validate, not a constraint on CC values.

With independent add-on composition, switch count is approximately:

**25 base entries + one add-on per additional PT + initialization.**

Ten add-ons in two mutex groups, for example, need 35 base/add-on entries and
three conduits, not a Cartesian-product map. Counts may change for explicitly
justified compound/default cases; note-length conditions are already included
in the 25 bases. Conduit count depends on dimensions, not the total PT count.

**Capacity for all four complete profiles has not yet been verified.** The
curated r3 strings candidate uses 33 of the 52 conduits, woodwinds r1 uses 36,
and brass r1 uses 37 as detailed above. Pitched percussion has not been budgeted. The
earlier 18-CC strings draft covered 60 built-in PTs but had provisional groups
and deferred techniques. It is evidence of feasibility, not a completed budget.
Produce the four allocation tables, with explicit section-relevance dispositions
for omitted PTs. The 2026-10-16 curation explicitly authorizes deferring techniques
outside the default strings vocabulary to a guitar/other profile or customization.
If a profile really
does not fit, explicitly review less-common exclusions or alternate encodings;
never silently remove requirements.

## Off events: policy

Implemented in wire schema 2 (above): Off actions remain, each Off is typed by the
technique it ends, and an Off within 30 ms of its own On is suppressed. The
original reasoning is retained below. The discussion did **not** decide to
remove Off events. The working proposal was:

> On sets this dimension to a named value; Off clears only this dimension to
> unspecified. Natural is not a global reset, and unspecified is not an explicit
> request for Vibrato, Open, Bowed, or another default-looking PT.

Replacement and disappearance are different cases. A new Vibrato On can replace
Non vibrato directly. But when a temporary Flautando ends without a successor,
Fiddle needs a clear command or it retains stale Flautando. Dorico's internal
mutex cancellation is not itself a MIDI message to Fiddle. With this latched
protocol, removing all Off actions would require some other proven mechanism
for communicating absence; the current proposal does not supply one.

Conversely, a late Off from an old switch must not erase a newer value. Test
event cadence and order, including overlaps, rather than assuming Off occurs
exactly once when a persistent direction finally ends. Dorico's documentation
describes On/Off actions in terms of note starts/ends, and Steinberg's add-on
examples explicitly use On/Off values to enable/disable a property.
[Actions and conditions](https://www.steinberg.help/r/dorico-pro/6.2/en/dorico/topics/library/library_expression_maps_dialog_r.html),
[Steinberg add-on examples](https://forums.steinberg.net/t/expression-maps-for-dorico-3-5/148834).

Keep these distinctions in the implementation contract:

- Explicit *nat./ord.* notation has a reviewed cancellation scope; merely
  selecting a Natural base fallback must not erase every add-on.
- A temporary mutex override ending is **not a stack pop**. Our test did not
  restore the preceding Vibrato without an explicit restatement. Do not invent
  musical intent in the decoder.
- Snapshot the received endpoint/channel state for each note. Later controller
  changes must not retroactively change that note's input attributes. A snapshot
  cannot recover independent voice intent absent from the MIDI stream or isolate
  continuous controllers on held VSTi notes sharing a channel. Simultaneous
  independent voice techniques need Dorico independent voice playback and
  distinct routed channels/endpoints; see the overlap scope correction above.
- Initialization, mid-passage starts, stop/restart, seek, and reconnect must
  reconstruct state. The export/live ordering caveats are not closed by this plan.

## Evidence behind reconsidering add-ons

The earlier rejection of add-ons in general was too broad:

- **A r2:** 16/16 exported PT sets matched using separate CCs, including four
  concurrent add-ons and their staged cancellations.
- **B r2:** packed modifiers sharing CC116 lost information (13/16 matched).
  This failed that encoding scheme, not the general base-plus-add-ons rule;
  the capture did not isolate internal selection versus action conflicts.
- **D r4 / E r5:** sharing one KS pitch across independent dimensions lost
  commands; separating the pitches recovered 16/16 with the same velocities.
- **F r6 live:** captures recovered 16/16 from the beginning and 5/5 from a
  fresh start at note 12. All four modifier commands preceded that starting note.
  Tested vibrato mutex replacements and coexistence with independent modifiers
  worked. Automatic restoration after a temporary override did not.

Sources: [round 2](../experiments/dorico-strings/results/round2.md),
[round 4](../experiments/dorico-strings/results/round4.md),
[round 5](../experiments/dorico-strings/results/round5.md), and
[live r6 evidence and caveats](../experiments/dorico-strings/live-results.md).
These are bounded observations, not proof of arbitrary numbers of simultaneous
add-ons, all transitions, or every endpoint/voice combination. We have not
isolated precedence between an exact base and an equivalent base-plus-add-ons
decomposition with distinguishable output markers.

## Next work, in order

0. **Host-check wire schema 2** with the [one-voice note-length fixture](../experiments/orchestral-strings-bridge/one-voice-length-test.md):
   persistence across length changes, beacon verification, and the Off counters.
1. **Current strings vocabulary is curated in r3** (re-encoded as r4). Local generation/catalog
   checks pass. R2 import/inspection passed by user report, but that does not
   establish r3 host acceptance. Retained bow-stroke/pressure grouping is unchanged.
   Explicit project assignment now enables the source-derived decoder; templates
   remain inspection candidates pending host acceptance.
2. **Focused host test of cancellation/composition within one voice per channel.** Cover unchanged directions
   over repeated notes, same-group A→B replacement, temporary expiry without a
   successor, note-length base transitions, overlap within the same voice,
   independent group coexistence, mid-passage starts,
   and exact-base versus equivalent add-on precedence. Inspect actual event order
   and reconstructed note PTs. Settle Off semantics before production integration;
   creation of the inspection prototype is not evidence that this gate has passed.
   Shared-channel multi-voice fixtures are historical, not acceptance gates or
   requests for additional tests.
3. **Complete/review the four vocabulary/allocation tables.** Use the
   [built-in inventory](dorico-built-in-technique-groups.md), resolve provisional
   groups, name supported instruments, reserve safe command pitches, and report
   exact PT, dimension/conduit, and switch-entry counts plus remaining capacity.
   Separate PT intent from continuous dynamics/pedal/pitch data or effects Dorico
   already performs; do not promise those are fully represented by a PT enum.
   Strings r3, woodwinds r1 and brass r1 tables/candidates are generated;
   pitched percussion remains to be created. New woodwind/brass groups and
   section-specific range contracts remain subject to review and host acceptance.
4. **Validate source-derived map/decoder pairs.** The authoring workspace now
   compiles the stored map directly and versions its source and assignments.
   A structured editor and compiled-profile coverage audit remain future work.
   Validate the pairs in isolated host projects before production use.
5. **Assignment and installation foundation is implemented.** Project settings
   pin immutable source/decoder revisions by input. Chairs installation writes
   those maps and their endpoint/preset references without changing allocation.
   Changed pins mark the template dirty; older bridge files remain available.
   Follow the [template smoke test](bridge-map-authoring.md#template-smoke-test)
   for Dorico restart, application and export verification. Writing files is not
   proof of host assignment or synchronization. Full portable host snapshots
   remain future work.

## Supersession and implementation status

This is the single current bridge-generation plan. In a conflict, follow this
document, not a historical proposal or an experiment's old “next test” paragraph.

| Earlier approach/document | Status now |
| --- | --- |
| One universal bridge covering all built-in PTs | Superseded by four orchestral section profiles. The existing hand-built resource remains the legacy runtime map and base-switch starting point. |
| Chair-specific, base-only Cartesian enumeration in [chair coverage](chair-coverage-algorithm.md) | Superseded as a generation strategy. The implemented analyzer remains a diagnostic; its product count is **not** the new plan's entry estimate. |
| Assumption that Dorico add-ons are generally unreliable | Withdrawn. Preserve the specific packing/shared-pitch failures and the successful independent-channel evidence. |
| Independent CC per missing PT in the Repair plan | Legacy diagnostic/preview, not the chosen built-in allocation strategy. |
| All-instrument [built-in inventory](dorico-built-in-technique-groups.md) | Retained reference; not a commitment to implement every family in this phase. Provisional groups still need review. |
| A–F probe procedures and suggested 720-base packing experiment | Historical experiments, not the current work queue or production encodings. Keep files/captures for reproducibility. |
| Custom/vendor PT configuration and additional instrument profiles | Deferred, not rejected. |

[Bridge profiles and audit reference](dorico-bridge-profiles.md) describes existing
tools and retained assignment/versioning constraints. The **Chair coverage** and
**Repair plan** UI tabs have not been removed or relabeled in this documentation
change. Violin 1's enormous product count and the shared-budget starvation are
limitations of the old analyzer, not evidence that this section-profile plan
needs an astronomical number of entries. Do not use those tabs as its install plan.

The original standalone inspection step added only XML and review tables. The
subsequent [authoring implementation](bridge-map-authoring.md) adds opt-in project
assignments and source-derived decoding. Historical probe XML/MIDI captures and
Dorico projects remain unchanged. No plugin installation is performed.

Historical isolation: [two-base d2](../experiments/orchestral-strings-bridge/two-base-diagnostic.md)
retains original r2 Natural bases 2 (Medium) and 4 (>Long) unchanged. All add-on
actions remain intact. Its [first host capture](../experiments/orchestral-strings-bridge/two-base-results-01.md)
(2026-10-15) reproduces the bar-2 On-before-Off sequence at the same sample times
as r2. The pair suffices in this fixture; the other 23 rows are unnecessary.
The exact internal scheduling mechanism remains unproven.
