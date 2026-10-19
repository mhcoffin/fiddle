# Strings r2 — first live smoke capture, 2026-09-24

**Later evidence:** the supplied MIDI export contains all required commands and
matches all 16 corrected PT sets; see [export comparison and next step](smoke-results-02.md).
The original live discrepancy remains real, but is not reproduced by export.

## Follow-up: active PT display and Dorico round-trip

The user confirms that Dorico's active-technique display shows
**Non vibrato + Overpressure + Sul tasto** at 3.1, and **Natural only** at 4.3.
This rules out simply forgetting the two markings. It does not by itself expose
the canonical IDs of similarly named score PTs or establish the MIDI output path.

The supplied `dorico-r2-roundtrip.doricolib` contains the same r2 map entity ID.
A recursive comparison of the entire XML tree (tags, attributes, ordered
children, and text with surrounding formatting whitespace ignored) finds
**only one difference**: `fileVersion` changes from `1.1438` to `1.1454`.
All 25 bases, 106 add-ons, 22 mutex groups, conditions, enabled flags and
Init/On/Off actions are unchanged. In particular, Non vibrato still has
On CC105=1 / Off CC105=0, and the Natural memberships are intact.
Export SHA256: `d70e8a6de1b69acd085fc92e3769c99cd62611c8f1d3e182dc59c87a714f3510`.
The smaller file size is serialization/formatting, not missing switches.

Thus import/export did not lose or rewrite the configuration. The remaining
distinction is score-to-switch selection versus the live VST3 parameter/capture
path. Next diagnostic: export MIDI from this unchanged test score to
`smoke-r2.mid`, retaining this capture and round-trip XML. Compare whether the
export contains the missing CC105=1 and the clears at note 15 before changing
the map, choosing a workaround, or blaming a specific Dorico algorithm.

**Expectation correction for future passes:** our original fixture expected
Non vibrato to survive explicit Natural at note 15 because Natural is absent
from the map's vibrato mutex group. The user's active display says Natural
alone, consistent with the supplied description of a general reset. Therefore
note 15 should be evaluated against no active add-ons in a revised test. Its
retained pressure/contact/stroke still fail that expectation, but absence of
Non vibrato there is **not** independently a failure. The original expectation
file and original comparison below remain unchanged as historical evidence.

## Original first-pass comparison

**Partial success; not a full pass.** All 16 expected musical notes arrived.
Eight match the complete expected PT set; eight differ. Non vibrato never became
active, and the intended explicit Natural cancellation did not happen. The
capture alone cannot distinguish incorrect/missing score PT associations from
Dorico switch/cancellation behaviour. No map or runtime changes are justified
until those two score inputs are checked.

## Evidence and integrity

- Original trace: `/var/folders/3k/4c2wqkxn5p50ldsh3glsd78r0000gn/T/fiddle-midi-probe-1790291990860528-2/capture.jsonl`.
- Preserved local copy: `local-captures/smoke-r2-pass1.jsonl` (git-ignored).
- Trace SHA256: `e8c6361e25c45f21dc8f2451cd11e2f2017ca03c1a3f10ecad4cd587f4b56f8a`.
- r2 map SHA256: `31e8826434ecda621459343e95725ca31e1c495053f309e9fc70f4c5b0b92647`.
- Closed schema-1 capture; footer reports 59,462 blocks and zero capture drops.
  The existing strict `read_capture` reader accepted every block/event, including
  sequence completeness, valid input and zero adapter drops/invalid inputs.
- One playing pass: blocks 57565–58626 inclusive; endpoint port 0 / channel 1
  (UI Port 1 / Channel 1). Sixteen positive-velocity musical NoteOns, all MIDI72.
  Fourteen selection/audition note-ons occurred outside playback and are excluded.
- At idle block 57554, all **50 Init commands** arrived: CC102–119 at value 0
  and all 32 allocated KS pitches at velocity 1. All dimensions were established
  before the first played note; no missing state was invented as zero.
- Each played note saw base CC102=2 (Natural, Medium). Decoding used this folder's
  r2 allocation, **not** the archived r6 decoder. Natural base is normalized to an
  empty articulation set. Physical KS releases are not logical dimension clears.

The trace does not carry an expression-map entity ID or the actual score PT
objects. Its complete Init allocation and active commands agree with r2, and
the note sequence agrees with the scaffold; this is not independent proof that
the imported map or score was never edited.

## Observed states at note-on

Only active add-ons are listed. Empty means initialized unspecified, not unknown.

| Notes | Location | Observed | Comparison |
| --- | --- | --- | --- |
| 1–2 | Bar 1 beats 1–2 | None | Match |
| 3–4 | Bar 1 beats 3–4 | Flautando | Match |
| 5–6 | Bar 2 beats 1–2 | None | Match: temporary expiry without a successor |
| 7–8 | Bar 2 beats 3–4 | Overpressure | Match |
| 9–10 | Bar 3 beats 1–2 | Overpressure + Sul tasto | Missing Non vibrato |
| 11–12 | Bar 3 beats 3–4 | Flautando + Sul tasto | Missing Non vibrato |
| 13 | Bar 4 beat 1 | Flautando + Sul tasto + Fouetté | Missing Non vibrato |
| 14 | Bar 4 beat 2 | Flautando + Sul tasto + Détaché | Missing Non vibrato |
| 15 | Bar 4 beat 3 | Flautando + Sul tasto + Détaché | Missing Non vibrato; intended Natural did not clear these three |
| 16 | Bar 4 beat 4 | Flautando + Sul tasto + Détaché + Vibrato | Vibrato arrived, but the other three should already have cleared |

CC105 was initialized to 0; no CC105=1 (Non vibrato) was recorded anywhere in
the activation. Its only nonzero played value is 3 (Vibrato), before note 16.
Thus the failure is not a Non vibrato command arriving late or being subsequently
erased by Off. The intended On command is absent.

No pressure/contact/stroke clears appear at note 15. At note 16 Dorico explicitly
reasserts all three along with Vibrato, so they are not merely stale values
retained by this offline decoder. The Natural fallback bases elsewhere correctly
coexist with add-ons; an explicit Natural cancellation still needs explanation.

## What did work

- Temporary Flautando ends without a replacement: CC112=0 at sample 130888,
  before note 5 at sample 132300. Notes 5 and 6 have no active add-ons.
- Pressure changes from Overpressure to Flautando while Sul tasto survives.
- Fouetté and then Détaché use their distinct values on the same bow-stroke CC.
  Pressure/contact survive both stroke choices.
- Three independent dimensions coexist on notes 13/14, and **four** on note 16
  (although three of those are unwanted there because cancellation failed).
  This is evidence of composition, not a pass for the intended note-16 state.
- All requested Init commands were observed in this activation, including KS
  resets. Active KS enum changes/coexistence are still untested in this score.

## Off cadence and ordering

Commands were inspected in recorded adapter order, without repairing/reordering
late messages. At changing combinations, Dorico often turns the previous entire
combination off and reasserts its surviving dimensions with the new one. For
example, before note 11, both Overpressure and Sul tasto clear at sample 329338;
Flautando and Sul tasto turn on at sample 330044; note 11 follows at 330882.
The Off→On gap is 706 samples, approximately 16 ms at 44.1 kHz.

Similarly, before note 14, all three old dimensions clear at 428563, then
Détaché/Flautando/Sul tasto turn on at 429269; note 14 is at 430239. There is no
observed old Off overwriting a new On at these tested replacements. Unchanged
successive notes do not necessarily receive a fresh On/Off cycle: notes 3/4
share one Flautando activation, and notes 7/8 share one Overpressure activation.
All four final dimensions clear at sample 527788 after the last musical release.

This matters for the future decoder: current state is transiently unspecified
between combinations, so it must snapshot each note's attributes rather than
retroactively changing sounding notes. This capture does **not** test overlapping
musical notes across those transitions.

Timing caveats remain:

- Eighteen playing preroll blocks have negative host sample positions. The trace
  preserves these; it is not a test of the legacy unsigned TCP time conversion.
- Equal-offset parameter/event-list ties occur at `(block, offset)` pairs
  `(57582,413)`, `(57841,106)`, `(57970,208)`, `(58358,2)`. The adapter uses its
  documented parameters-before-events policy. VST3 does not establish a total
  cross-list order, so these are not proof of host ordering.
- No backward host clock during this pass, or same-time same-CC set-then-reset,
  was observed. This is not a blanket guarantee for other scores or overlaps.

## Original next action (completed by user report and round-trip above)

In the saved test project, inspect the **actual playing-technique objects**:

1. Bar 3 beat 1: Non vibrato must refer to built-in `pt.nonVibrato`, not ordinary
   text, a similarly named vendor-defined PT, or a different vibrato option.
2. Bar 4 beat 3: the Natural/ord. marking must refer to built-in `pt.natural`,
   not the scaffold's instructional words or a differently associated marking.

If those associations are correct, preserve the score and export the expression
map actually assigned to this endpoint to a new filename for comparison. Then
isolate the two cases in the host; do not infer general add-on failure or remove
Off actions from this result. Do not advance to overlapping/KS/mid-start tests
until this discrepancy is understood.
