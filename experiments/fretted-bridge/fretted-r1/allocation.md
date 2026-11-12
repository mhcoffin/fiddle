# Fretted instruments r1 — generated allocation

Wire schema 2 candidate: compiled by Fiddle, **not yet host-verified**. See [README](README.md) for scope, cancellation policy and caveats.

**39/50 conduits: 18 CCs + 21 KS pitches; 11 spare.** 25 bases + 87 add-ons = 112 switch rows, plus one Init switch (41 actions, including the two beacon keyswitches). 91 distinct encoded IDs + 0 built-in alias; 21 explicit mutex groups.

All pitch numbers are MIDI integers, not octave labels. CC value 0 means unspecified except CC102 (base code 0); KS velocity 1 means unspecified. Physical KS releases are not dimension resets. Every add-on Off writes its typed value On+64: it clears the dimension only while that technique is current. Init writes the unspecified value. Init also sends the revision beacon KS126 velocity 65 and KS127 velocity 91; those pitches are reserved and carry no technique.

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
| tremblement | KS5 | 1 | 2: Tremblement (`pt.tremblement`) | No |
| muffled_strings | KS6 | 1 | 2: Muffled strings (`pt.muffledStrings`) | No |
| glissando | KS9 | 1 | 2: Glissando up (`pt.glissandoUp`); 3: Glissando down (`pt.glissandoDown`) | No |
| portamento | KS10 | 1 | 2: Portamento up (`pt.portamentoUp`); 3: Portamento down (`pt.portamentoDown`) | No |
| strum | KS11 | 1 | 2: Strum up (`pt.strumUp`); 3: Strum down (`pt.strumDown`) | No |
| damp | KS12 | 1 | 2: Damp (`pt.damp`) | No |
| laissez_vibrer | KS13 | 1 | 2: Laissez vibrer (`pt.laissezVibrer`) | No |
| simile | KS14 | 1 | 2: Simile (`pt.simile`) | No |
| excitation | CC106 | 0 | 1: Plucked (`pt.plucked`); 2: Pizzicato (`pt.pizzicato`); 3: Bartok/snap pizzicato (`pt.bartoksnapPizzicato`); 4: Buzz pizzicato (`pt.buzzPizzicato`); 5: Nail pizzicato (`pt.nailPizzicato`); 6: Pizzicato alla chitarra (`pt.pizzicatoAllaChitarra`); 7: Pizzicato secco (`pt.pizzicatoSecco`); 8: Guitar pizzicato (`pt.guitarPizzicato`) | Proposed yes |
| harmonic | CC113 | 0 | 1: Artificial harmonic (`pt.artificialHarmonic`); 2: Natural harmonic 1 (`pt.naturalHarmonic1`); 3: Natural harmonic 2 (`pt.naturalHarmonic2`); 4: Half-harmonic (`pt.halfHarmonic`); 5: Pinch harmonic (`pt.pinchHarmonic`); 6: Harp harmonic (`pt.harpHarmonic`) | No |
| bend | CC107 | 0 | 1: Bend (`pt.bend`); 2: Slight bend (`pt.slightBend`); 3: Dip/bend (`pt.dipbend`) | No |
| pre_bend | CC108 | 0 | 1: Pre-bend (`pt.preBend`) | No |
| vibrato_bar | CC109 | 0 | 1: Vibrato bar dip (`pt.vibratoBarDip`); 2: Vibrato bar scoop (`pt.vibratoBarScoop`) | No |
| vibrato_bar_line | CC110 | 0 | 1: Vibrato bar line (`pt.vibratoBarLine`) | No |
| palm_muting | CC111 | 0 | 1: Palm muting (`pt.palmMuting`) | No |
| tapping | CC112 | 0 | 1: Left-hand tap (`pt.leftHandTap`); 2: Right hand tap (`pt.rightHandTap`); 3: Right-hand tap (tapping) (`pt.rightHandTapTapping`) | No |
| slide | KS7 | 1 | 2: Slide (`pt.slide`) | No |
| pick_scrape | KS8 | 1 | 2: Pick scrape (`pt.pickScrape`) | No |
| skank | KS15 | 1 | 2: Skank (`pt.skank`) | No |
| drive | KS16 | 1 | 2: Overdrive (`pt.overdrive`); 3: Distortion (`pt.distortion`) | No |
| modulation | KS17 | 1 | 2: Chorus (`pt.chorus`); 3: Flange (`pt.flange`); 4: Phaser (`pt.phaser`) | No |
| delay | KS18 | 1 | 2: Delay (`pt.delay`) | No |
| wah_pedal | KS19 | 1 | 2: Wah-wah (`pt.wahWahGuitar`) | No |
| wah_position | KS20 | 1 | 2: Wah-wah open (`pt.wahWahOpen`); 3: Wah-wah stopped (`pt.wahWahStopped`) | No |

## Grouping rationale

- **connection**: Connected versus detached phrasing; independent of stroke and articulation.
- **accent**: Accent strengths; keep separate from staccato and tenuto bases.
- **vibrato**: Four distinct canonical vibrato choices; unspecified is not vibrato.
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
- **tremblement**: Preserved independently; not presumed identical to tremolo.
- **muffled_strings**: String-specific muffling; not collapsed with generic damping.
- **glissando**: Direction of a glissando; retained independently of portamento.
- **portamento**: Direction of portamento.
- **strum**: Directional strummed pizzicato; distinct from arpeggio.
- **damp**: Independent damping request for ringing strings.
- **laissez_vibrer**: Independent ringing request; no guessed relationship to note length.
- **simile**: Transmit the explicit PT if emitted; does not itself encode previous state or restore a stack.
- **excitation**: Plucking/pizzicato method. The strings values keep their numbers (Plucked, Pizzicato, Bartók/snap, buzz, nail, alla chitarra, secco); Guitar pizzicato is appended. Explicit Natural returns to the normal stroke.
- **harmonic**: Harmonic production. The strings values keep their numbers (artificial, natural 1/2, half); pinch and harp harmonics are appended.
- **bend**: Bend shape of one note: full bend, slight (blues) bend, or dip. Dorico may also play a notated bend as pitch bend; Fiddle must not bend twice.
- **pre_bend**: Pre-bent attack, independent of the bend that may release it.
- **vibrato_bar**: Vibrato (whammy) bar gesture on one note: dip or scoop.
- **vibrato_bar_line**: Vibrato bar line: a sustained whammy-bar passage, independent of single dips and scoops.
- **palm_muting**: Palm muting (P.M. line). Independent of the String mute dimension, strumming and harmonics; ended by its own line, not by Natural.
- **tapping**: Tapped note: left-hand tap or one of the two right-hand tap IDs (kept distinct, not aliased).
- **slide**: Slide into or between notes, distinct from the explicit Glissando and Portamento IDs.
- **pick_scrape**: Pick scrape along the wound strings.
- **skank**: Skank: the clipped off-beat rhythm-guitar stroke.
- **drive**: Amp/pedal gain: overdrive or distortion. Alternatives; independent of modulation and delay.
- **modulation**: Modulation effect: chorus, flanger or phaser, one at a time.
- **delay**: Delay/echo effect, independent of drive and modulation.
- **wah_pedal**: Wah-wah pedal engaged (direction), independent of the per-note open/stopped marks.
- **wah_position**: Wah-wah open (o) or stopped (+) on one note.
