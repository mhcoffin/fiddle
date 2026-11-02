# Dynamics measurement — results 01 (2026-10-20)

One MIDI Probe capture, Dorico 6, four complete passes (A–D in
[README.md](README.md)), no dropped or invalid input. Override formats came from
`dynamics-overrides-export.doricolib`. Reproduce with
`python3 experiments/dynamics-curve/analyze.py <capture.jsonl>`.

## Findings

1. **Dorico sends whole MIDI values.** Every CC1 and velocity is a multiple
   of 1/127: Dorico rounds before sending; there is no extra precision to
   recover.
2. **Levels.** The p family counts down from −1 (p −1, pp −2, … pppppp −6),
   the f family up from +1 (f 1, … ffffff 6); mp is −0.5 and mf +0.5.
3. **The curve is an S-curve, not a plain power.** With
   `x = clamp((level − min) / (max − min), 0, 1)` and power `p`:

   ```
   y = 0.5 · (2x)^p            for x < 0.5
   y = 1 − 0.5 · (2(1 − x))^p  for x ≥ 0.5
   MIDI = round(1 + 126 · y)     (1..127)
   ```

   This reproduces 55 of 56 ladder values exactly, for CC1 and for velocity.
   The miss is an exact half-way value (pass B fffff: 116.5, sent as 116, while
   the other .5 values round up), i.e. Dorico's floating-point rounding. Plain
   `x^p` and `x^(1/p)` match only 31 of 56. Power 1 is linear.
   The map option `dynamicsOptions.dynamicCurveParams` is
   `"midpoint,lowerPower,upperPower"`: the exported 1.75 curve was
   `"0.500000,1.750000,1.750000"`, and Iconica Sketch's bundled `"0.5,1,1"` is linear.
4. **Minimum/maximum dynamic level clip; they don't rescale outside the
   range.** In pass C (−3..3), everything at or below ppp sent 1 and everything
   at or above fff sent 127.
5. **Velocity.** With dynamics on CC1 (volume type CC1, attack type note
   velocity), Dorico sent velocity **100 on every note**. With dynamics on
   velocity, velocity follows the same curve as CC1.
6. **Articulations add to the note's level** before the curve, for that note
   only, on CC1 and velocity alike. Measured from the fitted curve: pass A
   accent +0.51 and marcato +0.82 (set 0.5/0.8), pass D accent +0.99, marcato
   +2.04, tenuto +1.50 (set 1.0/2.0/1.5); tenuto 0 changes nothing.
7. **Timing.** At a dynamic change, the new CC1 arrives 0–7 ms **after** the
   note-on it belongs to, not before.
8. **Crescendo.** Over a held note CC1 moved one code at a time (p→f: 41 to
   87 in 47 steps at power 2.5; 54 to 74 in 21 steps at power 1).
9. **Resolution.** At power 1 over −6..6 there are 10.5 codes per level and
   adjacent written dynamics are at least 5 codes apart. At power 2.5 the
   extremes crowd together (pppppp and ppppp are 1 and 2).

## Override formats as Dorico writes them

| Option | Value as exported |
| --- | --- |
| `dynamicsOptions.dynamicCurveParams` | `string: "0.500000,1.750000,1.750000"` |
| `dynamicsOptions.minProfileLevelForPlayback` | `int: -5` |
| `dynamicsOptions.maxProfileLevelForPlayback` | `int: 5` |
| `dynamicsOptions.accentDynamicDelta` | `double: 0.6` |
| `dynamicsOptions.marcatoDynamicDelta` | `double: 0.9` |
| `dynamicsOptions.tenutoDynamicDelta` | `double: 0.3` |
| `dynamicsOptions.ghostNoteDynamicDelta` | `double: 2.25` |
| `dynamicsOptions.dynamicHumanisePercent` | `int: 7` |
| `dynamicsOptions.primaryBeatStressDelta` | `double: 0.35` |
| `dynamicsOptions.beatStressDelta` | `double: 0.15` |
| `noteExpressionOptions.enableNoteExpression` | `bool: false` |

The last row is **Use pitch contour emphasis**. Its override switch was on but
its checkbox was left unticked (confirmed from a screenshot of the map), so
Dorico exported the override's value, false. Each override row has the switch
(override on/off) and the value; only rows whose switch is on are exported.

## Consequences for Fiddle

- Bridge maps can pin a linear curve (`"0.500000,1.000000,1.000000"`), a known
  level range and zero articulation deltas, and Fiddle can then recover each
  note's level exactly from CC1 or velocity: `level = min + (max − min)(code − 1)/126`.
  Effects left to Dorico (beat stress, humanisation, contour) add to the level
  before the curve, so a range wider than ±6 avoids clipping them.
- Fiddle then re-applies each layer's curve (S-curve above, with the layer
  map's `dynamicCurveParams`/min/max, else the project's) plus its articulation
  deltas.
- Because the new CC1 arrives after the note-on, a layer that converts the
  bridge's CC1 into note velocity at note start reads the previous dynamic.
  Fiddle must wait a few ms for CC1 (it has the playback delay to do so).
- The bridge's CC1 bases send velocity 100 regardless of dynamic, so velocity
  from those bases carries no dynamic information.
