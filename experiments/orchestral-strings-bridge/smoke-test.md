# Strings r2: first playback/cancellation smoke test

Status: **completed for this bounded baseline**. The
[fresh live capture after reopening Dorico](smoke-results-03.md) matches all 16
corrected PT sets and the MIDI export. The earlier first-run discrepancy is
preserved for investigation; do not repeat this unchanged test again now.
This is a bounded first test, not a
full-profile certification. No new map revision or Fiddle integration is needed.

## 1. Create the disposable score

Import [smoke-r2.musicxml](smoke-r2.musicxml) into a **new** Dorico project.
It contains one violin, four bars of 4/4, and sixteen quarter-note C5s at 80 bpm.
Save it separately from your musical projects.

**The text above the notes is just a checklist. It does not activate PTs.**
Enter real playing techniques as follows. Use the named built-in playback
techniques, not similarly named vendor/custom PTs. If a playing-technique symbol
is missing from your palette, stop and tell us which one rather than replacing
the intended PT with a lookalike. A test-only notation linked to the existing
built-in PT is possible, but we should check the mapping explicitly.

| Location | Notes | Actual playing technique to enter | Purpose |
| --- | --- | --- | --- |
| Bar 1, beats 1–2 | 1–2 | None | Baseline; do not add a dynamic or articulation |
| Bar 1, beats 3–4 | 3–4 | Flautando, with an explicit duration covering **only these two notes** | Temporary direction with no successor |
| Bar 2, beats 1–2 | 5–6 | None; specifically **do not add Natural** here | Flautando must expire to unspecified without help from another On |
| Bar 2, beat 3 | 7 | Overpressure, persistent | Establish pressure direction; note 8 repeats it |
| Bar 3, beat 1 | 9 | Non vibrato **and** Sul tasto, persistent | Coexist with the continuing Overpressure; note 10 repeats all three |
| Bar 3, beat 3 | 11 | Flautando, persistent | Replace Overpressure, retaining Non vibrato + Sul tasto; note 12 repeats |
| Bar 4, beat 1 | 13 | Fouetté, on this note | Fourth simultaneous add-on, in Bow stroke |
| Bar 4, beat 2 | 14 | Détaché, persistent | Replace Fouetté, retaining the other three dimensions |
| Bar 4, beat 3 | 15 | Natural / ord., explicitly mapped to `pt.natural` | Clear pressure, contact and stroke, but retain Non vibrato |
| Bar 4, beat 4 | 16 | Vibrato (`pt.vibrato`, not Con vibrato) | Replace Non vibrato |

For the first Flautando span, inspect its duration/continuation handle in Dorico:
it must include notes 3 and 4 but not note 5. Do not merely place ordinary text
or add a persistent Flautando direction with no endpoint. This is the only
duration edit needed in this first test. Do not add slurs or overlapping voices.

The original desired states are in [smoke-r2.expected.json](smoke-r2.expected.json).
For comparison, correct note 15 to no active add-ons: the host's explicit Natural
resets Non vibrato too, contrary to the original assumption in the table/fixture.
The fixture is retained unchanged to preserve the original test hypothesis.
They are test hypotheses, **not claims about what Dorico actually does**.
In particular, Natural cancellation and temporary expiry are things we are
measuring. There is no expectation that expiry restores a previous direction.

## 2. Route only to the silent capture plugin

The existing **Fiddle MIDI Probe** bundle is present in your user VST3 directory;
no plugin installation or rebuild was performed for this test. Whether Dorico
currently lists it must be checked in the host.

1. In this disposable project, load **Fiddle MIDI Probe** as the violin's VST
   instrument. This is **not** the ordinary **Fiddle** plugin.
2. Route the violin to **Port 1, Channel 1**. Check endpoint port/channel counts
   if necessary; do not assume import has assigned the desired routing.
3. Assign **Fiddle Bridge - Orchestral Strings r2 - INSPECTION ONLY** to that
   endpoint/channel. Import r2 into the new project if it is not already listed.
   Importing a map alone is not assignment. Do not use r1 or the old r6 probe map.
4. Save the disposable project. If the capture-only plugin is unavailable, stop
   here; do **not** substitute the live Fiddle endpoint or a sounding VSTi.

Silence is intentional. The capture-only plugin never relays these messages to
Fiddle, so no runtime decoder changes or Fiddle restart are required. Normal
Fiddle projects/endpoints must remain unchanged.

## 3. One capture, then inspect before repeating

Play once from the beginning through all four bars (about 12 seconds), then stop.
Close the test project and quit Dorico to ensure the capture is flushed and closed.
Tell the assistant **done**. We will locate the new closed probe capture under
the macOS temporary directory and inspect it before requesting another pass.
Do not delete earlier captures or overwrite previous experiment exports.

Optionally export MIDI to `experiments/orchestral-strings-bridge/smoke-r2.mid`.
That is useful corroboration, but live capture is needed for input/event ordering;
an export alone cannot establish real-time VST3 behaviour.

## What the capture review must establish

- Bind the trace to this score and the r2 allocation, not the archived r6 decoder.
  Preserve original event sequence, block, sample offset and input-source fields.
- Reject missing footer, invalid/dropped input or capture overflow. Select the
  actual playing pass and correct endpoint; distinguish selection auditions from
  played notes using transport flags. Do not borrow a previous pass's state.
- Check all 16 musical note-ons against expected PT sets in event order. Natural
  base means no extra articulation here; record the actual length code separately
  without assuming one particular Dorico duration bucket.
- Track state as **unknown** until a command establishes it. Report incomplete
  initialization separately; do not treat unobserved CC/KS dimensions as zero.
  This also provides a first observation of the full map's 50-action Init.
- Inspect CC112 (pressure), CC109 (stroke), CC110 (contact) and CC105 (vibrato)
  around each transition. On values are listed in [allocation.md](allocation.md).
  Report Off cadence across repeated notes and expiry at note 5, including any
  old Off that follows and erases a replacement On.
- KS NoteOff/velocity zero is only a pulse release, **not** a logical reset.
  KS velocity 1 is the explicit unspecified command. Retain Init KS events even
  though no active KS-based PT is requested in this first score.
- At equal sample offsets, CC parameters and note events come from different
  VST3 lists. The adapter's parameters-before-events merge is a policy, not a
  host-proven total order. Report those ties rather than silently upgrading them
  to a full timing pass.

The old `inspect_live.py` / `inspect_midi.py` decoder entry points are for the
archived maps and must **not** be run against this test as if it were r6. Their
low-level trace/MIDI readers can be reused, but r2 analysis must use this folder's
allocation and expectation files. No r2 runtime decoder is being installed.

## Follow-up gates (not requested yet)

After reviewing this first pass: active KS-based enum replacement and independent
KS combinations; overlapping note releases at a replacement boundary; fresh
mid-passage start and stop/restart; explicit Natural versus base fallback; and
exact-base versus equivalent base/add-on precedence. The present 25-base map
does not create a redundant decomposition for the last test, so that requires a
separate, explicitly labelled probe rather than an inference from this score.

## Local reproduction

```sh
python3 experiments/orchestral-strings-bridge/smoke_score.py
python3 experiments/orchestral-strings-bridge/smoke_score.py --check
python3 -m unittest discover -s experiments/orchestral-strings-bridge -p 'test_*.py' -v
```

Local tests verify the scaffold, expectations and map binding, not Dorico import
or PT activation. The original r1/r2 expression-map XML is unchanged by this step.
