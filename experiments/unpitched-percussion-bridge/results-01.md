# Results 01 — probe run in Dorico 6 (2026-10-31)

Probe library `probe/Fiddle-Orchestral-UnpitchedProbe-r1.doricolib`, score
`unpitched-probe.musicxml` (buzz roll written as `<tremolo type="unmeasured">0</tremolo>`),
manual marks per the README. MIDI Probe capture `fiddle-midi-probe-1793466911943412-2`
(10.4 MB, kept locally as `local-captures/capture-01.jsonl`; the condensed
timeline is `local-captures/events-01.txt`). Checker output follows the
findings.

## U0 — mixed library: YES, when installed

- Library ▸ Import Library… has one entry point per entity kind, and each
  imports only its own kind from the file: imported as an expression map the
  percussion maps are dropped, imported as a percussion map the expression
  map is dropped.
- Copied into `DefaultLibraryAdditions/` (Dorico closed, then started), the
  one file yields all three: Library ▸ Expression Maps lists "Fiddle Bridge -
  Unpitched Percussion Probe r1", Library ▸ Percussion Maps lists "Fiddle
  Unpitched Probe A" and "B". That is how Fiddle installs bridge maps, so
  design decision 1 (one `.doricolib` per section) stands.
- Map A (`forSingleInstrument` true) was offered for a single Snare Drum in
  Endpoint Setup. r1 is regenerated with `forSingleInstrument` true.

## MusicXML import (for the score-test writer)

- Both parts arrive as Snare Drum (from `<instrument-sound>drum.snare-drum`),
  named from the part names, on a five-line staff. Selecting a note also
  highlights the instrument label "Sn. Dr. 1"; Edit ▸ Filter ▸ Notes and
  Chords drops it from the selection so note properties are editable.
- Kept: the dynamics ladder, the hairpin, accent, marcato, the three-stroke
  tremolo (bar 5 whole note, bar 6 beat 3), the x notehead (bar 7 beat 3).
- Dropped: `<tremolo type="unmeasured">0</tremolo>` (the buzz roll, bar 6
  beat 1) and the parenthesized notehead (ghost note, bar 4 beat 3). Score
  specs must list these as manual techniques.

## Verdicts

| | Verdict | Evidence |
| --- | --- | --- |
| U1 | **A1** — both maps see every technique | bar 4 accent: pitch 70 (the diagnostic accent entry) *and* CC104=1; marcato CC104=2; bar 7 dead stroke (no entry): pitch 60 *and* CC108=1; bar 6 beat 3 rim+tremolo: CC102=10 (tremolo base) *and* CC106=3 (rim add-on) *and* pitch 61 (rim entry) |
| U2 | **A SUSTAINED; B FALLBACK-STROKES** | map A bar 5: one note at 65, 2400 ms, velocity 56, CC1 56→72 across the hairpin (272 values, ~every 150 ms); map B: 24 notes at 60, 100 ms each (six per beat), velocities tracking the hairpin 56→72, and the same CC1 ramp |
| U3 | **EXACT-COMBO when the combination has an entry; BEST-SUBSET otherwise; unmatched → Natural** | bar 9 rods+rim → 69 (the `pt.beaterRods+pt.rim` entry); bar 6 beat 3 rim+tremolo (no entry; the `pt.rim+pt.roll` entry never applied, see below) → 61 (rim), played as ten 100 ms strokes; bar 7 dead stroke → 60 with CC108=1, not dropped |
| U4 | **SENT, per note, 0 ms lead** | KS15 note-on at exactly each hit's time, velocity 127, note-off with the hit's (599.8 ms); in stream order the keyswitch precedes the hit |
| U5 | **beater BOTH; snares ADDON** | "Rods": CC105=2 and every later hit at 68 (bars 8, 10, 11) or 69 (bar 9, with rim) — the direction stays in force to the end; "Snares off/on": CC112=2 then 1, pitch unchanged (map A has no snare entry) |
| U6 | **EXACT; no beat stress; no CC1 on hits** | ladder velocities 40 48 56 60 68 72 80 88; bar 3 eighths all 68; no CC1 in bars 1–4 |

### Findings the plan did not anticipate

1. **A written three-stroke tremolo is `pt.tremolo`, not `pt.roll`.** Bar 5
   selected base 10 (tremolo) and entry 65 (tremolo). The `pt.rim+pt.roll`
   combination entry was therefore never a candidate. What produces
   `pt.roll` in Dorico is still unknown; r1 keeps both codes and both bases.
   `scoretest.py`'s tremolo token expects `pt.tremolo`.
2. **Sustain follows the chosen entry.** A tremolo whose chosen entry carries
   the tremolo technique (65) is one sustained note; otherwise (rim 61 by
   best subset; natural 60 on map B) Dorico plays repeated 100 ms strokes at
   that pitch. In both shapes the expression map still selects the tremolo
   base and sends the CC1 ramp, and each stroke's velocity follows the
   dynamic too. So Fiddle always learns the roll kind from CC102 and the
   surface from the pitch; only the note shape differs.
3. **NoteLength for a tremolo is the stroke, not the written note:** the
   whole-note tremolo took the kVeryShort row (code 10), while the half-note
   buzz roll took its written length (code 18, kLong). Harmless for r1
   (every roll base has all five rows) but the decoder must not infer the
   written length from a roll base's bucket.
4. **The ghost note sent nothing**: no KS4, velocity 68 like its neighbours.
   The saved project (`Unpitched percussion probe.dorico`) holds a
   user-made *playback* technique `pt.user.ghost`, also named "Ghost note",
   beside the built-in `pt.ghostNote`; the ghost playing technique
   (`pta.user.ghost`) most likely resolves to that copy, which no map knows.
   To be re-run after pointing it at the built-in technique. Dorico also has
   a `ghostNoteDynamicDelta` playback option (pinned to 0 by the map), so a
   ghost note is at least partly a dynamics matter in Dorico.
5. **The x notehead imports as side stick.** The note played 62, and in the
   project that note alone carries `kEventPlayingTechnique = pt.sideStick`
   (the mark on the previous note was a text playing technique, which does
   not set the note property). `scoretest.py`'s `xhead` token stays
   expectation-free, since this is one instrument's notehead set.
6. **Add-on changes are Off-all-then-On-all**: at bar 9 (rods → rods+rim)
   CC105 66 then CC105 2 + CC106 3; at bar 11 (rods+snaresOff →
   rods+snaresOn) CC105 66, CC112 66, then CC105 2, CC112 1. At the end of
   playback every add-on is switched Off (CC105 66, CC112 65); CC102 is not
   reset. Switch timing as in the pitched maps: Off ≈ 25 ms and On/base
   12.5 ms (10 ticks at ♩ = 100) before the note.
7. **CC102 is sent only on change** (2 at start, 1 for the eighths, 2 back,
   10/18/10 for the rolls, 2 after); the decoder's base state carries over.

### Branch table outcome

Target design as written: hit bases by velocity, roll-family bases on CC1,
strokes as pitch codes, modifiers as add-ons, no combination codes in r1.
Changes to r1 from this run: `forSingleInstrument` true. Open for r2: the
entry keyswitch as a conduit (U4: simultaneous with the note, so Fiddle
would pair by time), and stroke × roll combinations in 90–114 if sustained
rolls on a surface turn out to matter for playback.

## Checker output

```
U1 technique routing: A1
  bar 4 accent: pitch 70, CC104 1; marcato: CC104 2; ghost: KS4 velocity None
  bar 7 dead stroke: pitch 60, CC108 1
  bar 5 roll: CC102 10 before the note (tremolo base - a roll-family base)
U2 rolls: A SUSTAINED; B FALLBACK-STROKES
  map A bar 5 roll: 1 note-on(s); longest pitch 65 velocity 56 length 2400 ms; CC1 (0.0, 56)..(2362.5, 72) (272 values)
  map B bar 5 roll (no entry): 24 note-on(s); longest pitch 60 velocity 57 length 100 ms; CC1 (0.0, 56)..(2375.0, 72) (272 values)
U3 combinations and fallback: BEST-SUBSET (rim wins) - the rim+roll entry never applied; EXACT-COMBO shown by bar 9 rods+rim; the combination's roll played as STROKES; unmatched technique falls back to Natural
  bar 5 written roll resolves to pt.tremolo
  bar 6 buzz roll: pitch 66
  bar 6 rim+tremolo: pitch 61, 10 note-on(s) over the half note
  bar 7 x notehead: pitch 62 (63 = imported as cross stick)
  bar 7 dead stroke (no entry): pitch 60
U4 entry keyswitch: SENT x4 (per note)
  KS15 at 16800.0 ms: velocity 127, length 599.8 ms, 0.0 ms before the next hit  (x4)
U5 direction techniques: beater BOTH (bar 9 rods+rim -> rods+rim entry); snares ADDON
  bars 8-9 Rods: CC105 values [2, 66, 2, 66, 2]; pitches bar 8 [68, 68, 68, 68], bar 9 (rim) [69, 69, 69, 69]
  bars 10-11 Snares off/on: CC112 values [2, 66, 1, 65]; pitches [68 x8]
U6 velocity dynamics: EXACT; NO-BEAT-STRESS; no CC1 on hits
  ladder: 40 48 56 60 68 72 80 88 (all as expected), CC1 None; bar 3 eighths all 68
```
