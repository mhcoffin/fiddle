# Strings r4 — one voice, persistent technique across changing note lengths

**Prepared 2026-10-19; not yet captured.** This is the in-scope follow-up to the
retired shared-overlap fixtures. In the r2 [shared-overlap captures](shared-overlap-results-02.md),
Dorico pre-rolled Flautando On about 16 ms before a note at a base (note-length)
change and sent the old combination's Off at the previous note's end, so the
latched decoder cleared a technique that was still active. The single-base d1
map removed the symptom; the two-base d2 map reproduced it. Since the trigger
is a base change rather than a second voice, it can occur with one voice per
channel whenever note lengths change under a persistent technique.

Wire schema 2 (strings r4) defends against this in two ways: typed Off values
(an Off ends only the technique it names) and suppression of an Off that
arrives within 30 ms of its own technique's On. This fixture checks the defence
on a real host and records whether the On-before-Off pattern occurs at all.

Five bars, 16 notes, about 15 seconds. No instructional text is embedded in
the MusicXML.

## Set up

1. In Fiddle, open **Technique Catalog → Bridge maps**, add **Strings**
   (r4, wire schema 2) and assign it to **Port 1, channel 2**. Give that chair
   a sound layer if you want audio; the decoded techniques are what is tested.
2. With Dorico closed, select **Chairs → Install Playback Template**, then open
   Dorico and apply the Fiddle playback template. Alternatively, export the r4
   map and assign it manually to Port 1 / Channel 2 of the Fiddle plug-in.
3. Import [one-voice-length-r4.musicxml](one-voice-length-r4.musicxml) as a
   **new** project. One violin, one voice, 4/4, quarter = 80. Leave independent
   voice playback off. Add no slurs, articulations, dynamics or other PTs.

| Bar | Notes (MIDI) | Purpose |
| --- | --- | --- |
| 1 | C5 D5 E5 F5 quarters (72 74 76 77) | Flautando begins |
| 2 | G5 E5 halves (79 76) | length category changes |
| 3 | C5 whole (72) | changes again |
| 4 | C D E F G F E D eighths | changes again (short) |
| 5 | C5 quarter, then rests | Natural |

## Enter the technique timeline

1. At **bar 1 beat 1**, enter **Flautando**, continuing through bar 4.
2. At **bar 5 beat 1**, enter **`nat`** or **`ord`** (built-in `pt.natural`).
   Do not type `natural`: the installed Art Conductor library maps it to the
   custom `pt.user.natural`, which does not cancel Flautando.
3. Check Dorico's active playback-technique display: Flautando on every note of
   bars 1–4, Natural at bar 5. If it disagrees, stop and report that.

## Play and record

1. Play from the beginning through bar 5.
2. In **Bridge maps**, the Port 1 / channel 2 row should report
   **Beacon matches**, with no withheld notes. Note the **stale Offs ignored** and
   **early Offs suppressed** counts. A non-zero early-Off count means the
   On-before-Off pattern occurred and was suppressed.
3. In the Note Inspector, every note of bars 1–4 should carry `pt.flautando`,
   and bar 5 should not.
4. Repeat once starting at bar 3. Init and the beacon must be resent; bar 3
   onward should still show Flautando.

Report the counters, any note whose techniques differ from
[the expectations](one-voice-length-r4.expected.json), and whether starting at
bar 3 changes the result. If a raw capture is wanted, add the Fiddle MIDI Probe
on another endpoint as in earlier fixtures. This fixture changes neither the map
nor the runtime.
