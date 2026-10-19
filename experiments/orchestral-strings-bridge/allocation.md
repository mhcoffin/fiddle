# Orchestral strings r2 — generated allocation

Inspection prototype, **not runtime compatible or host-verified**. See [README](README.md) for scope, cancellation policy and caveats.

**50/52 conduits: 18 CCs + 32 KS pitches; 2 spare.** 25 bases + 106 add-ons = 131 switch rows, plus one Init switch (50 actions). 110 distinct encoded IDs + 1 built-in alias; 22 explicit mutex groups.

All pitch numbers are MIDI integers, not octave labels. CC value 0 means unspecified except CC102 (base code 0); KS velocity 1 means unspecified. Physical KS releases are not dimension resets. Every add-on Off writes its own unspecified value.

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

| Dimension | Conduit | Off / Init | Named On values | Explicit nat. cancels? |
| --- | --- | --- | --- | --- |
| connection | CC103 | 0 | 1: Legato (`pt.legato`); 2: Non legato (`pt.nonLegato`) | No |
| accent | CC104 | 0 | 1: Accent (`pt.accent`); 2: Marcato (`pt.marcato`) | No |
| vibrato | CC105 | 0 | 1: Non vibrato (`pt.nonVibrato`); 2: Con vibrato (`pt.conVibrato`); 3: Vibrato (`pt.vibrato`); 4: Wide vibrato (`pt.wideVibrato`) | No |
| excitation | CC106 | 0 | 1: Plucked (`pt.plucked`); 2: Pizzicato (`pt.pizzicato`); 3: Bartok/snap pizzicato (`pt.bartoksnapPizzicato`); 4: Buzz pizzicato (`pt.buzzPizzicato`); 5: Guitar pizzicato (`pt.guitarPizzicato`); 6: Nail pizzicato (`pt.nailPizzicato`); 7: Pizzicato alla chitarra (`pt.pizzicatoAllaChitarra`); 8: Pizzicato secco (`pt.pizzicatoSecco`) | Proposed yes |
| left_hand_pizzicato | CC107 | 0 | 1: LH pizzicato (`pt.lhPizzicato`) | No |
| bow_material | CC108 | 0 | 1: Col legno battuto (`pt.colLegnoBattuto`); 2: Col legno tratto (`pt.colLegnoTratto`) | Proposed yes |
| bow_stroke | CC109 | 0 | 1: Collé (`pt.colle`); 2: Détaché (`pt.detache`); 3: Détaché lancé (`pt.detacheLance`); 4: Jeté (`pt.jete`); 5: Martellato (`pt.martellato`); 6: Martelé (`pt.martele`); 7: Portato/Louré (`pt.portatoloure`); 8: Ricochet (`pt.ricochet`); 9: Saltando (`pt.saltando`); 10: Sautillé (`pt.sautille`); 11: Spiccato (`pt.spiccato`); 12: Fouetté (`pt.fouette`) | Proposed yes |
| bow_contact | CC110 | 0 | 1: Sul ponticello (`pt.sulPonticello`); 2: Sul tasto (`pt.sulTasto`); 3: P.d.l.t. (`pt.pdlt`); 4: Bow on bridge (`pt.bowOnBridge`); 5: Bow on tailpiece (`pt.bowOnTailpiece`) | Proposed yes |
| bow_direction | CC111 | 0 | 1: Upbow (`pt.upbow`); 2: Downbow (`pt.downbow`) | No |
| bow_pressure | CC112 | 0 | 1: Flautando (`pt.flautando`); 2: Overpressure (`pt.overpressure`) | Proposed yes |
| harmonic | CC113 | 0 | 1: Artificial harmonic (`pt.artificialHarmonic`); 2: Natural harmonic 1 (`pt.naturalHarmonic1`); 3: Natural harmonic 2 (`pt.naturalHarmonic2`); 4: Half-harmonic (`pt.halfHarmonic`); 5: Pinch harmonic (`pt.pinchHarmonic`) | No |
| mute | CC114 | 0 | 1: Muted (`pt.muted`); 2: Open 1 (`pt.open1`) | No |
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
| shake | KS5 | 1 | 2: Shake (`pt.shake`) | No |
| tremblement | KS6 | 1 | 2: Tremblement (`pt.tremblement`) | No |
| muffled_strings | KS7 | 1 | 2: Muffled strings (`pt.muffledStrings`) | No |
| sons_xylophoniques | KS8 | 1 | 2: Sons xylophoniques (`pt.sonsXylophoniques`) | Proposed yes |
| pince | KS9 | 1 | 2: Pincé (`pt.pince`) | Proposed yes |
| glissando | KS10 | 1 | 2: Glissando up (`pt.glissandoUp`); 3: Glissando down (`pt.glissandoDown`) | No |
| portamento | KS11 | 1 | 2: Portamento up (`pt.portamentoUp`); 3: Portamento down (`pt.portamentoDown`) | No |
| slide | KS12 | 1 | 2: Slide (`pt.slide`) | No |
| smear | KS13 | 1 | 2: Smear (`pt.smear`) | No |
| scoop | KS14 | 1 | 2: Scoop (`pt.scoop`) | No |
| plop | KS15 | 1 | 2: Plop (`pt.plop`) | No |
| fall | KS16 | 1 | 2: Fall (drop) (`pt.fallDrop`) | No |
| doit | KS17 | 1 | 2: Doit (`pt.doit`) | No |
| bend | KS18 | 1 | 2: Bend (`pt.bend`) | No |
| dip_bend | KS19 | 1 | 2: Dip/bend (`pt.dipbend`) | No |
| slight_bend | KS20 | 1 | 2: Slight bend (`pt.slightBend`) | No |
| pre_bend | KS115 | 1 | 2: Pre-bend (`pt.preBend`) | No |
| flip | KS116 | 1 | 2: Flip (`pt.flip`) | No |
| jazz_turn | KS117 | 1 | 2: Jazz turn (`pt.jazzTurn`) | No |
| rip | KS118 | 1 | 2: Rip (`pt.rip`) | No |
| squeeze | KS119 | 1 | 2: Squeeze (`pt.squeeze`) | No |
| strum | KS120 | 1 | 2: Strum up (`pt.strumUp`); 3: Strum down (`pt.strumDown`) | No |
| damp | KS121 | 1 | 2: Damp (`pt.damp`) | No |
| muffled_damped | KS122 | 1 | 2: Muffled/Damped (`pt.muffledDamped`) | No |
| laissez_vibrer | KS123 | 1 | 2: Laissez vibrer (`pt.laissezVibrer`) | No |
| release | KS124 | 1 | 2: Release (`pt.release`) | No |
| simile | KS125 | 1 | 2: Simile (`pt.simile`) | No |

## Grouping rationale

- **connection**: Connected versus detached phrasing; independent of stroke and articulation.
- **accent**: Accent strengths; keep separate from staccato and tenuto bases.
- **vibrato**: Four distinct canonical vibrato choices; unspecified is not vibrato.
- **excitation**: Alternative plucking/pizzicato methods; bowed aliases Natural. Guitar-named pizzicato retained as a strings colour.
- **left_hand_pizzicato**: May coexist with a bowed right hand; not folded into excitation.
- **bow_material**: Struck versus drawn wood; separate from bow contact and stroke.
- **bow_stroke**: Alternative strokes, including Fouetté (user-reviewed in r2). Exact IDs retained even for near-synonyms; review host cancellation in Dorico.
- **bow_contact**: Proposed alternative contact regions; not exclusion with flautando or pressure.
- **bow_direction**: Direction of this stroke.
- **bow_pressure**: User-reviewed r2 alternatives on one pressure dimension; independent of contact point and stroke, so flautando plus sul tasto remains possible.
- **harmonic**: Proposed alternative harmonic production; includes pinch as an extended string technique, excludes harp-specific harmonic.
- **mute**: Con/senza sordino intent; explicit Open is not unspecified. Nat. must not remove a mute.
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
- **shake**: Preserved independently; not presumed identical to trill.
- **tremblement**: Preserved independently; not presumed identical to tremolo.
- **muffled_strings**: String-specific muffling; not collapsed with generic damping.
- **sons_xylophoniques**: Independent special string colour pending combination review.
- **pince**: Preserve this less-common exact string PT independently.
- **glissando**: Direction of a glissando; retained independently of portamento.
- **portamento**: Direction of portamento.
- **slide**: Independent generic sliding gesture; no invented alias to portamento.
- **smear**: Independent pitch gesture.
- **scoop**: Independent onset gesture; can coexist with an ending gesture.
- **plop**: Independent onset gesture.
- **fall**: Independent ending gesture.
- **doit**: Independent ending gesture.
- **bend**: Independent general bend; no assumed exclusion with other gestures.
- **dip_bend**: Independent compound gesture.
- **slight_bend**: Preserved independently until gesture combinations are reviewed.
- **pre_bend**: Independent pre-bend; may precede another pitch gesture.
- **flip**: Independent ornament.
- **jazz_turn**: Independent ornament; included for orchestral strings playing jazz idioms.
- **rip**: Independent ornament.
- **squeeze**: Independent ornament.
- **strum**: Directional strummed pizzicato; distinct from arpeggio.
- **damp**: Independent damping request for ringing strings.
- **muffled_damped**: Preserve generic damping separately from string-specific muffling.
- **laissez_vibrer**: Independent ringing request; no guessed relationship to note length.
- **release**: Independent release intent; no invented interpretation.
- **simile**: Transmit the explicit PT if emitted; does not itself encode previous state or restore a stack.
