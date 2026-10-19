# Strings r2 — one shared PT timeline, with overlapping notes

**Two captures complete, not accepted:** the [repeat](shared-overlap-results-02.md)
reproduces Flautando On followed 16 ms later by Clear at bar 2's onset. The user
confirmed Flautando in the active-technique display. No more unchanged repeats
are needed; the procedure below records how these captures were made.
This replaces the retired mixed-technique
overlap fixture. Three bars, 12 notes, about nine seconds. Same unchanged r2
map and silent Fiddle MIDI Probe; no Fiddle runtime integration or installation.
There are no instructional words embedded in the MusicXML.

## Import and route

Import [shared-overlap-r2.musicxml](shared-overlap-r2.musicxml) as a **new**
disposable Dorico project. Do not reuse the old overlap score.

- One violin staff, 4/4, quarter = 80.
- Both voices use **Fiddle MIDI Probe, Port 1 / Channel 1** and
  **Fiddle Bridge - Orchestral Strings r2 - INSPECTION ONLY**.
- Leave independent voice playback **off**. Silence is intentional.
- No slurs, articulations, dynamics, duration overrides, or other PTs.

The notation should be:

| Bar | Lower/down-stem voice | Upper/up-stem voice |
| --- | --- | --- |
| 1: control | C4 quarter on beat 1, then rests | G5, D5, E5, F5 quarters |
| 2: overlap | C4 dotted half on beat 1, then a quarter rest | G5, D5, E5, F5 quarters |
| 3: cleanup | C4 quarter on beat 1, then rests | C5 quarter on beat 1, then rests |

MIDI numbers: C4=60, C5=72, D5=74, E5=76, F5=77, G5=79.
Both voices sound together at the start of every bar. Bar 2's low C should
overlap D/E and end before F; actual MIDI timing will establish that.

## Enter just one shared technique timeline

1. At **bar 1 beat 1**, enter actual **Flautando** so it applies to **both
   voices**, continuing through bar 2. If one staff-wide marking already
   applies to both, do not add duplicate markings. If Dorico scopes the item
   to the selected voice, apply the same technique to the other voice too.
2. At **bar 3 beat 1**, enter **`nat` or `ord`** for both voices, using the
   same scope check. Both select built-in Natural (`pt.natural`). **Do not type
   `natural`: the installed Art Conductor library maps that to a different,
   custom playback technique (`pt.user.natural`) that does not cancel Flautando.**
3. Do not add any other PT changes. In particular, do not add Overpressure,
   Strum Up/Down, or restatements on individual notes.

**Before capturing, check the active playback-technique display for each voice.**
Both must show Flautando through bars 1–2 and Natural at bar 3. Check especially
the upper D/E/F in bar 2. We are not asking the two voices to disagree about a
technique at any point. If the display disagrees or the intended scope cannot
be verified, stop and report that; do not force a capture or edit the bridge map.

## Capture

1. Save as a new project, for example `Strings r2 shared overlap.dorico`.
2. Fully quit and reopen Dorico, then load that saved project. Recheck the
   active PT display; avoid further edits before playback.
3. Play once from the beginning through bar 3 and its rests.
4. Fully quit Dorico to close the capture, then tell the assistant **done**.

No MIDI export is required. Do not overwrite any previous scores or captures.

## Analysis contract

Use [shared-overlap-r2.expected.json](shared-overlap-r2.expected.json), the r2
allocation, and the strict capture reader. Expected states are conditional
hypotheses, not observed results. Preserve the raw trace and its hash.

- Validate capture completeness and initialization; exclude idle auditions.
  Match 12 musical notes and their releases on the selected endpoint/channel.
  Simultaneous low/high note ordering is **not** prescribed; allow humanized
  onset offsets and match by onset neighborhood and pitch. Voice numbers
  describe notation, not transmitted MIDI IDs.
- The first ten notes should snapshot Flautando only; the final two should
  have no add-ons. Natural base/length codes are not extra techniques. Unknown
  initialization must not silently become an empty state.
- In bar 1, low C must end before D. In bar 2, low C must be active at D/E
  and end before F. If those lifetime conditions are absent, overlap-specific
  conclusions are inconclusive, regardless of note-start state matches.
- Inspect **CC112** (1=Flautando, 0=clear) around every musical release,
  especially the old C and the upper G/D/E. Report clears and reassertions,
  including their source, sample/offset and sequence. A correct F snapshot
  does not prove uninterrupted state: a new On might have repaired a clear.
- Distinguish note-start correctness from channel-state gaps between notes.
  A gap is an observation, not automatically an audible defect or decoder bug.
  Preserve equal-offset parameter/event-list ordering caveats.
- If controls fail despite verified shared PT activation, diagnose that before
  assigning an overlap cause. Do not repeat the previous assumption that
  different notated voices imply independently transmitted technique intent.

This is deliberately **only the persistent CC case**. It does not validate
keyswitch overlap, replacement of one technique by another during overlap,
all Off-event behaviors, independently changing voices, or downstream VSTi
timbres. Those cannot be inferred from a pass here.

## Reproduce locally

```sh
python3 experiments/orchestral-strings-bridge/shared_overlap_score.py --check
python3 -m unittest discover -s experiments/orchestral-strings-bridge -p 'test_*.py' -v
```
