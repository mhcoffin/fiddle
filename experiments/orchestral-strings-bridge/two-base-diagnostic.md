# Two-base diagnostic d2 — restore the observed length transition

**Historical procedure.** The [one-voice-per-channel scope decision](../../docs/orchestral-bridge-maps.md#supported-routing-scope--one-voice-per-channel)
retires this shared-channel two-voice fixture from acceptance testing. The steps
below document the completed experiment; no repeat or timing follow-up is requested.

First host test completed: [results](two-base-results-01.md). The conditional
pair reproduces the original bar-2 unwanted clear; this remains a diagnostic.

The [map](Fiddle-Orchestral-Strings-r2-two-base-d2.doricolib) retains exactly two
original r2 Natural rows, including their conditions, actions and timing:

| CC102 | Original condition |
| ---: | --- |
| 2 | NoteLength == kMedium |
| 4 | NoteLength > kLong |

All 106 add-ons, On/Off actions, 50 Init commands and mutex data remain unchanged.
The other 23 bases are omitted. No fallback covers VeryShort, Short or Long;
this is limited to the existing score, whose original captures used bases 2 and 4.
If the host selects another category or cannot select a base, the comparison is
inconclusive. This is not a production map or decoder.

## Run the same score

1. Open the successful d1 shared-overlap project. Save As a new copy named
   `Strings r2 shared overlap - two base d2.dorico`. Keep all music, PT markings,
   tempo, routing and independent-voice settings unchanged.
2. In Library > Expression Maps, import
   `Fiddle-Orchestral-Strings-r2-two-base-d2.doricolib` from this directory.
3. Assign **Fiddle Strings r2 - TWO BASE d2 - PROBE ONLY** to the violin's
   **Fiddle MIDI Probe, Port 1 / Channel 1** endpoint. Both voices share it.
   Confirm the map has the two conditional Natural bases above. Import alone
   does not assign a map. Use the silent Probe endpoint for this test.
4. Keep Flautando active for both voices in bars 1–2 and built-in Natural
   (`nat`/`ord`) in bar 3, as in the successful d1 run.
5. Save, fully quit/reopen Dorico, reopen the d2 copy and verify its assignment.
   Play once from the start through bar 3's rests.
6. Fully quit Dorico to close the capture, then tell the assistant **done**.

No new score import or notation entry is needed. The original r2 and d1 maps
and their projects remain useful comparisons.

## Read the result

Use `shared-overlap-two-base-d2.expected.json` and unchanged `allocation.json`.
Check a complete capture, all initialization, 12 notes/releases, actual overlap,
and bases 2/4. Confirm an actual 2→4 transition near bar 2 before treating this
as a test of switching. Shared-channel base state need not track each overlapping
note's individual length; inspect every CC102/112 event and note-start PT set.

Expected musical intent remains Flautando on the first ten notes, absent on the
last two. Inspect transient clears/reassertions too, not only note snapshots.

- If the bar-2 On-before-Off returns with the 2→4 transition, the two retained
  conditional bases suffice to reproduce the anomaly; the other 23 rows are
  unnecessary in this fixture. This does not prove Dorico's internal mechanism.
- If the transition happens without a clear, the minimal pair is insufficient
  to reproduce r2's symptom; inspect the difference before proposing a repair.
- If there is no transition, missing state, or changed overlap geometry, report
  an inconclusive comparison.
- Report bar-3 cancellation independently. Keep Off actions intact regardless
  of the outcome; this experiment does not justify dropping length transmission.

## Reproduce locally

```sh
python3 experiments/orchestral-strings-bridge/two_base_diagnostic.py --check
python3 -m unittest discover -s experiments/orchestral-strings-bridge -p 'test_*.py' -v
```

The generator checks the frozen r2 hash. Structural tests verify that the retained
rows and all other behavioral data are unchanged and bind expectations to the
new map identity/hash and unchanged score. Local checks do not establish host acceptance.
