# Results 01 — probe run in Dorico 6 (2026-10-30)

Probe library `probe/Fiddle-Orchestral-UnpitchedProbe-r1.doricolib`, score
`unpitched-probe.musicxml` (buzz roll written as `<tremolo type="unmeasured">0</tremolo>`).

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

## MusicXML import (for the score-test writer)

- Both parts arrive as Snare Drum (from `<instrument-sound>drum.snare-drum`),
  named from the part names.
- Kept: the dynamics ladder, the hairpin, accent, marcato, the three-stroke
  tremolo (bar 5 whole note, bar 6 beat 3) as a roll, the x notehead (bar 7
  beat 3).
- Dropped: `<tremolo type="unmeasured">0</tremolo>` (the buzz roll, bar 6
  beat 1) and the parenthesized notehead (ghost note, bar 4 beat 3). Score
  specs must list these as manual techniques.

## Verdicts

_Pending the capture._
