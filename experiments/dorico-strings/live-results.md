# Strings F r6: live input and server replay, 2026-09-22

> **Retired 2026-10-19.** The experimental decoder, its environment-variable
> opt-in and `ExperimentalBridgeTest` replay tool were removed in favour of
> compiled bridge maps. The commands below need a checkout of commit `d93b90b`
> or earlier.

> **Retained chronological evidence.** Intermediate “next test,” installation,
> and remaining-gate statements below belong to their recorded stage; later
> entries document subsequent results. They are not today's work queue or a
> fresh inspection of installed state. As of 2026-09-24, the active design is
> [Orchestral bridge maps](../../docs/orchestral-bridge-maps.md). Successful
> independent-conduit add-ons remain evidence for that plan; the automatic
> restoration failure remains a limitation, not proof that add-ons fail generally.

The two closed Dorico probe captures passed technique reconstruction:

| Capture | Expected notes | Exact PT sets | Missing state | Dropped inputs/capture records |
| --- | --- | --- | --- | --- |
| From beginning | 1–16 | 16/16 | none | none |
| Fresh activation, start at note 12 | 12–16 | 5/5 | none | none |

Both contain one idle audition note, excluded from the playback comparison.
The second capture contains only one transport playback pass: no earlier
playthrough supplied its state. All four note-12 keyswitch commands precede the
musical NoteOn in Dorico's event list, at the same sample offset. Subsequent
resets/reassertions and the final non-vibrato → vibrato replacement are correct.

The first note's base CC and NoteOn have the same offset. The adapter's explicit
parameters-before-events policy yields Medium length (unlike the exported MIDI
file's event order). Cross-list ties remain policy-dependent, not evidence of
a total host ordering. The first capture includes negative preroll; the first
musical event is at signed sample -1, saturated to 0 on the current unsigned
wire. Neither capture is unconditional timing acceptance.

## Evidence and replay

Raw copies are retained locally in `local-captures/` (gitignored; not uploaded):

- `live-r6-start.jsonl`: SHA-256 `6737ab64ca521c198848cb10a0c628af77353b55532b74a36db2973a42a9c6e9`
- `live-r6-note12.jsonl`: SHA-256 `e19f3e09d233699e141fb4937bb07ef29b90fe44ee363ccb422d80e8f5b7a72e`

From the repository root:

```sh
python3 experiments/dorico-strings/inspect_live.py experiments/dorico-strings/local-captures/live-r6-start.jsonl
python3 experiments/dorico-strings/inspect_live.py experiments/dorico-strings/local-captures/live-r6-note12.jsonl --from-note 12
build/release/ExperimentalBridgeTest experiments/dorico-strings/local-captures/live-r6-start.jsonl 1
build/release/ExperimentalBridgeTest experiments/dorico-strings/local-captures/live-r6-note12.jsonl 12
```

The Python checks intentionally return nonzero for the timing cautions above.
The C++ replay checks semantics, using the native relay's real event-to-protobuf
conversion, protobuf serialization/parsing, `NoteStreamTracker`, and two separate
`ExpressionMapAnnotator` instances with deterministic downstream mappings. Both
captures yield the expected output selection on each layer. It does **not** open
a TCP connection or load a vendor VSTi.

`experimental_bridge` tests every defined scalar/base value, exact canonical IDs,
compound base techniques, missing/invalid state, physical releases, cancellation,
transport reset, and independent port/channel state. `experimental_bridge_mixer`
exercises the production mixer, MIDI scheduler and two hosted deterministic
instruments, checking actual MIDI delivery and rendered samples. This separates
decoder/resolver proof from vendor-instrument and live scheduling acceptance.

Validation for this change: six targeted CTest suites passed (native input,
expression maps, transport, experimental bridge, realtime queues, experimental
bridge mixer), as did the full `mixer_integration` test and all 43 Python
experiment tests. Both raw captures pass the C++ replay. `FiddleServer` builds.
The historical send-alignment failures on the backlog did not reproduce in
this mixer run; that alone is not a diagnosis or a claimed fix.

## Runtime boundary

The experimental decoder is opt-in, session-only, and limited to Strings F r6.
Normal launches retain the bundled legacy decoder. No project/library schema,
installed expression map, or user project configuration was changed. See the
installation status under Remaining acceptance below.

The built server supports these **three explicit environment variables** for a
controlled session (not enabled automatically):

- `FIDDLE_EXPERIMENTAL_BRIDGE`: absolute path to `generated/high-vibrato.decoder.json`.
- `FIDDLE_EXPERIMENTAL_BRIDGE_PORT`: zero-based port, 0–15.
- `FIDDLE_EXPERIMENTAL_BRIDGE_CHANNEL`: one-based channel, 1–16.

**Live routing differs from the probe:** the production server reserves port 0,
channel 1 (Dorico Port 1, Channel 1) for the metronome and intercepts it before
the note tracker. Use port **0**, channel **2** for the live acceptance session;
this matches the existing three Violin I layers (mixer inputChannel 1 is
zero-based). In the disposable Dorico score select **Port 1, Channel 2**.
Do not reassign the layers, and do not use the probe's old Channel 1 routing.

Malformed requested configuration stops initialization before creating the MIDI
listener. With a valid opt-in, only that endpoint changes decoder; every layer
on it receives the same original technique set. Reserved keys and their physical
releases are consumed before musical-note callbacks. Invalid values invalidate
their dimension; notes with incomplete state are withheld and counted/logged.
No late-CC guessing/jitter enrichment is applied to experimental notes.

STOP clears transient state. START preserves preparation sent before transport.
The existing legacy bridge also gains port-and-channel-local CC state and uses
host timestamps consistently for automation/enrichment rather than mixing host
and wall clocks. Its protocol tables and normal note behavior remain legacy.
The downstream annotator also preserves the incoming written-length category at
note-off; previously, shortened playback could choose another length-conditioned
switch's release actions. A regression test fails before that fix and passes
afterward; duration remains the fallback when no category was supplied.

The generated manifest's `runtime_compatible: false` remains deliberate: it is
not automatically compatible with normal production launches. The existing
reachability/repair UI still audits the legacy map, not this session override.
Environment variables are a laboratory affordance, not the final profile UI.

## Remaining acceptance

### First live vendor-instrument pass

The user reports that all 16 notes play after increasing the ordinary Fiddle
plug-in's exposed channel count (its initial configuration was one port and one
channel). For this test, **one port and at least two channels** are required;
the score must use Port 1, Channel 2. Include this step in future setup guidance.

After playback, MCP reports the original Main branch/version, clean and stopped,
with all three Violin I players loaded and still routed to Strings. The live
Note Inspector visibly contains the note-12 four-technique combination
(`pt.nonVibrato`, `pt.flautando`, `pt.overpressure`, `pt.fouette`), subsequent
reduced combinations, and final `pt.vibrato`, all with Medium written length.
This is live downstream evidence, not just offline replay. A complete automated
16-note comparison of the server's inspector records has not been collected.

The inspected layer also displays unmatched incoming techniques and Natural
(Long) fallback for some combinations; the final vibrato resolves to Poco
Vibrato. Do not interpret audible notes as proof of every requested downstream
articulation. Identify the inspected assignment and distinguish genuinely
unsupported techniques from canonical-ID/alias/resolver gaps before declaring
full musical acceptance. The inspector was left open; no layer settings or
expression-map assignments were edited during this check.

### Downstream fallback investigation and fixes

Read-only inspection of the assigned map IDs and installed Art Conductor XML,
followed by offline resolution of the 16 expected PT sets at Medium length,
explains the observed vibrato fallbacks:

| Technique | Duality 1st Violins | Elite 1st Violins | Solo Violin |
| --- | --- | --- | --- |
| `pt.nonVibrato` | No switch; Natural fallback | NV | NV |
| `pt.vibrato` | Poco Vibrato | No switch; Natural fallback | Poco Vibrato |
| Flautando, overpressure, fouette | No entries | No entries | No entries |

These are map-coverage findings, not claims about available instrument samples.
The inspected Natural/Poco Vibrato results and keyswitch pair 25/41 are
consistent with Duality. The offline checks do not replace a complete capture
of all three live layers. “Unmatched” describes articulation-switch coverage;
timing treatment such as tenuto can still apply separately.

Two independent resolver/parser bugs were reproduced and fixed:

- A natural fallback now retains applicable add-ons. With the installed
  Duality map, `pt.muted` alone now resolves to Natural [Long] + Con Sordino,
  just as staccato + mute already retained that add-on.
- Note-length comparisons now preserve individual operands and support AND/OR
  with AND precedence. `NoteLength <= kShort AND NoteLength > kVeryShort`
  now selects Natural [Staccato] for Short notes in Duality and Solo Violin,
  instead of leaving them without a matching base. Malformed length conditions
  fail closed; an empty-PT fallback cannot bypass its condition. Non-length
  conditions retain their previous behavior; this is not a general Dorico
  expression-language interpreter (parentheses are not supported).

Self-contained XML/annotator regressions cover implicit/explicit natural,
unsupported extra PTs, MIDI emission, all five length buckets, boundary values,
AND/OR ordering and precedence, and invalid conditions. The new regressions
failed before the fixes. No vendor files, live assignments, or running sessions
were changed, and no alias substitutions were added. The rebuilt server needs
a controlled restart with the existing experimental environment to activate
the fixes for live r6 testing; no new Dorico plug-in installation is needed.

That controlled restart has now been completed, preserving the r6 environment
on port 0/channel 2 and restoring Main to the same saved version. All 39 layer
assignments/settings matched the pre-restart MCP snapshot. The user replayed
the original passage, then a fresh four-note flow at quarter = 120. Direct
inspection of the latest Duality-shaped annotation records confirms:

| Note | Incoming PTs / length | Selected base | Add-ons |
| --- | --- | --- | --- |
| Unmarked quarter | none / Medium | Natural [Long] | none |
| Unmarked eighth | none / Short | Natural [Staccato] | none |
| Con sord. quarter | `pt.muted` / Medium | Natural [Long] | Con Sordino |
| Continuing con sord. eighth | `pt.muted` / Short | Natural [Staccato] | Con Sordino |

Both muted records display the mute keyswitch (B0 / MIDI 23) alongside the
appropriate base-switch actions, with no unmatched PT warning. This passes
the focused live annotation/MIDI-emission smoke test for both fixes; it is not
an independent audio capture or a comparison of all three vendor layers.
Afterward Fiddle reports ready, clean, stopped, and the original Main version.

Live-test preparation: the rebuilt ordinary Fiddle VST3 has now been installed
with Dorico closed and its signature/content verified. The prior bundle is
retained in `build/backups/native-before-live-r6-20260922/Fiddle.vst3`; a consistent
SQLite backup and audio-device settings are alongside it. No experimental
server launch or live acceptance is implied by installation.

The controlled server session has subsequently been launched with the above
port-0/channel-2 opt-in. Before playback, MCP confirmed ready, clean, 39 loaded
layers, and the original Main branch/version
`82731153-7cc5-663d-84b5-d6d310c563f2` /
`45efcd85-8b35-8012-1f87-7eb21f94cefb`. All three Violin I layers retain their
original maps, gains, and Strings bus destination; five group buses are present.
Session output is in `build/backups/native-before-live-r6-20260922/live-r6-server-2.log`.

Startup recovery note: a UI lookup by app name unintentionally opened the older
`/Applications/FiddleServer.app`; that extra instance was terminated. A clean
restart of the explicit build path cleared a subsequent database-lock failure.
The app initially selected existing branch B1, so Main was explicitly restored
through the branch selector. Database quick-check passed; branch heads and the
237-version history matched the pre-test backup. No database rollback was used.
The app bundle's deep signing check also encountered a pre-existing packaging
issue (Python scripts under Contents/MacOS); the VST3 signature verified, and the
locally built app launched, but do not claim a distribution-ready signed app.

Launch the rebuilt server with the explicit opt-in, then exercise a
disposable score/chair through a real hosted library. Check full/mid-passage
starts, repeated playback, actual sound and Note Inspector/MIDI capture, and
unaffected legacy endpoints. TCP/render-ahead/vendor behavior is still unproven
for this profile. Do not route the r6 map into an ordinary server launch.

Broader tests still needed: scalar con-vibrato/wide-vibrato and temporary
restoration in Dorico, simultaneous/chord notes, multiple endpoints, seeking,
and other instrument-family reservations. The two captures do not prove those.

The next bounded live test is prepared in [reliability.md](reliability.md): a
separate seven-bar MusicXML scaffold, per-note expected states, and five
playback passes for chords, remaining vibrato values, duration-limited
restoration, repeated playback, and stopped seeking. It uses the unchanged r6
map and current opt-in endpoint. The scaffold requires actual PT setup in
Dorico; preparation and 49 passing Python tests are not live acceptance.

First reliability playback: **not accepted yet; score setup differs from the
checklist.** Live inspector samples show C5/E5/G5 chord tones with `pt.vibrato`
and Medium length. The final three records still contain flautando + vibrato,
and three successive records in the temporary non-vibrato section retain
non-vibrato. Duration/restoration is therefore not proven.
Read-only selection of the actual notation in Dorico identifies the markings
at bar 2, bar 3, and bar 4 as **Vibrato [pt.vibrato]** in all three cases, rather
than the requested nonVibrato/conVibrato/wideVibrato. Correct those identities
and verify explicit durations for bar 5 and bar 7 before replaying or diagnosing
a decoder failure. No score edits were made during this inspection.

Follow-up score correction (partial): saved a separate project copy at
`generated/reliability-r6-corrected.dorico`, preserving the original. Bar 2 now
reports `Non Vibrato [pt.nonVibrato]`; the marking at bar 4 reports
`Fiddle test wide vibrato (Wide vibrato [pt.wideVibrato])`. Two project-local
notation definitions were added for con/wide vibrato, mapped to the existing
playback techniques; no Save as Default or playback-definition changes were
made. Bar 3 still reports ordinary `pt.vibrato`: the new con-vibrato popover
shortcut did not resolve, and an unverified alternative was cancelled rather
than committed. UI automation cannot select entries in the notation list
reliably (AXError.notImplemented); manual selection is needed to inspect the
saved con-vibrato definition/shortcut. Bars 5/7 durations remain unchanged.
The Wide vibrato playback-definition dialog shows Attribute, so its coverage
of all four bar-4 notes also needs explicit setup/verification. This copy is
not ready for acceptance playback; do not treat these partial edits as a pass.

The user subsequently selected the con-vibrato definition manually. Its saved
dialog visibly confirms popover text `vibrato22` and playback technique
`Con vibrato`; the earlier `ftcon` edit was not retained. No further score edits
were made: coordinate clicks now fail with `windowNotFoundAtPosition`, including
after raising/zooming the score window and resetting the UI connection. The
dialog is closed and the score window is enlarged. Awaiting manual selection
of bar 3's existing `vib.` marking to continue with keyboard controls.

Score correction completed after that manual selection. Keyboard entry of the
verified shortcut changed bar 3 to `Con vibrato [pt.conVibrato]`. Wide vibrato
now has an explicit 2.0-second span across bar 4. Bar-5 non-vibrato and bar-7
flautando each have a 1.0-second span from beat 2 to the beat-4 boundary;
their selected duration handles were visually checked against the noteheads.
Saved `generated/reliability-r6-corrected.dorico`. The original project, playback
definitions, expression map, and decoder were not changed. The scaffold's
wide-vibrato instruction is corrected to whole-bar duration rather than
persistence. Ready for the first corrected full-playback pass, not yet live
acceptance: attribute coverage and background-technique restoration still
need observation in Fiddle.

First corrected full playback: **partial pass; scalar restoration fails the
desired expectation.** Read-only inspection of the latest Note Inspector
history shows non-vibrato (`vibrato: 2`), con-vibrato (`64`), and wide-vibrato
(`127`) reaching Fiddle as their distinct canonical PTs. The four successive
wide-vibrato records cover bar 4. Chord samples show C5/E5/G5 with `pt.vibrato`
and Medium length; this visual inspection is not a complete 38-record export.

At the bar-5 boundary, reverse-chronological records are: no PTs / vibrato
scalar 1 (beat 4), non-vibrato / scalar 2 (beats 3 and 2), and vibrato / scalar
126 (beat 1). The temporary non-vibrato ends, but the prior vibrato is not
restored in Fiddle's recorded state. The profile defines scalar 1 as unspecified;
this is consistent with a Switch Off without a successful reassertion, but
the inspector alone does not establish the exact incoming MIDI ordering or
whether Dorico omitted a reassertion. Obtain a MIDI export of the corrected
flow before attributing the cause or altering the decoder.

At bar 7, the newest four records (reverse order) are vibrato only, flautando
+ vibrato, flautando + vibrato, vibrato only. This passes the intended binary
flautando duration/cancellation boundary. Vendor-map unmatched warnings for
some received PTs are separate from the bridge-transport checks. Fiddle remains
ready, clean, stopped, 39 layers, and on the original Main branch/version.
No score, expression-map, decoder, routing, or mixer changes during this check.

The corrected MIDI export now corroborates that boundary: at tick 9109 it
sends note 127/velocity 1 (unspecified), followed by CC102=2 and the bar-5
beat-4 note, without a vibrato reassertion. Bar 7's flautando cancellation does
include 127/126 before the next musical note. All 38 pitches/onsets are present;
37/38 PT expectations match, with only bar 5 beat 4 differing. Separately, the
export splits initial CCs onto channel 2 while music/protocol notes use channel
1, and places the first CC102=2 after the first musical note. Do not call this
an initialization pass or silently merge channels. Exact event evidence and
limits are recorded in [results/reliability-r6-corrected.md](results/reliability-r6-corrected.md).
Recommended next test uses an explicit vibrato return at bar 5 beat 4; no score
or decoder change has been made as part of the export inspection.

After the user added explicit vibrato at bar 5 beat 4 and replayed the full
passage, the latest live Note Inspector records confirm the corrected bar-5
sequence: vibrato / non-vibrato / non-vibrato / vibrato (scalar values
126 / 2 / 2 / 126). The return record includes emitted NoteVel 25 and NoteVel
41, rather than the previous unspecified state. The final bar still ends
with vibrato alone after two flautando + vibrato notes. This passes the focused
live explicit-return check, not automatic restoration or a fresh 38-note MIDI
export comparison. The earlier MIDI export remains evidence of the no-explicit-
return score. MCP confirms ready, clean, stopped, 39 layers, original Main
branch/version. Next: repeat from the start after STOP, then mid-passage starts
and backwards seek. No runtime/map/mixer edits or inspector clearing performed.

Repeat-from-start after STOP: focused live inspection passes. The opening
Medium-length C5 records have no PTs, scalar vibrato 1, and binary flautando
OFF (2), with other displayed dimensions initialized; opening chord samples
also have no PTs. An older Long/velocity-64 audition record is outside this
playback and was not counted. Bar 5 again shows the explicit vibrato return
(126) after two non-vibrato records (2), and the latest final-bar records show
vibrato alone following the two flautando + vibrato notes. No stale vibrato or
flautando was observed at the repeated opening. This is targeted inspector
verification, not a new complete MIDI capture. Next: start directly at bar 2
beat 3, expecting non-vibrato immediately, then play through the end.

Start directly at bar 2 beat 3: focused live inspection passes. At the start
of the newest passage, the two Medium-length C5 records both show
`pt.nonVibrato` / scalar 2, followed chronologically by con-vibrato / scalar 64.
The preceding Long/velocity-64 C5 audition record has no PTs and is distinct
from these playback notes; older records below it belong to the prior pass.
The bar-5 explicit vibrato return and final flautando cancellation still show
the expected states. This verifies the observed mid-start technique state,
not a complete new event capture or an independent audio assessment. Next:
start directly at bar 5 beat 3 (inside the temporary non-vibrato span), play
to the end, and check non-vibrato immediately followed by explicit vibrato.

Start directly at bar 5 beat 3: focused live inspection passes. The first
Medium-length C5 record has `pt.nonVibrato` / scalar 2; the next chronological
record has `pt.vibrato` / scalar 126 and emitted NoteVel 25/41. The preceding
Long/velocity-64 audition record is distinct and was excluded. The latest
ending still shows two flautando + vibrato notes followed by vibrato alone.
MCP remains ready, clean, stopped, 39 layers, original Main branch/version.
Next and final pass in this bounded checklist: play from bar 4, stop while
wide vibrato is active, then seek backwards to bar 2 beat 3 and play to the end.
Check that the restarted passage begins with non-vibrato, not stale wide vibrato.

Backwards-seek attempt: the user noted a possible overrun into bar 5. The
inspector confirms that the interrupted segment reached a Medium non-vibrato
note after a vibrato note, preceded by wide-vibrato records (consistent with
bar 5 beat 2). A Long/velocity-64 non-vibrato audition separates this from the
two Medium non-vibrato notes at the newest restarted passage's opening. The
restart and final flautando boundary show the expected PTs, but this attempt
does not establish the intended wide-vibrato-to-non-vibrato stale-state check:
the last recorded musical state before seeking was already non-vibrato.
Repeat only this last pass, stopping just after bar 4's first note before
seeking to bar 2 beat 3. Inspector records show notes, not an exact transport-
stop timestamp, so do not claim the precise instant of the stop was captured.

Repeated backwards-seek test: focused live inspection passes. The interrupted
segment's last Medium-length records show wide vibrato / scalar 127. The
intervening Long/velocity-64 audition also retains wide vibrato. The newest
restarted passage then begins with two Medium C5 notes carrying non-vibrato /
scalar 2, followed by con-vibrato. Thus the observed restarted notes do not
inherit wide vibrato, even though it was still present at the preceding audition.
The latest final-bar records retain the expected flautando cancellation and
vibrato-only final note. MCP remains ready, clean, stopped, 39 layers, original
Main branch/version.

This completes the bounded live reliability checklist with the explicit bar-5
vibrato return: targeted full/repeat, persistent-direction mid-start,
temporary-span mid-start, and interrupted backwards-seek checks passed.
Automatic restoration without an explicit marking remains a failed hypothesis
for this score/map, corroborated by the preserved MIDI export. Do not promote
these focused single-endpoint observations to full-profile, fresh-process,
multi-endpoint, all-vendor, or distribution-readiness acceptance. No more manual
playback passes are requested for this round; no runtime/map/mixer edits made.
