# Single-base diagnostic d1 — same score, no note-length base transitions

**Historical procedure.** The [one-voice-per-channel scope decision](../../docs/orchestral-bridge-maps.md#supported-routing-scope--one-voice-per-channel)
retires this shared-channel two-voice fixture from acceptance testing. The steps
below document the completed experiment; no repeat is requested.

**First host capture passes the bounded shared-PT fixture.** See
[results](single-base-results-01.md): no bar-2 clear, and Natural cancels at bar 3. A separate diagnostic for the reproducible
[Flautando On-before-Off anomaly](shared-overlap-results-02.md), not a new
production bridge revision. Do not replace r2 or use this with ordinary Fiddle.

## What changes

[Fiddle-Orchestral-Strings-r2-single-base-d1.doricolib](Fiddle-Orchestral-Strings-r2-single-base-d1.doricolib)
retains the r2 Natural base with CC102=2, removes its note-length condition,
and omits the other 24 base switches. This leaves **one unconditional Natural
base**, so the shared-overlap score cannot change bases at bar 2.

All 106 add-ons, their On/Off actions, the 50 initialization commands, the
mutex groups, and other map settings are unchanged. In particular:

- Flautando On: **CC112=1**.
- Flautando Off: **CC112=0**, still present.
- Init CC102=0 remains unchanged; musical notes should then observe CC102=2.

The distinct display name is **Fiddle Strings r2 - SINGLE BASE d1 - PROBE ONLY**.
Its distinct entity ID prevents it from replacing r2. This map does not encode
note-length categories, staccato, tenuto, or the other removed base combinations.
Do not use it to evaluate those features. All original artifacts are retained.

## User steps

1. Open the saved **shared PT overlap** project (the three-bar, twelve-note
   score). Use **Save As** to create `Strings r2 shared overlap - single base d1.dorico`.
   Keep the score and playing-technique markings unchanged.
2. In Dorico's **Library > Expression Maps**, import the diagnostic `.doricolib`
   linked above. Confirm the new map has **one base switch** and Flautando still
   has both On and Off actions. Do not edit the original r2 map.
3. Assign **Fiddle Strings r2 - SINGLE BASE d1 - PROBE ONLY** to the violin's
   **Fiddle MIDI Probe, Port 1 / Channel 1** endpoint. Importing alone does not
   assign it. Both voices must share that endpoint; independent voice playback
   stays off. No production Fiddle or HALion instrument should receive the test.
4. Confirm Flautando remains active through bars 1–2, with built-in Natural
   (`nat` or `ord`, **not** custom `natural`) in bar 3. Change nothing else.
5. Save, fully quit/reopen Dorico, and reopen the **d1 copy**. Confirm the d1 map
   assignment remains selected. Play once from the start through bar 3's rests.
6. Fully quit Dorico to close the capture, then tell the assistant **done**.

No new MusicXML import or additional notation is needed. Silence is intentional.
This artifact has not been installed into DefaultLibraryAdditions or a playback
template; the manual import and assignment above are limited to the test copy.

## What the capture can establish

Use [shared-overlap-single-base-d1.expected.json](shared-overlap-single-base-d1.expected.json).
It binds the unchanged note/PT expectations to this map's identity and hash.
Add-on decoding uses unchanged r2 `allocation.json`; every musical note must
observe base value 2. Check that first to avoid analyzing an accidentally
retained r2 assignment. Base 2 is necessary but the saved assignment/identity
should also be verified where possible.

Validate a closed complete trace, initialization and the actual control/overlap
geometry exactly as before. Inspect the whole CC112 stream, not just note
snapshots. Expected intent is Flautando on the first ten notes, absent on the
final two. Report any transient clears, reassertions, or lingering state.

- If the bar 2 clear disappears, that supports a link to changing length-based
  switches in this fixture, not a proof of Dorico's internal algorithm or a
  justification for permanently dropping length transmission.
- If it persists, length-based switching is not necessary for this symptom;
  do not discard Off actions or invent a decoder repair based on that alone.
- If bar 2 holds Flautando but bar 3 fails to clear, continuation and legitimate
  cancellation remain separate issues; report both.
- No actual overlap, incomplete initialization, or incorrect assignment makes
  the relevant comparison inconclusive.

This diagnostic removes unused base rows as well as length conditions, so a
successful result implicates the base-selection configuration, without proving
one specific scheduler operation. Further isolation may be warranted.

## Reproduce locally

```sh
python3 experiments/orchestral-strings-bridge/single_base_diagnostic.py --check
python3 -m unittest discover -s experiments/orchestral-strings-bridge -p 'test_*.py' -v
```

The generator checks the frozen r2 source hash before deriving the map. Tests
check that only metadata and the base list/retained condition differ, all
add-ons/Off/Init/mutex data are identical, and expectations reference the same
score but the new diagnostic map. They do not establish Dorico host behavior.
