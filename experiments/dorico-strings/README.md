# Experimental Strings bridge: allocation and Dorico wire probes

> **Historical experiments, not the active work queue (2026-09-24).** Follow
> [Orchestral bridge maps](../../docs/orchestral-bridge-maps.md) for the current
> four-profile plan. A–F files and captures are retained for reproducibility,
> not adopted as production encodings. Old “next test,” “untested,” and
> installation-status statements below or in individual procedures describe
> their original stage; later [live results](live-results.md) record subsequent
> outcomes. The proposed 720-base packing experiment is not the current approach.
> Independent-conduit add-ons succeeded in bounded tests; packing failures do
> not justify rejecting add-ons generally. Do not rerun old setup/install steps
> merely because they appear here.

**Not a replacement for the installed Fiddle map.** These six maps deliberately
use a new encoding that the running Fiddle decoder does not understand. Import
only into a disposable Dorico test project using a normal instrument, **not the
Fiddle plug-in**. Do not install in DefaultLibraryAdditions, change a working
score's endpoint, or use Fiddle's existing instrument-map picker to load these.
No experimental decoder has been installed or activated. A separate capture-only
[MIDI Probe](live-probe.md) is now available for safe live input tests; normal
Fiddle still cannot interpret these maps without the explicit experimental
server opt-in. [Live r6 results and server replay](live-results.md) document that
opt-in and the remaining live TCP/vendor-instrument acceptance gate.

## Historical revisions and export results

**r2 results are now available:** [round 2](results/round2.md). A r2 matches all
16 expected PT sets. B r2 still loses modifiers on notes 11–13, so the proposed
two-CC saving is not validated. Both retain a first-note length-ordering warning.
These files need no further identical re-export. C r3 subsequently matched all
16 expected PT sets in `keyswitch-r3.mid`, with that same ordering warning.
D r4 shared-pitch velocity commands then matched only 13/16 PT sets, losing
modifiers on notes 11–13 ([round 4](results/round4.md)). E r5 then recovered all
16 PT sets with separate pitches ([round 5](results/round5.md)). F r6 also matches
all 16 PT sets, including Non vibrato → Vibrato on pitch 127; the first-note CC102
ordering warning remains in the export. Live r6 captures now reconstruct 16/16
from the beginning and 5/5 from a fresh note-12 start; see [live results](live-results.md)
for the ordering/preroll caveats and downstream replay tests.
The larger base-switch packing alternative is superseded as the active plan.

The first real exports are analyzed in [round 1 results](results/round1.md).
Separate-controller modifiers worked, but compound switches did not. A generator
defect used comma-separated PTs where the existing Dorico maps use `+` conjunctions.
Fiddle's permissive parser masked the distinction. **Packing is inconclusive,
not ruled out by those captures.**

The A/B maps remain **r2** and byte-for-byte unchanged; C is a separate **r3**
map; D is **r4**, E is **r5**, and F is **r6**. Earlier generated maps remain
byte-for-byte unchanged. The completed r2 test used `separate-r2.mid` and `packed-r2.mid`; preserve
these captures. Importing a map alone does not change endpoint assignment.
No need to rebuild your score for F. Note 6 is
Staccatissimo + Marcato (without Tenuto); the expectation and scaffold are fixed.
For notes 4/5 use Dorico's combined Staccato-tenuto control if import loses Tenuto.

Both old files also place the first CC102=2 after the first note-on at tick zero.
The inspector now flags this ordering ambiguity; do not mistake it for another
missing PT or silently reorder it. Live-path validation remains necessary.

## Outcome so far

- A concrete **18-CC budget for 60 built-in PTs**, plus note-length information,
  targeting bowed orchestral strings. This is a working subset, **not all string
  PTs**, and its larger candidate groups still need musical review.
- Six importable experimental `.doricolib` files and matching exact-ID decoder
  JSON. All cover the same **21 PTs**; C/D/E/F add keyswitch specifications.
- Variant **A** uses eight CCs with independent add-ons on separate controllers.
- Variant **B** uses six CCs, packing three independent modifiers into one byte.
  If B works in Dorico, this would reduce the full budget from 18 to **16 CCs**.
- Variant **C** uses five CCs plus six reserved command notes. If validated,
  this could reduce the full draft budget to **15 CCs**. Its export PT sets pass;
  live-path behavior remains untested.
- Variant **D** uses the same five CCs plus one reserved pitch and six command
  velocities. Its export loses commands when independent modifiers coexist.
- Variant **E** uses D's velocities on three distinct pitches, one per dimension.
  All sixteen PT sets match its export.
- Variant **F** additionally moves vibrato from CC106 to pitch 127, using five
  velocities for its scalar states. Its baseline export matches all sixteen PT
  sets; Con vibrato, Wide vibrato, and temporary restoration remain untested.
- A read-only MIDI-export inspector and a 16-note expectation list. Automated
  tests validate serialization/decoding, not Dorico's actual switch selection.

**Export and r6 live-input testing are complete for the baseline score; full live
instrument acceptance remains pending.** The map XML follows
the structure used by the existing Fiddle map and is tested with Fiddle's real
expression-map parser; that does not establish Dorico acceptance or behavior.

## Proposed full allocation

Every scalar axis uses **0 = unspecified**, followed by the listed IDs in order
starting at 1. The exact values live in [profile.json](profile.json); this table
is a reading guide. Do not change list order without a new protocol revision.
Common properties should keep these CCs in future family profiles where feasible.

| CC | Property | Nonzero values / encoding | Included in wire probe? |
| --- | --- | --- | --- |
| 102 | Shortening × tenuto × accent × note length | 90 complete states, formula below | Yes |
| 103 | Connection | Legato; Non legato | Yes |
| 104 | Primary excitation | Plucked; Pizzicato; Bartok/snap; Buzz; Guitar; Nail; Alla chitarra; Secco | No |
| 105 | Bow stroke | Collé; Détaché; Détaché lancé; Jeté; Martellato; Martelé; Portato/Louré; Ricochet; Saltando; Sautillé; Spiccato | No |
| 106 | Vibrato | Non vibrato; Con vibrato; Vibrato; Wide vibrato | Yes |
| 107 | Mute | Muted; Open 1 | Yes |
| 108 | Position | Sul ponticello; Sul tasto; P.d.l.t.; Bow on bridge; Bow on tailpiece | Yes |
| 109 | Repetition | Tremolo | No |
| 110 | Harmonics | Natural 1; Natural 2; Artificial; Half-harmonic | No |
| 111 | Bow direction | Upbow; Downbow | No |
| 112 | Dynamic shape | Crescendo; Diminuendo; Crescendo-diminuendo; Diminuendo-crescendo; Volume swell | No |
| 113 | Trill | Generic; Half-step; Whole-step | No |
| 114 | Arpeggio | Up; Down | No |
| 115 | Col legno | Battuto; Tratto | No |
| 116 | Flautando | Present | Yes; B instead uses the combined modifier value |
| 117 | Overpressure | Present | A only; unused by B |
| 118 | Fouetté | Present | A only; unused by B |
| 119 | LH pizzicato | Present | No |

The proposed full budget is **18 of 18 reserved controllers**, with no spare.
Other-family PTs are not silently assigned alternative meanings in this strings
profile. If family profiles reuse a CC, the profile must be selected for the
**whole chair**, shared by every layer, with explicit matching map/decoder
versions. No in-band profile handshake is implemented in this experiment.

### CC102: complete base combinations, not competing add-ons

```
shortening = 0 unspecified, 1 staccato, 2 staccatissimo
tenuto     = 0 unspecified, 1 tenuto
accent     = 0 unspecified, 1 accent, 2 marcato
length     = 0 very short, 1 short, 2 medium, 3 long, 4 very long

CC102 = (((shortening * 2 + tenuto) * 3 + accent) * 5 + length)
```

There are 3 × 2 × 3 × 5 = **90 states**, numbered 0–89. For example, staccato +
tenuto + accent with medium length is **52**. Values 90–127 are unassigned and
the inspector rejects them. These are not compatible with the old CC102 layout.

The generator creates all 90 base switches and their length conditions.
**Dorico performs no arithmetic**: each complete base switch sends its constant
value. The decoder extracts the exact PT set and length from the same source
specification. Staccato and Tenuto are not put into one mutex group.

### CC116 in B: an experiment, not an assumed optimization

| Value | Active modifiers |
| --- | --- |
| 0 | None |
| 1 | Flautando |
| 2 | Overpressure |
| 3 | Flautando + Overpressure |
| 4 | Fouetté |
| 5 | Flautando + Fouetté |
| 6 | Overpressure + Fouetté |
| 7 | All three |

B defines all seven nonempty combinations as explicit add-on switches. It does
**not** ask separate add-ons to OR together bit values. Whether Dorico reliably
selects the combined add-on rather than conflicting subsets, and restores the
right combined value when an Attribute ends, is exactly what we must measure.
The deliberately unusual Flautando/Overpressure combination is a wire stress
test, not a recommendation for musical notation.

Dorico's documented selection process can combine a base with add-ons, but is
not an arbitrary event transport. The developer's explanation is the reason to
test selection, not just validate XML.
[Steinberg: switch selection](https://forums.steinberg.net/t/expression-maps-how-are-switches-selected/812723/4).

If A works and B fails, retain A's independent controllers. A possible later
fallback is to enumerate those combinations in **base** switches and have each
base write both CC102 and CC116. That would require 720 base entries and still
needs a Dorico test; it is not implemented here. Do not ship a packing scheme
merely because its decoder passes a unit test.

## Deliberate gaps and cancellation policy

- **No built-in PT definitions are redefined.** The maps reference existing
  exact IDs and have their own map/group IDs, separate from `xmap.user.Fiddle.1`.
- Bowed aliases Natural in the installed built-ins, so the budget does not
  pretend to transmit a separate Bowed identity. Alias observability remains
  an explicit limitation, not a silent alias rewrite.
- All probe CCs initialize to zero. Each add-on has an off action returning its
  own CC to zero. The decoder never invents an initial state absent from the
  capture. CC102 has no note-off reset that could erase a later note's value.
- Explicit Non vibrato, Non legato, and Open 1 retain distinct nonzero values.
- In this probe, Natural is explicitly in the **Position** mutex group only.
  It is not in Vibrato, Mute, or Connection. This is a proposed reset policy to
  test, **not a claim that it overrides every implicit Dorico cancellation**.
  Natural base selection itself does not emit blanket resets.
- A wide-vibrato Attribute within a persistent vibrato Direction must restore
  the underlying state afterwards, not merely send zero. The same issue applies
  to the packed modifier Attribute. Only real traces can establish restoration.
- Dynamics remain Dorico's CC1 plus note-velocity output; the probe does not
  duplicate dynamic curves. The full allocation merely reserves CC112 for
  shape intent; automatic activation and double-rendering concerns remain open.
- Steady/force dynamic PT identities, ghost/soft/let-ring modifiers, fades,
  special damping, and glissando/portamento gestures are **not covered** by this
  budget. `deferred` records these gaps. Ordinary MIDI performance may capture
  an effect without preserving the PT identity; that is not full PT coverage.
- Harp/guitar-specific production and techniques need their own scope pass.
  The remaining built-in inventory is not discarded: see the
  [full proposal](../../docs/dorico-built-in-technique-groups.md).
- No production profile UI, migration, or automatic Dorico assignment is
  included. The JSON tables support offline diagnostics and an explicit,
  session-only r6 server decoder; normal launches retain the legacy protocol.

## First Dorico test: a disposable 16-note score

1. Open [acceptance.musicxml](generated/acceptance.musicxml) as a **new project**
   in Dorico, using a normal Dorico instrument, **not Fiddle**. The scaffold
   contains sixteen quarter notes at 120 BPM, labeled T1–T16, with the ordinary
   articulations already entered. Verify their import; the PT directions in
   steps 7–16 below must still be added manually. Alternatively, enter the notes
   in a new one-violin project: four bars of 4/4, no ties/slurs/repeats/chords.
2. Import [A](generated/separate.doricolib) and [B](generated/packed.doricolib)
   in Dorico's Expression Maps dialog. They appear as **Fiddle EXP Strings A -
   separate r2** and **Fiddle EXP Strings B - packed r2**. Assign A r2 to the test endpoint.
3. Apply the following markings. Ensure the **Playing Techniques lane** reports
   the intended PT combination; printed text alone is not evidence of activation.

| Note | Marking / action | Expected retained intent |
| --- | --- | --- |
| 1 | Unmarked | No explicit PT |
| 2 | Staccato | Staccato |
| 3 | Tenuto | Tenuto |
| 4 | Staccato-tenuto | Both |
| 5 | Staccato-tenuto plus accent | All three |
| 6 | Staccatissimo + marcato | Both; Dorico does not offer Staccatissimo-tenuto |
| 7 | Non vibrato begins | Non vibrato |
| 8 | Accent | Non vibrato + Accent |
| 9 | No new marking | Non vibrato |
| 10 | Flautando, explicitly extended through note 14 | Non vibrato + Flautando |
| 11 | Overpressure, explicitly extended through note 13 | Add Overpressure |
| 12 | Fouetté Attribute on this note only | Add Fouetté |
| 13 | No new marking | Fouetté gone; other three retained |
| 14 | No new marking | Overpressure gone; Non vibrato + Flautando retained |
| 15 | No new marking | Non vibrato only |
| 16 | Vibrato | Vibrato replaces Non vibrato |

For uncommon markings, use a **project-local playing-technique appearance linked
to the existing built-in PT** if needed. Do not create a new playback technique
with the same name. Do not edit global/library defaults. If Dorico will not
produce one of these requested combinations, record that as an activation
limitation rather than hand-editing the expected result to claim success.

4. Play from the start, then export this project/flow as MIDI to `separate-r2.mid`.
   Inspect it with the command below. Repeat after assigning B r2, saving its MIDI
   as `packed-r2.mid`. Do not compare two exports accidentally made with A.
5. Send back the two MIDI files (and the small `.dorico` project if a case fails).
   An export missing initialization or reserved CCs is **inconclusive**; use a
   live raw-MIDI capture for the same test rather than adding guessed defaults.

From the repository root:

```sh
python3 experiments/dorico-strings/inspect_midi.py /absolute/path/separate-r2.mid --variant separate --expect experiments/dorico-strings/expected.json
python3 experiments/dorico-strings/inspect_midi.py /absolute/path/packed-r2.mid --variant packed --expect experiments/dorico-strings/expected.json
```

The inspector prints each note's exact PT IDs, length, and missing/invalid state,
plus mismatches. It preserves same-tick file order rather than moving CCs in
front of notes to make a test pass. It isolates ports/channels, requires explicit
track selection if several tracks contain notes, and refuses to guess ordering
when relevant controllers are split across tracks. It supports type 0/1 PPQN
SMF, not SMPTE, type 2, arbitrary text dumps, or proprietary MIDI containers.

**Passing an export is not proof of the live VST3 path.** Once export selection
works, repeat with raw incoming MIDI captured before Fiddle's legacy decoder
(or a MIDI monitor) and test late CC ordering and note association separately.
The experiment intentionally does not wire these maps into the running app.

## Further host tests before a production map

- Mute + vibrato + position; then change only position. Check all three survive.
- Add Natural while mute/vibrato persist; compare actual cancellation with the
  proposed Position-only scope. Also test Bowed because of its Natural alias.
- Wide vibrato Attribute inside an ongoing Non vibrato Direction; verify return
  to Non vibrato, not unspecified. Inspect Con vibrato separately from Vibrato.
- Legato/non-legato combined with staccato/tenuto, not just separate test notes.
- Start playback at note 12. Verify complete reconstruction of active states;
  do not accept stale values left over from a previous full playthrough.
- Stop/restart, loops, and adjacent changes at exactly the same timestamp.
- Notes of all five length categories; inspect CC102 before each note-on.
- Overlapping notes/voices with different attributes: detect off-event clobbering
  and channel-sharing limits. Do not claim arbitrary polyphony is solved.

## Reproduce / validate

Only Python's standard library is needed:

```sh
python3 experiments/dorico-strings/generate.py
python3 experiments/dorico-strings/generate.py --check
python3 experiments/dorico-strings/generate.py --check --builtins '/Applications/Dorico 6.app/Contents/Resources/playingTechniqueDefinitions.xml'
python3 -m unittest discover -s experiments/dorico-strings -p 'test_*.py' -v
cmake --build build/release --target ExpressionMapTest --parallel 3
build/release/ExpressionMapTest_artefacts/Release/ExpressionMapTest /Users/mhc/fiddle/example-expression-maps
```

The generator only writes the thirteen named files under `generated/` (or an explicit
output directory). `--check` is read-only. Decoder tables contain a map digest,
profile/variant identity, and specification fingerprint. Changing XML alone
causes the inspector to reject the pair. Generated files are kept alongside
the source so the actual test maps can be reviewed and imported without setup.

These scripts test known wire states and XML round trips. They intentionally do
not implement a fake Dorico selector whose success could be mistaken for host
evidence. Next are controlled live instrument acceptance and further
member/restoration tests. A–F exports retain their first-note warning; live F
uses the adapter's declared equal-offset ordering policy. Probe capture, C++
server replay, deterministic hosted audio, and actual vendor-instrument playback
are distinct validation stages; see [live-results.md](live-results.md).
