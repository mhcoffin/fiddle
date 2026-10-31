# Unpitched percussion probe r1 — generated allocation

Wire schema 2 candidate: compiled by Fiddle, **not yet host-verified**. See [README](README.md) for scope, cancellation policy and caveats.

**8/50 conduits: 6 CCs + 2 KS pitches; 42 spare.** 20 bases + 15 add-ons = 35 switch rows, plus one Init switch (10 actions, including the two beacon keyswitches). 19 distinct encoded IDs + 0 built-in alias + 9 IDs with percussion-map entries; 4 explicit mutex groups.

All pitch numbers are MIDI integers, not octave labels. CC value 0 means unspecified except CC102 (base code 0); KS velocity 1 means unspecified. Physical KS releases are not dimension resets. Every add-on Off writes its typed value On+64: it clears the dimension only while that technique is current. Init writes the unspecified value. Init also sends the revision beacon KS126 velocity 30 and KS127 velocity 11; those pitches are reserved and carry no technique.

## Base (CC102)

Copied articulation/length codes; the old CC103 reset is removed.
Dynamics: cc1 = the level arrives on CC1 just after the note-on; velocity = in the note-on.

| Value | PTs | Condition | Dynamics |
| --- | --- | --- | --- |
| 0 | pt.natural | `NoteLength == kVeryShort` | velocity |
| 1 | pt.natural | `NoteLength == kShort` | velocity |
| 2 | pt.natural | `NoteLength == kMedium` | velocity |
| 3 | pt.natural | `NoteLength == kLong` | velocity |
| 4 | pt.natural | `NoteLength > kLong` | velocity |
| 5 | pt.roll | `NoteLength == kVeryShort` | cc1 |
| 6 | pt.roll | `NoteLength == kShort` | cc1 |
| 7 | pt.roll | `NoteLength == kMedium` | cc1 |
| 8 | pt.roll | `NoteLength == kLong` | cc1 |
| 9 | pt.roll | `NoteLength > kLong` | cc1 |
| 10 | pt.tremolo | `NoteLength == kVeryShort` | cc1 |
| 11 | pt.tremolo | `NoteLength == kShort` | cc1 |
| 12 | pt.tremolo | `NoteLength == kMedium` | cc1 |
| 13 | pt.tremolo | `NoteLength == kLong` | cc1 |
| 14 | pt.tremolo | `NoteLength > kLong` | cc1 |
| 15 | pt.buzzRoll | `NoteLength == kVeryShort` | cc1 |
| 16 | pt.buzzRoll | `NoteLength == kShort` | cc1 |
| 17 | pt.buzzRoll | `NoteLength == kMedium` | cc1 |
| 18 | pt.buzzRoll | `NoteLength == kLong` | cc1 |
| 19 | pt.buzzRoll | `NoteLength > kLong` | cc1 |

## Percussion map `drumkitmap.user.Fiddle.UnpitchedProbe.A`

Fiddle Unpitched Probe A: 11 entries × 1 instruments = 11 entries; forSingleInstrument true. Dorico plays the pitch of the entry matching a note's techniques (an unmatched technique falls back to Natural and still reaches the expression map), and sustains a roll only when that entry carries the roll. Every instrument has the same table.

| Pitch | PTs | Keyswitch |
| --- | --- | --- |
| 60 | Natural (`pt.natural`) | — |
| 61 | Rim (`pt.rim`) | — |
| 62 | Side stick (`pt.sideStick`) | — |
| 63 | Cross stick (`pt.crossStick`) | — |
| 64 | Roll (`pt.roll`) | — |
| 65 | Tremolo (`pt.tremolo`) | — |
| 66 | Buzz roll (`pt.buzzRoll`) | — |
| 67 | Rim (`pt.rim`) + Roll (`pt.roll`) | — |
| 68 | Rods (beater) (`pt.beaterRods`) | 15 |
| 69 | Rods (beater) (`pt.beaterRods`) + Rim (`pt.rim`) | — |
| 70 | Accent (`pt.accent`) | — |

Instruments: `instrument.orchestral.unpitched.snare.drum`

## Percussion map `drumkitmap.user.Fiddle.UnpitchedProbe.B`

Fiddle Unpitched Probe B: 1 entries × 1 instruments = 1 entries; forSingleInstrument true. Dorico plays the pitch of the entry matching a note's techniques (an unmatched technique falls back to Natural and still reaches the expression map), and sustains a roll only when that entry carries the roll. Every instrument has the same table.

| Pitch | PTs | Keyswitch |
| --- | --- | --- |
| 60 | Natural (`pt.natural`) | — |

Instruments: `instrument.orchestral.unpitched.snare.drum`

## Add-on dimensions

Each multi-value row is one mutex group. Singletons are independent; those marked nat. also have a two-member cancellation group with Natural. Tenuto is deliberately not in the shortening mutex group.

| Dimension | Conduit | Init | Named On values | Explicit nat. cancels? |
| --- | --- | --- | --- | --- |
| accent | CC104 | 0 | 1: Accent (`pt.accent`); 2: Marcato (`pt.marcato`) | No |
| beater | CC105 | 0 | 1: Felt (beater) (`pt.beaterFelt`); 2: Rods (beater) (`pt.beaterRods`); 3: Puffy (beater) (`pt.beaterPuffy`); 4: Dreads (beater) (`pt.beaterDreads`); 5: Sticks (`pt.sticks`) | No |
| strike_location | CC106 | 0 | 1: Centre (`pt.centre`); 2: Edge (drums) (`pt.edgeDrums`); 3: Rim (`pt.rim`) | Proposed yes |
| dead_stroke | CC108 | 0 | 1: Dead stroke (`pt.deadStroke`) | No |
| ghost_note | KS4 | 1 | 2: Ghost note (`pt.ghostNote`) | No |
| damp | KS12 | 1 | 2: Damp (`pt.damp`) | No |
| snares | CC112 | 0 | 1: Snares on (`pt.snareOn`); 2: Snares off (`pt.snareOff`) | No |

## Grouping rationale

- **accent**: Accent strengths; keep separate from staccato and tenuto bases.
- **beater**: Beater/mallet choice for one stroke. Built-in beater alternatives plus generic Sticks; ord. does not change mallets, so Natural does not cancel this group.
- **strike_location**: Playing spot, chiefly timpani (centre, edge, rim). Proposed: explicit nat./ord. returns to the normal spot.
- **dead_stroke**: Stroke that is stopped immediately, independent of later damping or l.v.
- **ghost_note**: Independent ghosting.
- **damp**: Independent damping request for ringing strings.
- **snares**: Snare mechanism, a direction that holds until changed. An explicit Off is not the unspecified state.
