# Orchestral pitched percussion r1 — generated allocation

Wire schema 2 candidate: compiled by Fiddle, **not yet host-verified**. See [README](README.md) for scope, cancellation policy and caveats.

**24/50 conduits: 15 CCs + 9 KS pitches; 26 spare.** 25 bases + 52 add-ons = 77 switch rows, plus one Init switch (26 actions, including the two beacon keyswitches). 56 distinct encoded IDs + 0 built-in alias; 12 explicit mutex groups.

All pitch numbers are MIDI integers, not octave labels. CC value 0 means unspecified except CC102 (base code 0); KS velocity 1 means unspecified. Physical KS releases are not dimension resets. Every add-on Off writes its typed value On+64: it clears the dimension only while that technique is current. Init writes the unspecified value. Init also sends the revision beacon KS126 velocity 122 and KS127 velocity 35; those pitches are reserved and carry no technique.

## Base (CC102)

Copied articulation/length codes; the old CC103 reset is removed.

| Value | PTs | Condition |
| --- | --- | --- |
| 0 | pt.natural | `NoteLength == kVeryShort` |
| 1 | pt.natural | `NoteLength == kShort` |
| 2 | pt.natural | `NoteLength == kMedium` |
| 3 | pt.natural | `NoteLength == kLong` |
| 4 | pt.natural | `NoteLength > kLong` |
| 5 | pt.staccatissimo | `NoteLength == kVeryShort` |
| 6 | pt.staccatissimo | `NoteLength == kShort` |
| 7 | pt.staccatissimo | `NoteLength == kMedium` |
| 8 | pt.staccatissimo | `NoteLength == kLong` |
| 9 | pt.staccatissimo | `NoteLength > kLong` |
| 10 | pt.staccato | `NoteLength == kVeryShort` |
| 11 | pt.staccato | `NoteLength == kShort` |
| 12 | pt.staccato | `NoteLength == kMedium` |
| 13 | pt.staccato | `NoteLength == kLong` |
| 14 | pt.staccato | `NoteLength > kLong` |
| 15 | pt.staccato + pt.tenuto | `NoteLength == kVeryShort` |
| 16 | pt.staccato + pt.tenuto | `NoteLength == kShort` |
| 17 | pt.staccato + pt.tenuto | `NoteLength == kMedium` |
| 18 | pt.staccato + pt.tenuto | `NoteLength == kLong` |
| 19 | pt.staccato + pt.tenuto | `NoteLength > kLong` |
| 20 | pt.tenuto | `NoteLength == kVeryShort` |
| 21 | pt.tenuto | `NoteLength == kShort` |
| 22 | pt.tenuto | `NoteLength == kMedium` |
| 23 | pt.tenuto | `NoteLength == kLong` |
| 24 | pt.tenuto | `NoteLength > kLong` |

## Add-on dimensions

Each multi-value row is one mutex group. Singletons are independent; those marked nat. also have a two-member cancellation group with Natural. Tenuto is deliberately not in the shortening mutex group.

| Dimension | Conduit | Init | Named On values | Explicit nat. cancels? |
| --- | --- | --- | --- | --- |
| connection | CC103 | 0 | 1: Legato (`pt.legato`); 2: Non legato (`pt.nonLegato`) | No |
| accent | CC104 | 0 | 1: Accent (`pt.accent`); 2: Marcato (`pt.marcato`) | No |
| tremolo | CC115 | 0 | 1: Tremolo (`pt.tremolo`) | No |
| trill | CC116 | 0 | 1: Trill (`pt.trill`); 2: Trill (half-step) (`pt.trill.half`); 3: Trill (whole step) (`pt.trill.whole`) | No |
| arpeggio | CC117 | 0 | 1: Arpeggio up (`pt.arpeggioUp`); 2: Arpeggio down (`pt.arpeggioDown`) | No |
| dynamic_level | CC118 | 0 | 1: ppp (`pt.ppp`); 2: pp (`pt.pp`); 3: p (`pt.p`); 4: mp (`pt.mp`); 5: mf (`pt.mf`); 6: f (`pt.f`); 7: ff (`pt.ff`); 8: fff (`pt.fff`) | No |
| dynamic_attack | CC119 | 0 | 1: ffp (`pt.ffp`); 2: fp (`pt.fp`); 3: pf (`pt.pf`); 4: sf (`pt.sf`); 5: sfp (`pt.sfp`); 6: sfz (`pt.sfz`) | No |
| dynamic_shape | KS0 | 1 | 2: Crescendo (`pt.crescendo`); 3: Diminuendo (`pt.diminuendo`); 4: Crescendo-diminuendo (`pt.crescendoDiminuendo`); 5: Diminuendo-crescendo (`pt.diminuendoCrescendo`); 6: Volume swell (`pt.volumeSwell`) | No |
| fade_in | KS1 | 1 | 2: Fade in (`pt.fadeIn`) | No |
| fade_out | KS2 | 1 | 2: Fade out (`pt.fadeOut`) | No |
| soft | KS3 | 1 | 2: Soft (`pt.soft`) | No |
| ghost_note | KS4 | 1 | 2: Ghost note (`pt.ghostNote`) | No |
| glissando | KS9 | 1 | 2: Glissando up (`pt.glissandoUp`); 3: Glissando down (`pt.glissandoDown`) | No |
| damp | KS12 | 1 | 2: Damp (`pt.damp`) | No |
| laissez_vibrer | KS13 | 1 | 2: Laissez vibrer (`pt.laissezVibrer`) | No |
| simile | KS14 | 1 | 2: Simile (`pt.simile`) | No |
| beater | CC105 | 0 | 1: Felt (beater) (`pt.beaterFelt`); 2: Rods (beater) (`pt.beaterRods`); 3: Puffy (beater) (`pt.beaterPuffy`); 4: Dreads (beater) (`pt.beaterDreads`); 5: Sticks (`pt.sticks`) | No |
| strike_location | CC106 | 0 | 1: Centre (`pt.centre`); 2: Edge (drums) (`pt.edgeDrums`); 3: Rim (`pt.rim`) | Proposed yes |
| roll | CC107 | 0 | 1: Roll (`pt.roll`) | No |
| dead_stroke | CC108 | 0 | 1: Dead stroke (`pt.deadStroke`) | No |
| muffled | CC109 | 0 | 1: Muffled/Damped (`pt.muffledDamped`) | No |
| motor | CC110 | 0 | 1: Motor on (`pt.motorOn`); 2: Motor off (`pt.motorOff`) | No |
| pedal | CC111 | 0 | 1: Pedal (`pt.pedal`) | No |

## Grouping rationale

- **connection**: Connected versus detached phrasing; independent of stroke and articulation.
- **accent**: Accent strengths; keep separate from staccato and tenuto bases.
- **tremolo**: Independent repetition, including tremolo plus sul ponticello.
- **trill**: Generic or specified interval; independent of vibrato and tremolo.
- **arpeggio**: Direction of chord spreading.
- **dynamic_level**: Steady named levels; not a replacement for continuous CC1/7/11.
- **dynamic_attack**: Alternative named compound/attack dynamics, separate from steady level.
- **dynamic_shape**: Alternative complete envelopes; not a quantized loudness curve.
- **fade_in**: Independent onset fade; may coexist with fade out.
- **fade_out**: Independent release fade.
- **soft**: Independent soft colour; not equated with a written dynamic level.
- **ghost_note**: Independent ghosting.
- **glissando**: Direction of a glissando; retained independently of portamento.
- **damp**: Independent damping request for ringing strings.
- **laissez_vibrer**: Independent ringing request; no guessed relationship to note length.
- **simile**: Transmit the explicit PT if emitted; does not itself encode previous state or restore a stack.
- **beater**: Beater/mallet choice for one stroke. Built-in beater alternatives plus generic Sticks; ord. does not change mallets, so Natural does not cancel this group.
- **strike_location**: Playing spot, chiefly timpani (centre, edge, rim). Proposed: explicit nat./ord. returns to the normal spot.
- **roll**: Explicit percussion roll, independent of the bowed/measured pt.tremolo dimension. Snare roll variants are out of scope.
- **dead_stroke**: Stroke that is stopped immediately, independent of later damping or l.v.
- **muffled**: Muffled/damped resonance (e.g. covered timpani), kept separate from the Damp action.
- **motor**: Vibraphone motor state. Explicit Off is not the unspecified state.
- **pedal**: Percussion pedal PT (vibraphone damper indication). Dorico pedal lines may send CC64 instead; that controller is passed through separately.
