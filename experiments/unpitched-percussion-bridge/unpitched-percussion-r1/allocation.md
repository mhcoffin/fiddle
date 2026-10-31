# Orchestral unpitched percussion r1 — generated allocation

Wire schema 2 candidate: compiled by Fiddle, **not yet host-verified**. See [README](README.md) for scope, cancellation policy and caveats.

**20/50 conduits: 11 CCs + 9 KS pitches; 30 spare.** 75 bases + 76 add-ons = 151 switch rows, plus one Init switch (22 actions, including the two beacon keyswitches). 90 distinct encoded IDs + 0 built-in alias + 11 IDs with percussion-map entries; 10 explicit mutex groups.

All pitch numbers are MIDI integers, not octave labels. CC value 0 means unspecified except CC102 (base code 0); KS velocity 1 means unspecified. Physical KS releases are not dimension resets. Every add-on Off writes its typed value On+64: it clears the dimension only while that technique is current. Init writes the unspecified value. Init also sends the revision beacon KS126 velocity 33 and KS127 velocity 111; those pitches are reserved and carry no technique.

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
| 5 | pt.staccatissimo | `NoteLength == kVeryShort` | velocity |
| 6 | pt.staccatissimo | `NoteLength == kShort` | velocity |
| 7 | pt.staccatissimo | `NoteLength == kMedium` | velocity |
| 8 | pt.staccatissimo | `NoteLength == kLong` | velocity |
| 9 | pt.staccatissimo | `NoteLength > kLong` | velocity |
| 10 | pt.staccato | `NoteLength == kVeryShort` | velocity |
| 11 | pt.staccato | `NoteLength == kShort` | velocity |
| 12 | pt.staccato | `NoteLength == kMedium` | velocity |
| 13 | pt.staccato | `NoteLength == kLong` | velocity |
| 14 | pt.staccato | `NoteLength > kLong` | velocity |
| 15 | pt.staccato + pt.tenuto | `NoteLength == kVeryShort` | velocity |
| 16 | pt.staccato + pt.tenuto | `NoteLength == kShort` | velocity |
| 17 | pt.staccato + pt.tenuto | `NoteLength == kMedium` | velocity |
| 18 | pt.staccato + pt.tenuto | `NoteLength == kLong` | velocity |
| 19 | pt.staccato + pt.tenuto | `NoteLength > kLong` | velocity |
| 20 | pt.tenuto | `NoteLength == kVeryShort` | velocity |
| 21 | pt.tenuto | `NoteLength == kShort` | velocity |
| 22 | pt.tenuto | `NoteLength == kMedium` | velocity |
| 23 | pt.tenuto | `NoteLength == kLong` | velocity |
| 24 | pt.tenuto | `NoteLength > kLong` | velocity |
| 25 | pt.roll | `NoteLength == kVeryShort` | cc1 |
| 26 | pt.roll | `NoteLength == kShort` | cc1 |
| 27 | pt.roll | `NoteLength == kMedium` | cc1 |
| 28 | pt.roll | `NoteLength == kLong` | cc1 |
| 29 | pt.roll | `NoteLength > kLong` | cc1 |
| 30 | pt.tremolo | `NoteLength == kVeryShort` | cc1 |
| 31 | pt.tremolo | `NoteLength == kShort` | cc1 |
| 32 | pt.tremolo | `NoteLength == kMedium` | cc1 |
| 33 | pt.tremolo | `NoteLength == kLong` | cc1 |
| 34 | pt.tremolo | `NoteLength > kLong` | cc1 |
| 35 | pt.buzzRoll | `NoteLength == kVeryShort` | cc1 |
| 36 | pt.buzzRoll | `NoteLength == kShort` | cc1 |
| 37 | pt.buzzRoll | `NoteLength == kMedium` | cc1 |
| 38 | pt.buzzRoll | `NoteLength == kLong` | cc1 |
| 39 | pt.buzzRoll | `NoteLength > kLong` | cc1 |
| 40 | pt.pressRoll | `NoteLength == kVeryShort` | cc1 |
| 41 | pt.pressRoll | `NoteLength == kShort` | cc1 |
| 42 | pt.pressRoll | `NoteLength == kMedium` | cc1 |
| 43 | pt.pressRoll | `NoteLength == kLong` | cc1 |
| 44 | pt.pressRoll | `NoteLength > kLong` | cc1 |
| 45 | pt.flatRoll | `NoteLength == kVeryShort` | cc1 |
| 46 | pt.flatRoll | `NoteLength == kShort` | cc1 |
| 47 | pt.flatRoll | `NoteLength == kMedium` | cc1 |
| 48 | pt.flatRoll | `NoteLength == kLong` | cc1 |
| 49 | pt.flatRoll | `NoteLength > kLong` | cc1 |
| 50 | pt.circularRoll | `NoteLength == kVeryShort` | cc1 |
| 51 | pt.circularRoll | `NoteLength == kShort` | cc1 |
| 52 | pt.circularRoll | `NoteLength == kMedium` | cc1 |
| 53 | pt.circularRoll | `NoteLength == kLong` | cc1 |
| 54 | pt.circularRoll | `NoteLength > kLong` | cc1 |
| 55 | pt.swooshRoll | `NoteLength == kVeryShort` | cc1 |
| 56 | pt.swooshRoll | `NoteLength == kShort` | cc1 |
| 57 | pt.swooshRoll | `NoteLength == kMedium` | cc1 |
| 58 | pt.swooshRoll | `NoteLength == kLong` | cc1 |
| 59 | pt.swooshRoll | `NoteLength > kLong` | cc1 |
| 60 | pt.trill | `NoteLength == kVeryShort` | cc1 |
| 61 | pt.trill | `NoteLength == kShort` | cc1 |
| 62 | pt.trill | `NoteLength == kMedium` | cc1 |
| 63 | pt.trill | `NoteLength == kLong` | cc1 |
| 64 | pt.trill | `NoteLength > kLong` | cc1 |
| 65 | pt.trill.half | `NoteLength == kVeryShort` | cc1 |
| 66 | pt.trill.half | `NoteLength == kShort` | cc1 |
| 67 | pt.trill.half | `NoteLength == kMedium` | cc1 |
| 68 | pt.trill.half | `NoteLength == kLong` | cc1 |
| 69 | pt.trill.half | `NoteLength > kLong` | cc1 |
| 70 | pt.trill.whole | `NoteLength == kVeryShort` | cc1 |
| 71 | pt.trill.whole | `NoteLength == kShort` | cc1 |
| 72 | pt.trill.whole | `NoteLength == kMedium` | cc1 |
| 73 | pt.trill.whole | `NoteLength == kLong` | cc1 |
| 74 | pt.trill.whole | `NoteLength > kLong` | cc1 |

## Percussion map `drumkitmap.user.Fiddle.OrchestralUnpitchedPercussion.r1`

Fiddle Bridge - Orchestral Unpitched Percussion r1 (host test): 11 entries × 75 instruments = 825 entries; forSingleInstrument true. Dorico plays the pitch of the entry matching a note's techniques (an unmatched technique falls back to Natural and still reaches the expression map), and sustains a roll only when that entry carries the roll. Every instrument has the same table.

| Pitch | PTs | Keyswitch |
| --- | --- | --- |
| 36 | Natural (`pt.natural`) | — |
| 36 | Roll (`pt.roll`) | — |
| 36 | Tremolo (`pt.tremolo`) | — |
| 36 | Buzz roll (`pt.buzzRoll`) | — |
| 36 | Press roll (`pt.pressRoll`) | — |
| 36 | Flat roll (`pt.flatRoll`) | — |
| 36 | Circular roll (`pt.circularRoll`) | — |
| 36 | Swoosh roll (`pt.swooshRoll`) | — |
| 36 | Trill (`pt.trill`) | — |
| 36 | Trill (half-step) (`pt.trill.half`) | — |
| 36 | Trill (whole step) (`pt.trill.whole`) | — |

Instruments: `instrument.band.unpitched.floortom.high`, `instrument.band.unpitched.floortom.low`, `instrument.band.unpitched.roto.tom.high`, `instrument.band.unpitched.roto.tom.low`, `instrument.band.unpitched.roto.tom.medium`, `instrument.band.unpitched.roto.tom.medium-high`, `instrument.band.unpitched.roto.tom.medium-low`, `instrument.band.unpitched.tomtom.high`, `instrument.band.unpitched.tomtom.high.alias.tom-high`, `instrument.band.unpitched.tomtom.low`, `instrument.band.unpitched.tomtom.low.alias.tom-low`, `instrument.band.unpitched.tomtom.medium`, `instrument.band.unpitched.tomtom.medium.alias.tom-medium`, `instrument.band.unpitched.tomtom.medium-high`, `instrument.band.unpitched.tomtom.medium-high.alias.tom-medium-high`, `instrument.band.unpitched.tomtom.medium-low`, `instrument.band.unpitched.tomtom.medium-low.alias.tom-medium-low`, `instrument.latin.unpitched.bongo.bell.high`, `instrument.latin.unpitched.bongo.bell.low`, `instrument.latin.unpitched.bongo.high`, `instrument.latin.unpitched.bongo.low`, `instrument.latin.unpitched.conga.high`, `instrument.latin.unpitched.conga.low`, `instrument.latin.unpitched.conga.medium`, `instrument.latin.unpitched.conga.very.high`, `instrument.latin.unpitched.conga.very.low`, `instrument.orchestral.unpitched.anvil`, `instrument.orchestral.unpitched.bass.drum`, `instrument.orchestral.unpitched.belltree`, `instrument.orchestral.unpitched.clash.cymbals`, `instrument.orchestral.unpitched.crash.cymbal.high`, `instrument.orchestral.unpitched.crash.cymbal.low`, `instrument.orchestral.unpitched.hihat`, `instrument.orchestral.unpitched.hihat.pedal`, `instrument.orchestral.unpitched.large.gong`, `instrument.orchestral.unpitched.opera.gong`, `instrument.orchestral.unpitched.ratchet`, `instrument.orchestral.unpitched.ride.cymbal.high`, `instrument.orchestral.unpitched.ride.cymbal.low`, `instrument.orchestral.unpitched.side.drum`, `instrument.orchestral.unpitched.sizzle.cymbal`, `instrument.orchestral.unpitched.sleigh.bells`, `instrument.orchestral.unpitched.small.gong`, `instrument.orchestral.unpitched.snare.drum`, `instrument.orchestral.unpitched.suspended.cymbal`, `instrument.orchestral.unpitched.tamtam`, `instrument.orchestral.unpitched.temple.high`, `instrument.orchestral.unpitched.temple.low`, `instrument.orchestral.unpitched.temple.medium`, `instrument.orchestral.unpitched.temple.medium.high`, `instrument.orchestral.unpitched.temple.medium.low`, `instrument.orchestral.unpitched.tenor.drum`, `instrument.orchestral.unpitched.tenor.drum.high`, `instrument.orchestral.unpitched.tenor.drum.medium`, `instrument.orchestral.unpitched.tenor.drum.medium.high`, `instrument.orchestral.unpitched.tenor.drum.medium.low`, `instrument.orchestral.unpitched.thundersheet`, `instrument.orchestral.unpitched.triangle`, `instrument.orchestral.unpitched.whip`, `instrument.orchestral.unpitched.whistle`, `instrument.orchestral.unpitched.wind.gong`, `instrument.percussion.other.cowbell.high`, `instrument.percussion.other.cowbell.low`, `instrument.percussion.other.tambourine`, `instrument.percussion.other.wood.block.high`, `instrument.percussion.other.wood.block.low`, `instrument.percussion.other.wood.block.medium`, `instrument.percussion.other.wood.block.medium.high`, `instrument.percussion.other.wood.block.medium.low`, `instrument.unpitched.china.cymbal`, `instrument.unpitched.flexatone`, `instrument.unpitched.kick.drum.low`, `instrument.unpitched.kick.drum.very.low`, `instrument.unpitched.ribboncrasher`, `instrument.unpitched.splash.cymbal`

## Add-on dimensions

Each multi-value row is one mutex group. Singletons are independent; those marked nat. also have a two-member cancellation group with Natural. Tenuto is deliberately not in the shortening mutex group.

| Dimension | Conduit | Init | Named On values | Explicit nat. cancels? |
| --- | --- | --- | --- | --- |
| accent | CC104 | 0 | 1: Accent (`pt.accent`); 2: Marcato (`pt.marcato`) | No |
| dynamic_level | CC118 | 0 | 1: ppp (`pt.ppp`); 2: pp (`pt.pp`); 3: p (`pt.p`); 4: mp (`pt.mp`); 5: mf (`pt.mf`); 6: f (`pt.f`); 7: ff (`pt.ff`); 8: fff (`pt.fff`) | No |
| dynamic_attack | CC119 | 0 | 1: ffp (`pt.ffp`); 2: fp (`pt.fp`); 3: pf (`pt.pf`); 4: sf (`pt.sf`); 5: sfp (`pt.sfp`); 6: sfz (`pt.sfz`) | No |
| dynamic_shape | KS0 | 1 | 2: Crescendo (`pt.crescendo`); 3: Diminuendo (`pt.diminuendo`); 4: Crescendo-diminuendo (`pt.crescendoDiminuendo`); 5: Diminuendo-crescendo (`pt.diminuendoCrescendo`); 6: Volume swell (`pt.volumeSwell`) | No |
| fade_in | KS1 | 1 | 2: Fade in (`pt.fadeIn`) | No |
| fade_out | KS2 | 1 | 2: Fade out (`pt.fadeOut`) | No |
| soft | KS3 | 1 | 2: Soft (`pt.soft`) | No |
| ghost_note | KS4 | 1 | 2: Ghost note (`pt.ghostNote`) | No |
| damp | KS12 | 1 | 2: Damp (`pt.damp`) | No |
| laissez_vibrer | KS13 | 1 | 2: Laissez vibrer (`pt.laissezVibrer`) | No |
| simile | KS14 | 1 | 2: Simile (`pt.simile`) | No |
| beater | CC105 | 0 | 1: Felt (beater) (`pt.beaterFelt`); 2: Rods (beater) (`pt.beaterRods`); 3: Puffy (beater) (`pt.beaterPuffy`); 4: Dreads (beater) (`pt.beaterDreads`); 5: Sticks (`pt.sticks`) | No |
| dead_stroke | CC108 | 0 | 1: Dead stroke (`pt.deadStroke`) | No |
| muffled | CC109 | 0 | 1: Muffled/Damped (`pt.muffledDamped`) | No |
| strike_location | CC106 | 0 | 1: Centre (`pt.centre`); 2: Edge (drums) (`pt.edgeDrums`); 3: Rim (`pt.rim`); 4: Edge (cymbals) (`pt.edgeCymbals`); 5: Rim shot (`pt.rimShot`); 6: Rim knock (`pt.rimKnock`); 7: Cross stick (`pt.crossStick`); 8: Side stick (`pt.sideStick`); 9: Shell (`pt.shell`); 10: Bell (`pt.bell`); 11: Frame (`pt.frame`); 12: Skin (`pt.skin`); 13: Hi-hat (`pt.hiHat`); 14: Stick shot (`pt.stickShot`); 15: Stick click (`pt.stickClick`); 16: Stick snap (`pt.stickSnap`); 17: Backstick (`pt.backstick`); 18: Stand click (`pt.standClick`); 19: Tap (`pt.tap`); 20: Click (`pt.click`); 21: Ping (`pt.ping`); 22: Ding (`pt.ding`); 23: Foot (`pt.foot`); 24: Scrape (`pt.scrape`); 25: Swish (`pt.swish`); 26: Slap (`pt.slap`); 27: Slap (congas) (`pt.slapCongas`); 28: Bass tone (`pt.bassTone`); 29: Shake (`pt.shake`); 30: Crush (`pt.crush`); 31: Sizzle (`pt.sizzle`); 32: Zing (`pt.zing`) | Proposed yes |
| snares | CC112 | 0 | 1: Snares on (`pt.snareOn`); 2: Snares off (`pt.snareOff`) | No |
| throwoff | CC113 | 0 | 1: Throwoff on (`pt.throwoffOn`); 2: Throwoff off (`pt.throwoffOff`) | No |
| choke | CC114 | 0 | 1: Choke (`pt.choke`) | No |
| openness | KS5 | 1 | 2: Closed (`pt.closed`); 3: Half-open (`pt.halfOpen`); 4: Open 2 (`pt.open2`); 5: Pedal (`pt.pedal`) | No |

## Grouping rationale

- **accent**: Accent strengths; keep separate from staccato and tenuto bases.
- **dynamic_level**: Steady named levels; not a replacement for continuous CC1/7/11.
- **dynamic_attack**: Alternative named compound/attack dynamics, separate from steady level.
- **dynamic_shape**: Alternative complete envelopes; not a quantized loudness curve.
- **fade_in**: Independent onset fade; may coexist with fade out.
- **fade_out**: Independent release fade.
- **soft**: Independent soft colour; not equated with a written dynamic level.
- **ghost_note**: Independent ghosting.
- **damp**: Independent damping request for ringing strings.
- **laissez_vibrer**: Independent ringing request; no guessed relationship to note length.
- **simile**: Transmit the explicit PT if emitted; does not itself encode previous state or restore a stack.
- **beater**: Beater/mallet choice for one stroke. Built-in beater alternatives plus generic Sticks; ord. does not change mallets, so Natural does not cancel this group.
- **dead_stroke**: Stroke that is stopped immediately, independent of later damping or l.v.
- **muffled**: Muffled/damped resonance (e.g. covered timpani), kept separate from the Damp action.
- **strike_location**: Where, or with what, the instrument is struck: the pitched map's centre/edge/rim on the same conduit and values, then every other built-in stroke and surface. Natural returns to the normal spot.
- **snares**: Snare mechanism, a direction that holds until changed. An explicit Off is not the unspecified state.
- **throwoff**: Throw-off lever, kept apart from the snare state: verify what notation emits it before merging.
- **choke**: Cymbal choke: a damping action on one stroke, independent of dead stroke and muffling.
- **openness**: Hi-hat openness and the pedal stroke, a direction that holds until changed.
