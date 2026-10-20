# Orchestral strings bridge — current candidate r5

**Current (2026-10-20): [r5](r5/README.md)** is r4 with Dorico's note-duration
playback options pinned to 100%, so Dorico sends written lengths and Fiddle
applies the duration percentages. It is the Bridge maps Strings template; r4
stays frozen.

**r4 (2026-10-19): [r4](r4/README.md)** keeps r3's vocabulary, allocation
and grouping on wire schema 2 (typed Off values and an Init revision beacon on
KS126/127). The next host check is the
[one-voice note-length fixture](one-voice-length-test.md). The r3 description
below is retained; r3 stays frozen as a schema-1 map (still decoded, no beacon).

Revision 3, curated on 2026-10-16, is the current bowed-orchestral-strings
candidate. It covers violin, viola, cello and double bass, solo or section.
Guitar and harp are outside this profile. A guitar bridge and bridge-map
customization are planned separately.

**Inspection only: no compatible Fiddle decoder exists.** The r3 map has been
generated and locally validated; it has not been imported or played in Dorico.
Generation does not install maps or change endpoints, playback templates or plugins.

**Supported routing: one Dorico voice per MIDI channel.** Shared-channel
multi-voice behavior is outside playback-fidelity acceptance. The earlier
shared-voice experiments are historical evidence and do not block this work.

| Item | Historical r2 | Current r3 |
| --- | ---: | ---: |
| Add-on dimensions | 49 | **32** |
| Base articulation/length dimension | 1 | 1 |
| Total conduits | 50 | **33** |
| CCs / keyswitch pitches used | 18 / 32 | **18 / 15** |
| Spare conduits in the 52-conduit pool | 2 | **19** |
| Base / add-on switch rows | 25 / 106 | **25 / 87** |
| Distinct encoded built-in PT IDs | 110 | **91** |
| Built-in aliases | 1 | 1 |
| Explicit mutex groups | 22 | 22 |

The 32 add-on dimensions comprise 19 multi-value groups and 13 independent flags.
Mutex groups stay at 22 because the removed dimensions were independent flags;
two Natural-cancellation groups and the base shortening group remain too.

Nineteen PTs were removed from the default vocabulary, freeing 17 dimensions.
The exclusions defer guitar-specific and generic jazz/pitch FX and two ambiguous
percussion controls. They are scope choices, not assertions that string players
cannot imitate those gestures. No retained PTs were merged, packed or aliased to
save space. Keep general articulation, dynamics, ornament and string-specific
extended techniques with a clear role. Nail pizzicato and strummed/alla-chitarra
pizzicato remain because they are relevant to bowed strings, despite the names.

- [Current r3 map and review notes](r3/README.md).
- [Current allocation](r3/allocation.md) and [machine-readable manifest](r3/allocation.json).
- [All 298 built-in PT dispositions](r3/inventory.md), including explicit exclusion reasons.
- [Curation decisions and supporting sources](r3/curation.md).
- [Historical r2 overview and host results](r2-history.md).

The r2 map, root-level `allocation.json`/`allocation.md`/`inventory.md`, and score
fixtures remain frozen for historical capture analysis. **Use `r3/` for current
allocation work.** Compacting dimensions changes some controller numbers and enum
values, so r3 has a new entity ID and must eventually use its own matching decoder.
Do not decode r2 captures with r3 allocation or assign r3 to existing Fiddle decoders.

Generate/check the current candidate:

```sh
python3 experiments/orchestral-strings-bridge/generate.py
python3 experiments/orchestral-strings-bridge/generate.py --check
python3 -m unittest discover -s experiments/orchestral-strings-bridge -p 'test_*.py' -v
```

Check historical r2 reproduction with `generate.py --revision 2 --check`.
The current [section-profile plan](../../docs/orchestral-bridge-maps.md) governs
remaining host acceptance and future runtime integration. PT coverage means an
ID can be transmitted if Dorico activates it; dynamics and ornaments may also
produce performance MIDI that must not be performed a second time by a decoder.

Future MusicXML fixtures must omit instructional text markers and place entry
instructions in a separate guide. No new host playback test is requested by this
vocabulary revision.
