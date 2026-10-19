# Orchestral strings bridge — historical r2 inspection prototype

Historical vocabulary and host evidence. For the current curated candidate, see
[the current overview](README.md) and [r3](r3/README.md).

**Import and inspect only. No compatible Fiddle decoder exists for this map.**
Nothing here installs a map, changes a playback template, or configures Fiddle.
This is the first concrete artifact under the
[current section-profile plan](../../docs/orchestral-bridge-maps.md), not a
continuation of the archived A–F probe encodings.

**Routing scope, agreed 2026-10-15: one Dorico voice per MIDI channel.** With
multiple voices on one channel, Dorico's output is treated as practically
unpredictable and exact agreement with direct playback is not required of the
bridge. The shared-voice overlap fixtures and d1/d2 investigations below are
historical evidence, not acceptance gates or active requests for more tests.
Their proposed follow-ups are superseded by this decision. Remaining validation
concerns one voice per channel, including its length transitions and note overlap.

**Host progress:** the user reports successful r2 import/inspection. The first
isolated [16-note playback smoke test](smoke-test.md) used only
the silent **Fiddle MIDI Probe**, not a production Fiddle endpoint. Playback
has now been captured: [first-pass results](smoke-results-01.md) show successful
expiry/replacements but missing Non vibrato and failed explicit Natural
cancellation. The user checked the active PT display and exported the map
unchanged. The [MIDI export comparison](smoke-results-02.md) now matches all
16 corrected PT sets. The [fresh live pass after reopening Dorico](smoke-results-03.md)
also passes all 16 corrected PT sets, with all 50 Init commands present. The
first-run discrepancy is not reproduced in the fresh session; its exact cause
is not established. Subsequent KS and overlap investigations follow.
The [12-note active keyswitch test](keyswitch-test.md) now
[passes all 12 PT sets after a score correction](keyswitch-results-02.md),
including two KS pitches plus a CC dimension together. No map changes were
needed. The marker-free [paired control/overlap test](overlap-test.md) has now
had two captures. The [fresh-session result and scope correction](overlap-results-02.md)
retire its pass/fail expectations: the test conflates shared-channel voice/PT
resolution with command ordering. Preserve it as diagnostic history, not an
active repeat request or evidence requiring a map change. The replacement
[shared-PT overlap test](shared-overlap-test.md) is now prepared: three bars,
12 notes, Flautando shared by both voices through the control and overlap bars,
then Natural for both. Its [first capture](shared-overlap-results-01.md) confirms
the overlap geometry but shows a Flautando On followed by a clear at bar 2's
start. The user confirmed the displayed active PT, and a
[repeat capture](shared-overlap-results-02.md) reproduces the critical command
sample positions exactly. No further unchanged repeat is needed. The separate
[single-base d1 diagnostic](single-base-diagnostic.md) was prepared for manual
import/assignment to a copy of the same score. It retains one unconditional
Natural base and all original add-on On/Off/Init/mutex data. Its [first host capture](single-base-results-01.md) passes all 12 PT sets: the
bar-2 clear disappears and bar-3 Natural cancellation works. This supports a
link to base-selection configuration, without identifying its exact mechanism.
It does not replace r2.
Use `nat`/`ord`, not the installed custom `natural`, for built-in cancellation.
It tests persistent CC state only, not changing techniques
during overlap. Independent voice intent requires distinct routed channels/endpoints.

**Future test-score preference:** omit instructional text markers from MusicXML;
put entry instructions in a separate Markdown guide. The helper defaults to
marker-free scores; completed historical fixtures remain byte-for-byte unchanged.
Generated manifest verification flags are not a
record of manual host acceptance; this note records the reported import result.

## Result

The broad candidate vocabulary uses **50/52 conduits**, leaving two spare:

| Item | Count |
| --- | ---: |
| Base articulation/length conduit | 1 |
| Add-on dimensions (enums and independent flags) | 49 |
| CCs, including the base (102–119) | 18 |
| Keyswitch pitches (0–20 and 115–125) | 32 |
| Spare conduits (KS126 and KS127) | **2** |
| Original base switches | 25 |
| Add-on switches | 106 |
| Base/add-on rows | 131 |
| Init switches | 1 (50 actions) |
| Distinct explicitly encoded built-in PT IDs | 110 |
| Built-in aliases accounted for | 1 (Bowed → Natural) |
| Explicit mutex groups | 22 |

This establishes representational capacity **for the documented vocabulary and
grouping**, not that Dorico reliably composes every combination. No PT was removed
to make the count fit. It includes a generous fringe of generic/jazz pitch
gestures, preserving them independently where exclusion would be speculative.
There is room for two additional independent dimensions or enum splits. We have not used
program changes, packed independent flags, or shared independent KS pitches.

Revision 2 incorporates the user's grouping review: Fouetté belongs to Bow stroke;
Flautando and Overpressure are alternatives in one Bow pressure group. This saves
two conduits without removing any PTs. Fouetté now also shares the bow-stroke
group's explicit Natural cancellation. The r1 XML is retained as a superseded
inspection artifact; import **r2** for current review. Its name/entity ID are new
because consolidation changes wire allocations after the affected dimensions.

## Scope and review boundaries

Violin, viola, cello and double bass, solo or section. This is **not** a harp,
guitar, keyboard, electronic-FX, or percussion profile. It covers every S-family
entry in our built-in inventory except the explicitly harp-specific harmonic;
Bowed is accounted for by its built-in Natural alias. Guitar-named pizzicato,
pinch harmonics and strummed pizzicato are retained as extended string colours.

General articulation, vibrato, dynamic intent, ornament and pitch gestures are
included. Dedicated guitar tapping/palm-muting vocabulary is left outside this
first profile; generic damping and muffled-string PTs are included. These scope
choices are explicit and reviewable, not a claim that such gestures are physically
impossible on a bowed string instrument. Dorico has no section-specific canonical
PT list in the inventory we used. **“Complete” here means complete for this stated
bowed-strings vocabulary, not every conceivable use of all 298 built-in IDs.**

- [Full 298-ID inventory disposition](inventory.md): every included ID, alias,
  and exclusion with a reason; no custom PT definitions.
- [Allocation and musical rationale](allocation.md): all conduit numbers,
  exact IDs, On/Off values, base codes and proposed cancellation groups.
- [Machine-readable allocation](allocation.json): documentation/test manifest,
  **not an installed or implemented decoder**.

The reserved notes leave MIDI **21–114** available for musical notes. Treat this
as a profile contract to validate, not a universal limit on strings (extreme
harmonics/transposition could exceed it). Octave labels differ between apps;
use MIDI numbers when inspecting actions. A future matching decoder must swallow
all command notes and releases before VSTi routing. These keys are unsafe to send
directly to an arbitrary VSTi, even when outside its sounding range.

## Musical semantics needing host validation

The 25 bases are copied from the hand-built Fiddle map, retaining conditions,
volume/velocity handling and timing. Each writes only CC102, values 0–24; the
Natural bases' old CC103 legato reset is removed. Staccato + Tenuto remains an
explicit compound base, and Tenuto is not mutually exclusive with Staccato.
Other possible compound base articulations are not added in this revision.

Each add-on writes only its own dimension. CC value 0 and KS velocity 1 mean
unspecified. Every add-on has an Off action clearing **only that dimension**.
KS On values begin at velocity 2; physical NoteOff is not a logical reset.
Init clears each dimension and sets the base to code 0. Whether all 50 Init
actions are sent as intended remains a host test, not an assumption.

Enums for bow stroke, contact, harmonic production and dynamic shape are musical
proposals, not inferred Dorico guarantees. Near-synonyms keep distinct canonical
IDs. Left-hand pizzicato, bow pressure, vibrato, contact and stroke are separate
dimensions. Flautando and Overpressure exclude each other, but either can coexist
with a contact point or stroke; Fouetté excludes other strokes. Fade in and fade
out are separate flags, as are
onset and release pitch gestures. Simile is only an explicit marker; it cannot
encode the state it refers to by itself.

Explicit Natural is included in the cancellation groups for excitation, bow
material, bow stroke (including Fouetté), bow contact, bow pressure, sons xylophoniques and
pincé. It deliberately does **not** cancel mute, vibrato, legato or dynamics.
Non legato explicitly replaces Legato. Natural base fallback sends no blanket
reset. Distinguishing explicit nat./arco cancellation from a Natural fallback,
and the effect of Dorico's built-in primaryString grouping, must be tested in
the host. The map disables automatic mutex-group generation, but this is not
proof of host behaviour.

Off timing/order is **provisional**, especially A→B replacement and overlapping
notes. An old Off arriving after a new On can erase the new value. A temporary
override expiring is not a promise to restore a previous direction. This artifact
does not settle those issues or implement note-state snapshots.

An entry for a PT does not prove that ordinary notation activates it. Dynamics,
hairpins, trills, glissandi and arpeggios can also involve Dorico-rendered MIDI
performance data. The map retains intent labels, not continuous curves or all
ornament parameters; it does not synthesize an additional ornament or replace
CC1/7/11. A future decoder must avoid double-performing Dorico's effects.

## Dorico inspection smoke test

1. Use a disposable project. Open **Library → Expression Maps**, use **Import
   Library**, and select [Fiddle-Orchestral-Strings-r2.doricolib](Fiddle-Orchestral-Strings-r2.doricolib).
2. Select **Fiddle Bridge - Orchestral Strings r2 - INSPECTION ONLY**. Check that
   it appears alongside, rather than replacing, the old Fiddle map.
3. Inspect the five sets of five conditional bases and representative add-ons:
   Vibrato (CC105), Bow contact (CC110), Bow pressure (CC112), Dynamic shape
   (KS0), and Simile (KS125).
   Compare On and Off actions against the allocation table.
4. Inspect the mutex groups: Vibrato alternatives share a group; Staccato and
   Staccatissimo share one but Tenuto does not; independent modifiers are not
   grouped together. Review the Natural cancellation choices above.
5. Optionally export just this map to a **different filename** and retain that
   round-trip file for comparison. Report missing rows, changed actions, labels,
   conditions, or groups. Import/inspection success is the first host gate.

**Do not assign it to the live Fiddle endpoint or a VSTi for playback.** Neither
the legacy decoder nor the experimental r6 decoder understands this allocation.
For the first isolated capture, follow the [playback test plan](smoke-test.md).
No Dorico restart or production template change is needed
for this import-and-inspect step.

## Reproduce and validate

From the repository root:

```sh
python3 experiments/orchestral-strings-bridge/generate.py --revision 2
python3 experiments/orchestral-strings-bridge/generate.py --revision 2 --check
python3 -m unittest discover -s experiments/orchestral-strings-bridge -p 'test_*.py' -v
python3 experiments/orchestral-strings-bridge/generate.py --revision 2 --check --dorico-catalog '/Applications/Dorico 6.app/Contents/Resources/playingTechniqueDefinitions.xml'
```

`profile.py` is the reviewed specification. The generator derives XML, allocation
tables and inventory from it and the repository's recorded built-in catalog.
Generation is deterministic; `--check` is read-only and catches stale artifacts.
The optional installed-catalog check verifies all 298 IDs/labels and the Bowed
alias without modifying Dorico. Local tests cannot establish Dorico import,
score-to-PT activation, or MIDI ordering. The user has reported successful import;
PT activation and MIDI ordering remain **unverified** until captured and reviewed.

Future wire changes need a new revision/entity ID; do not silently reuse r2 in
projects. This prototype intentionally does not change production profile
selection, the app, plugin, library maps, or playback templates.

The historical [two-base d2 diagnostic](two-base-diagnostic.md)
restores original conditional Natural bases 2 and 4 only, with all add-ons unchanged.
Its [first host capture](two-base-results-01.md) reproduces the original
On-before-Off sequence at the same sample times. The two retained conditional
bases suffice for this fixture; the exact internal mechanism remains unproven.
