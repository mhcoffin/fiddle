# Orchestral brass r2 — generated allocation

Wire schema 2 candidate: compiled by Fiddle, **not yet host-verified**. See [README](README.md) for scope, cancellation policy and caveats.

**37/50 conduits: 18 CCs + 19 KS pitches; 13 spare.** 25 bases + 71 add-ons = 96 switch rows, plus one Init switch (39 actions, including the two beacon keyswitches). 75 distinct encoded IDs + 0 built-in alias; 14 explicit mutex groups.

All pitch numbers are MIDI integers, not octave labels. CC value 0 means unspecified except CC102 (base code 0); KS velocity 1 means unspecified. Physical KS releases are not dimension resets. Every add-on Off writes its typed value On+64: it clears the dimension only while that technique is current. Init writes the unspecified value. Init also sends the revision beacon KS126 velocity 27 and KS127 velocity 65; those pitches are reserved and carry no technique.

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
| vibrato | CC105 | 0 | 1: Non vibrato (`pt.nonVibrato`); 2: Con vibrato (`pt.conVibrato`); 3: Vibrato (`pt.vibrato`); 4: Wide vibrato (`pt.wideVibrato`) | No |
| tremolo | CC115 | 0 | 1: Tremolo (`pt.tremolo`) | No |
| trill | CC116 | 0 | 1: Trill (`pt.trill`); 2: Trill (half-step) (`pt.trill.half`); 3: Trill (whole step) (`pt.trill.whole`) | No |
| dynamic_level | CC118 | 0 | 1: ppp (`pt.ppp`); 2: pp (`pt.pp`); 3: p (`pt.p`); 4: mp (`pt.mp`); 5: mf (`pt.mf`); 6: f (`pt.f`); 7: ff (`pt.ff`); 8: fff (`pt.fff`) | No |
| dynamic_attack | CC119 | 0 | 1: ffp (`pt.ffp`); 2: fp (`pt.fp`); 3: pf (`pt.pf`); 4: sf (`pt.sf`); 5: sfp (`pt.sfp`); 6: sfz (`pt.sfz`) | No |
| dynamic_shape | KS0 | 1 | 2: Crescendo (`pt.crescendo`); 3: Diminuendo (`pt.diminuendo`); 4: Crescendo-diminuendo (`pt.crescendoDiminuendo`); 5: Diminuendo-crescendo (`pt.diminuendoCrescendo`); 6: Volume swell (`pt.volumeSwell`) | No |
| fade_in | KS1 | 1 | 2: Fade in (`pt.fadeIn`) | No |
| fade_out | KS2 | 1 | 2: Fade out (`pt.fadeOut`) | No |
| soft | KS3 | 1 | 2: Soft (`pt.soft`) | No |
| ghost_note | KS4 | 1 | 2: Ghost note (`pt.ghostNote`) | No |
| tremblement | KS5 | 1 | 2: Tremblement (`pt.tremblement`) | No |
| glissando | KS9 | 1 | 2: Glissando up (`pt.glissandoUp`); 3: Glissando down (`pt.glissandoDown`) | No |
| portamento | KS10 | 1 | 2: Portamento up (`pt.portamentoUp`); 3: Portamento down (`pt.portamentoDown`) | No |
| simile | KS14 | 1 | 2: Simile (`pt.simile`) | No |
| tonguing | CC106 | 0 | 1: Double-tongue (`pt.doubleTongue`); 2: Triple-tongue (`pt.tripleTongue`) | No |
| flutter_tongue | CC107 | 0 | 1: Flutter-tongue (`pt.flutterTongue`) | No |
| growl | CC108 | 0 | 1: Growl (`pt.growl`) | No |
| multiphonic | CC109 | 0 | 1: Multiphonic (`pt.multiphonic`) | No |
| alternative_fingering | CC110 | 0 | 1: Alternative fingering (`pt.alternativeFingering`) | No |
| mute | CC114 | 0 | 1: Muted (`pt.muted`); 2: Open 1 (`pt.open1`); 3: Half-muted (`pt.halfMuted`); 4: Bell mute (`pt.bellMute`); 5: Bucket mute (`pt.bucketMute`); 6: Cup mute (`pt.cupMute`); 7: Straight mute (`pt.straightMute`); 8: Harmon mute, stem extended (`pt.harmonMuteStemExtended`); 9: Harmon mute, stem in (`pt.harmonMuteStemIn`); 10: Harmon mute, stem out (`pt.harmonMuteStemOut`) | No |
| plunger | CC113 | 0 | 1: Plunger mute (`pt.plungerMute`) | No |
| stopped | KS12 | 1 | 2: Stopped (`pt.stopped`) | No |
| cuivre | KS13 | 1 | 2: Cuivré (`pt.cuivre`) | No |
| mouthpiece_pop | CC111 | 0 | 1: Mouthpiece pop (`pt.mouthpiecePop`) | No |
| tongue_stop | CC112 | 0 | 1: Tongue stop (`pt.tongueStop`) | No |
| shake | KS15 | 1 | 2: Shake (`pt.shake`) | No |
| slide | KS16 | 1 | 2: Slide (`pt.slide`) | No |
| smear | KS17 | 1 | 2: Smear (`pt.smear`) | No |
| rip | KS18 | 1 | 2: Rip (`pt.rip`) | No |
| scoop | CC117 | 0 | 1: Scoop (`pt.scoop`) | No |
| plop | KS6 | 1 | 2: Plop (`pt.plop`) | No |
| fall | KS7 | 1 | 2: Fall (drop) (`pt.fallDrop`) | No |
| doit | KS8 | 1 | 2: Doit (`pt.doit`) | No |
| bend | KS11 | 1 | 2: Bend (`pt.bend`) | No |

## Grouping rationale

- **connection**: Connected versus detached phrasing; independent of stroke and articulation.
- **accent**: Accent strengths; keep separate from staccato and tenuto bases.
- **vibrato**: Four distinct canonical vibrato choices; unspecified is not vibrato.
- **tremolo**: Independent repetition, including tremolo plus sul ponticello.
- **trill**: Generic or specified interval; independent of vibrato and tremolo.
- **dynamic_level**: Steady named levels; not a replacement for continuous CC1/7/11.
- **dynamic_attack**: Alternative named compound/attack dynamics, separate from steady level.
- **dynamic_shape**: Alternative complete envelopes; not a quantized loudness curve.
- **fade_in**: Independent onset fade; may coexist with fade out.
- **fade_out**: Independent release fade.
- **soft**: Independent soft colour; not equated with a written dynamic level.
- **ghost_note**: Independent ghosting.
- **tremblement**: Preserved independently; not presumed identical to tremolo.
- **glissando**: Direction of a glissando; retained independently of portamento.
- **portamento**: Direction of portamento.
- **simile**: Transmit the explicit PT if emitted; does not itself encode previous state or restore a stack.
- **tonguing**: Double and triple articulation patterns are alternatives; independent of flutter tongue and note shortening.
- **flutter_tongue**: Flutter tonguing is its own dimension, not a global replacement for tonguing, vibrato, or mute state.
- **growl**: Independent rough/growling colour; no inferred equivalence to flutter tongue or multiphonics.
- **multiphonic**: Intent to produce multiphonics; no fingering, chord pitches or added notes are synthesized by this encoding.
- **alternative_fingering**: Alternate fingering/valve or slide-position intent, independent of multiphonics and ordinary articulation.
- **mute**: Inserted/generic mute choices including exact Harmon stem variants. Plunger is separate so it can coexist with an inserted mute.
- **plunger**: Independent external plunger, including plunger with straight mute. Explicit Open cancels it via an extra mutex group.
- **stopped**: Hand-stopped horn, independent of the inserted-mute enum. Explicit Open cancels it; no automatic pitch transposition is added.
- **cuivre**: Brassy/forced tone colour, especially horn; not synonymous with accent, stopped horn or louder dynamics.
- **mouthpiece_pop**: Mouthpiece percussive attack; independent of continuous tone modifiers.
- **tongue_stop**: Explicit tongue-stop effect; no guessed pitch or release synthesis.
- **shake**: Lip shake/trill gesture, independent of the explicit fingered trill interval PTs.
- **slide**: Trombone/valve slide gesture retained as its own exact PT; not silently aliased to Portamento or Glissando.
- **smear**: Brass smear, especially trombone; independent storage without a guessed combination rule.
- **rip**: Brass upward rip across partials; does not encode or generate intermediate pitches.
- **scoop**: Upward entry gesture; independent of a later exit gesture.
- **plop**: Downward entry gesture; preserved independently until generic/compound use is host-reviewed.
- **fall**: Falling exit gesture, including saxophone/trumpet effects; not an alias for glissando.
- **doit**: Rising exit gesture; independent of entry ornaments.
- **bend**: Embouchure/pitch-bending intent, not guitar pre-bending. Does not encode a continuous pitch curve.
