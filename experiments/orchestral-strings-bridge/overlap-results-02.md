# Strings r2 — fresh overlap capture and test-scope correction

2026-09-24. **Retire this fixture as a bridge acceptance test.** Preserve the
score, original expected JSON, and both captures as diagnostic history; do not
ask for another unchanged repeat. No map, fixture, decoder, or plugin changes
are made on the strength of this result.

## Why the original expectations are not a valid oracle

The user correctly identified a shared-channel constraint. Dorico documents
that instrument voices share an endpoint by default and recommends independent
voice playback for simultaneous different techniques in different voices.
[Independent voice playback](https://www.steinberg.help/r/dorico-pro/6.2/en/dorico/topics/play_mode/play_mode_tracks_instruments_independent_voice_playback_enabling_t.html),
[Playback techniques](https://www.steinberg.help/r/dorico-pro/6.2/en/dorico/topics/library/library_playback_techniques_c.html).

Although the guide disclaimed independent sustained timbres, its expected
note-start sets still assumed an unverified resolution of PTs and Natural
across two voices sharing a channel. That confounds the narrower question of
command ordering around overlapping note lifetimes. Neither capture proves a
bridge-map defect, and the exact source of the observed clears is unresolved.

Shared CC state is not the same as every MIDI property being channel-wide:
velocity is per note, and successive note starts can observe different state.
Fiddle can snapshot the state actually received at each note-on; it cannot
recover independent voice intent absent from the MIDI stream, or make shared
controllers independently affect held VSTi notes. Independent voice intent
needs distinct routed channels/endpoints, not merely different notated voices.

## Evidence

- Original: `/var/folders/3k/4c2wqkxn5p50ldsh3glsd78r0000gn/T/fiddle-midi-probe-1790298964849593-2/capture.jsonl`.
- Preserved copy: `local-captures/overlap-r2-pass2.jsonl` (git-ignored).
- SHA256: `6802e31a6d992f75d51d62f76531648ded13f59ad32303c43aa793efa29aba31`.
- Strict reader accepts the closed schema-1 capture: 4,508 blocks, no capture
  drops, missing records, or invalid/dropped adapter input.
- One playing pass, blocks 2384–3707 inclusive: all 17 expected musical pitches
  on Port 1 / Channel 1. One idle audition is excluded. All musical releases
  match, with no duplicate active pitch or dangling note.
- All 49 add-on dimensions are initialized before playback; base state is
  known at every note. Nineteen negative preroll blocks are legitimate, with
  no backwards host clock in the pass.
- Low C really overlaps D/E but ends before F in bars 2 and 4. Thus the note
  geometry itself was valid, even though the PT expectations were not established.

## Observations, not acceptance failures

Times are host samples at 44.1 kHz. Equal-offset ordering across parameter and
event lists uses the capture adapter's parameter-before-event policy; it is
not proof of a finer ordering inside Dorico.

- **Bar 1:** Flautando on C; Overpressure On (CC112=2) at 32369 followed by
  CC112=0 at 33075, exactly D's onset. D/E/F consequently reconstruct without
  Overpressure. This set-then-clear reproduces the first run after reopening.
- **Bar 2:** Flautando on C, Overpressure on D/E/F. C's release at 230205
  does not clear Overpressure before F at 231039.
- **Bar 3:** Strum Up is now present on C (it was absent in the first run),
  followed by Strum Down on D/E/F. Overpressure accompanies all four notes:
  it is explicitly reasserted at 263894 with Strum Up despite the intended
  Natural cancellation.
- **Bar 4:** Strum Up on C, Strum Down on D/E/F, again with Overpressure.
  C's release at 495150 does not reset the KS state before F at 496301.
- **Bar 5:** the final C still has Overpressure, explicitly reasserted at
  528494 before that note's onset at 529464.

Only 5/17 sets match the original JSON, but that count is historical comparison,
not a meaningful pass/fail score now. Restarting did not remove the control-bar
clear or retained pressure; it therefore was not the complete explanation.
These facts alone do not distinguish voice scope, score activation, or host
switch scheduling as causes.

## Next valid boundary

The prior passing 16-note CC and 12-note KS tests retain their bounded results.
A replacement overlap test should use an unambiguous shared PT timeline and
verify Dorico's active PTs before assigning expected MIDI states. Independent
voice playback on distinct channels would be a separate routing/isolation test.
Neither replacement is prepared here. Synthetic decoder tests can check
snapshot/release semantics, but cannot substitute for host evidence.
