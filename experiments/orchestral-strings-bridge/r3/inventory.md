# Orchestral strings r3 — full built-in inventory disposition

All 298 exact IDs from the recorded Dorico 6.2 inventory are accounted for. The family classification and section scope are our review decisions, not a Dorico-supplied instrument compatibility guarantee. No custom PT definitions are imported.

Coverage means a switch can transmit the ID **if Dorico activates it**; it does not establish that a particular score symbol or continuous dynamic emits that PT. Out-of-scope entries are visible for review, not silently discarded for capacity.

| Exact ID | Label | Kind | Disposition | Encoding / reason |
| --- | --- | --- | --- | --- |
| `pt.accent` | Accent | A | Add-on | accent |
| `pt.alternativeFingering` | Alternative fingering | A | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.arpeggioDown` | Arpeggio down | A | Add-on | arpeggio |
| `pt.arpeggioUp` | Arpeggio up | A | Add-on | arpeggio |
| `pt.artificialHarmonic` | Artificial harmonic | A | Add-on | harmonic |
| `pt.backstick` | Backstick | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.bartoksnapPizzicato` | Bartok/snap pizzicato | A | Add-on | excitation |
| `pt.bassTone` | Bass tone | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.beaterDreads` | Dreads (beater) | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.beaterFelt` | Felt (beater) | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.beaterPuffy` | Puffy (beater) | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.beaterRods` | Rods (beater) | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.bell` | Bell | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.bellMute` | Bell mute | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.bend` | Bend | A | Out of scope | Generic bend gesture deferred to the guitar profile/customization; explicit string Glissando/Portamento retained. |
| `pt.bowOnBridge` | Bow on bridge | D | Add-on | bow_contact |
| `pt.bowOnTailpiece` | Bow on tailpiece | D | Add-on | bow_contact |
| `pt.bowed` | Bowed | D | Alias | Bowed aliases pt.natural in the built-in catalog; no distinct wire value |
| `pt.bucketMute` | Bucket mute | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.buzzPizzicato` | Buzz pizzicato | A | Add-on | excitation |
| `pt.buzzRoll` | Buzz roll | A | Out of scope | Percussion roll variant; string repetition is represented by pt.tremolo. |
| `pt.centre` | Centre | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.choke` | Choke | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.chorus` | Chorus | A | Out of scope | Electronic/FX control outside this acoustic technique profile. |
| `pt.circularRoll` | Circular roll | A | Out of scope | Percussion roll variant; string repetition is represented by pt.tremolo. |
| `pt.click` | Click | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.closed` | Closed | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.closedEnd` | Closed end | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.closedHole` | Closed hole | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.colLegnoBattuto` | Col legno battuto | D | Add-on | bow_material |
| `pt.colLegnoTratto` | Col legno tratto | D | Add-on | bow_material |
| `pt.colle` | Collé | D | Add-on | bow_stroke |
| `pt.conVibrato` | Con vibrato | D | Add-on | vibrato |
| `pt.crescendo` | Crescendo | D | Add-on | dynamic_shape |
| `pt.crescendoDiminuendo` | Crescendo-diminuendo | D | Add-on | dynamic_shape |
| `pt.crossStick` | Cross stick | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.crunch` | Crunch | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.crunchChoke` | Crunch choke | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.crush` | Crush | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.crushDry` | Crush (dry) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.crushFat` | Crush (fat) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.cuivre` | Cuivré | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.cupMute` | Cup mute | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.damp` | Damp | A | Add-on | damp |
| `pt.deadStroke` | Dead stroke | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.delay` | Delay | A | Out of scope | Electronic/FX control outside this acoustic technique profile. |
| `pt.detache` | Détaché | D | Add-on | bow_stroke |
| `pt.detacheLance` | Détaché lancé | D | Add-on | bow_stroke |
| `pt.diminuendo` | Diminuendo | D | Add-on | dynamic_shape |
| `pt.diminuendoCrescendo` | Diminuendo-crescendo | D | Add-on | dynamic_shape |
| `pt.ding` | Ding | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.dipbend` | Dip/bend | A | Out of scope | Guitar/jazz bend variant deferred to another profile or customization. |
| `pt.distortion` | Distortion | A | Out of scope | Electronic/FX control outside this acoustic technique profile. |
| `pt.doit` | Doit | A | Out of scope | Jazz ending gesture deferred to another profile or customization. |
| `pt.doubleShotOverTheHeader` | Over the head double shot | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.doubleTongue` | Double-tongue | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.down` | Down | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.downbow` | Downbow | A | Add-on | bow_direction |
| `pt.edgeCymbals` | Edge (cymbals) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.edgeDrums` | Edge (drums) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.edgeRebound` | Edge rebound | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.f` | f | D | Add-on | dynamic_level |
| `pt.fadeIn` | Fade in | A | Add-on | fade_in |
| `pt.fadeOut` | Fade out | A | Add-on | fade_out |
| `pt.fallDrop` | Fall (drop) | A | Out of scope | Jazz ending gesture deferred to another profile or customization. |
| `pt.ff` | ff | D | Add-on | dynamic_level |
| `pt.fff` | fff | D | Add-on | dynamic_level |
| `pt.ffp` | ffp | D | Add-on | dynamic_attack |
| `pt.fingerClick` | Finger click | A | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.flange` | Flange | A | Out of scope | Electronic/FX control outside this acoustic technique profile. |
| `pt.flatCrash` | Flat crash | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.flatRoll` | Flat roll | A | Out of scope | Percussion roll variant; string repetition is represented by pt.tremolo. |
| `pt.flautando` | Flautando | D | Add-on | bow_pressure |
| `pt.flip` | Flip | A | Out of scope | Generic jazz pitch effect deferred to another profile or customization. |
| `pt.flutterTongue` | Flutter-tongue | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.foot` | Foot | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.fouette` | Fouetté | A | Add-on | bow_stroke |
| `pt.fp` | fp | A | Add-on | dynamic_attack |
| `pt.frame` | Frame | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.frictionSlide1` | Friction slide 1 | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.frictionSlide2` | Friction slide 2 | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.ghostNote` | Ghost note | A | Add-on | ghost_note |
| `pt.glissandoDown` | Glissando down | A | Add-on | glissando |
| `pt.glissandoUp` | Glissando up | A | Add-on | glissando |
| `pt.growl` | Growl | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.guitarPizzicato` | Guitar pizzicato | A | Out of scope | Guitar-specific pizzicato PT deferred to the future guitar profile; string Pizzicato and Pizzicato alla chitarra retained separately. |
| `pt.halfHarmonic` | Half-harmonic | A | Add-on | harmonic |
| `pt.halfMuted` | Half-muted | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.halfOpen` | Half-open | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.harmonMuteStemExtended` | Harmon mute, stem extended | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.harmonMuteStemIn` | Harmon mute, stem in | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.harmonMuteStemOut` | Harmon mute, stem out | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.harnessHit` | Dress harness hit | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.harpHarmonic` | Harp harmonic | D | Out of scope | Harp-specific; harp is outside the bowed-orchestral-strings profile. |
| `pt.hiHat` | Hi-hat | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.hitBowl` | Bowl hit | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.hitCage` | Cage hit | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.hitLoose` | Hit (loose) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.hitMedium` | Hit (medium) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.hitOverTheHead` | Over the head hit | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.hitTight` | Hit (tight) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.jazzTurn` | Jazz turn | A | Out of scope | Jazz ornament deferred; not part of the default orchestral-strings vocabulary. |
| `pt.jetWhistle` | Jet whistle | A | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.jete` | Jeté | A | Add-on | bow_stroke |
| `pt.keyClick` | Key click | A | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.keyClickWithPitch` | Key click with pitch | A | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.laissezVibrer` | Laissez vibrer | A | Add-on | laissez_vibrer |
| `pt.left` | Left (LH) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.leftHandTap` | Left-hand tap | A | Out of scope | Guitar tapping vocabulary; dedicated finger/body-tapping extension is outside this first profile. |
| `pt.legato` | Legato | D | Add-on | connection |
| `pt.lhPizzicato` | LH pizzicato | A | Add-on | left_hand_pizzicato |
| `pt.loose` | Loose | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.marcato` | Marcato | A | Add-on | accent |
| `pt.martele` | Martelé | D | Add-on | bow_stroke |
| `pt.martellato` | Martellato | D | Add-on | bow_stroke |
| `pt.mf` | mf | D | Add-on | dynamic_level |
| `pt.motorOff` | Motor off | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.motorOn` | Motor on | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.mouthClosed` | Mouth closed | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.mouthOpen` | Mouth open | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.mouthPursed` | Mouth pursed | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.mouthSlightlyOpen` | Mouth slightly open | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.mouthWideOpen` | Mouth wide open | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.mouthpiecePop` | Mouthpiece pop | A | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.mp` | mp | D | Add-on | dynamic_level |
| `pt.muffledDamped` | Muffled/Damped | A | Out of scope | Generic percussion damping variant omitted; strings retain Muffled strings, Damp and Laissez vibrer as distinct IDs. |
| `pt.muffledStrings` | Muffled strings | A | Add-on | muffled_strings |
| `pt.multiphonic` | Multiphonic | A | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.muted` | Muted | D | Add-on | mute |
| `pt.nail` | Nail | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.nailPizzicato` | Nail pizzicato | A | Add-on | excitation |
| `pt.nasalVoice` | Nasal voice | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.natural` | Natural | D | Base | CC102; original articulation/length encoding |
| `pt.naturalHarmonic1` | Natural harmonic 1 | A | Add-on | harmonic |
| `pt.naturalHarmonic2` | Natural harmonic 2 | A | Add-on | harmonic |
| `pt.nonLegato` | Non legato | D | Add-on | connection |
| `pt.nonVibrato` | Non vibrato | D | Add-on | vibrato |
| `pt.open1` | Open 1 | D | Add-on | mute |
| `pt.open2` | Open 2 | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.openEnd` | Open end | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.openHole` | Open hole | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.overdrive` | Overdrive | A | Out of scope | Electronic/FX control outside this acoustic technique profile. |
| `pt.overpressure` | Overpressure | D | Add-on | bow_pressure |
| `pt.p` | p | D | Add-on | dynamic_level |
| `pt.palmMuting` | Palm muting | D | Out of scope | Guitar palm-muting vocabulary; bowed-string damping has explicit separate entries. |
| `pt.pdlt` | P.d.l.t. | D | Add-on | bow_contact |
| `pt.pedal` | Pedal | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.pf` | pf | A | Add-on | dynamic_attack |
| `pt.phaser` | Phaser | A | Out of scope | Electronic/FX control outside this acoustic technique profile. |
| `pt.pickScrape` | Pick scrape | A | Out of scope | Guitar pick/scrape/skank vocabulary outside this profile. |
| `pt.pince` | Pincé | D | Add-on | pince |
| `pt.pinchHarmonic` | Pinch harmonic | D | Out of scope | Guitar pinch-harmonic production deferred to the future guitar profile; bowed-string harmonic modes retained. |
| `pt.ping` | Ping | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.pizzicato` | Pizzicato | D | Add-on | excitation |
| `pt.pizzicatoAllaChitarra` | Pizzicato alla chitarra | D | Add-on | excitation |
| `pt.pizzicatoSecco` | Pizzicato secco | D | Add-on | excitation |
| `pt.plop` | Plop | A | Out of scope | Jazz entry gesture deferred to another profile or customization. |
| `pt.plucked` | Plucked | D | Add-on | excitation |
| `pt.plungerMute` | Plunger mute | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.pop` | Pop | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.portCrash` | Port crash | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.portamentoDown` | Portamento down | A | Add-on | portamento |
| `pt.portamentoUp` | Portamento up | A | Add-on | portamento |
| `pt.portatoloure` | Portato/Louré | D | Add-on | bow_stroke |
| `pt.pp` | pp | D | Add-on | dynamic_level |
| `pt.ppp` | ppp | D | Add-on | dynamic_level |
| `pt.preBend` | Pre-bend | A | Out of scope | Guitar pre-bend gesture deferred to the future guitar profile/customization. |
| `pt.pressRoll` | Press roll | A | Out of scope | Percussion roll variant; string repetition is represented by pt.tremolo. |
| `pt.pull` | Pull | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.push` | Push | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.rake` | Rake | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.release` | Release | A | Out of scope | Generic percussion release intent omitted; no distinct bowed-string meaning established for this PT. Available for future customization. |
| `pt.ricochet` | Ricochet | A | Add-on | bow_stroke |
| `pt.right` | Right (RH) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.rightHandTap` | Right hand tap | A | Out of scope | Guitar tapping vocabulary; dedicated finger/body-tapping extension is outside this first profile. |
| `pt.rightHandTapTapping` | Right-hand tap (tapping) | A | Out of scope | Guitar tapping vocabulary; dedicated finger/body-tapping extension is outside this first profile. |
| `pt.rim` | Rim | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.rimKnock` | Rim knock | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.rimOnly` | Rim only | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.rimShot` | Rim shot | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.rip` | Rip | A | Out of scope | Generic jazz pitch effect deferred to another profile or customization. |
| `pt.roll` | Roll | A | Out of scope | Percussion roll variant; string repetition is represented by pt.tremolo. |
| `pt.saltando` | Saltando | D | Add-on | bow_stroke |
| `pt.sautille` | Sautillé | D | Add-on | bow_stroke |
| `pt.scoop` | Scoop | A | Out of scope | Jazz entry gesture deferred to another profile or customization. |
| `pt.scrape` | Scrape | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.scrapeLeft` | Scrape (left) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.scrapeRight` | Scrape (right) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.scratchIn` | Scratch in | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.scratchOut` | Scratch out | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.sf` | sf | A | Add-on | dynamic_attack |
| `pt.sfp` | sfp | A | Add-on | dynamic_attack |
| `pt.sfz` | sfz | A | Add-on | dynamic_attack |
| `pt.shake` | Shake | A | Out of scope | Jazz shake gesture; default strings retain the explicit trill and tremblement PTs. Revisit in another profile or customization. |
| `pt.shell` | Shell | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.shotOverTheHead` | Over the head shot | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.sideStick` | Side stick | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.simile` | Simile | D | Add-on | simile |
| `pt.sizzle` | Sizzle | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.sizzleSuck` | Sizzle suck | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.skank` | Skank | A | Out of scope | Guitar pick/scrape/skank vocabulary outside this profile. |
| `pt.skin` | Skin | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.slap` | Slap | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.slapCongas` | Slap (congas) | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.slapTongue` | Slap tongue | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.slide` | Slide | A | Out of scope | Generic slide gesture deferred; default strings cover explicit Glissando and Portamento IDs without inventing aliases. |
| `pt.slightBend` | Slight bend | A | Out of scope | Guitar/jazz bend variant deferred to another profile or customization. |
| `pt.smear` | Smear | A | Out of scope | Generic jazz pitch effect deferred to another profile or customization. |
| `pt.snareOff` | Snares off | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.snareOn` | Snares on | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.snenor` | Snenor | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.soft` | Soft | A | Add-on | soft |
| `pt.sonsXylophoniques` | Sons xylophoniques | D | Add-on | sons_xylophoniques |
| `pt.sostenutoPedal` | Sostenuto pedal | D | Out of scope | Keyboard/pedal mechanism outside this profile. |
| `pt.spiccato` | Spiccato | D | Add-on | bow_stroke |
| `pt.spoken` | Spoken | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.squeeze` | Squeeze | A | Out of scope | Generic jazz pitch effect deferred to another profile or customization. |
| `pt.staccatissimo` | Staccatissimo | A | Base | CC102; original articulation/length encoding |
| `pt.staccato` | Staccato | A | Base | CC102; original articulation/length encoding |
| `pt.standClick` | Stand click | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.stickButt` | Stick (butt) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickClick` | Stick click | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.stickShaft` | Stick (shaft) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickShaft1` | Stick (shaft 1) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickShaft2` | Stick (shaft 2) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickShaft3` | Stick (shaft 3) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickShaft4` | Stick (shaft 4) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickShaft5` | Stick (shaft 5) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickShaftOpen` | Stick (shaft open) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickShot` | Stick shot | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.stickShoulder` | Stick (shoulder) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickSnap` | Stick snap | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.stickTip` | Stick (tip) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickTip1` | Stick (tip 1) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickTip2` | Stick (tip 2) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickTip3` | Stick (tip 3) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickTip4` | Stick (tip 4) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.stickTip5` | Stick (tip 5) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.sticks` | Sticks | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.sticksIn` | Sticks in | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.stir` | Stir | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.stopped` | Stopped | A | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.straightMute` | Straight mute | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.strumDown` | Strum down | A | Add-on | strum |
| `pt.strumUp` | Strum up | A | Add-on | strum |
| `pt.suck` | Suck | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.sulPonticello` | Sul ponticello | D | Add-on | bow_contact |
| `pt.sulTasto` | Sul tasto | D | Add-on | bow_contact |
| `pt.sustainPedal` | Sustain pedal | A | Out of scope | Keyboard/pedal mechanism outside this profile. |
| `pt.swish` | Swish | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.swooshRoll` | Swoosh roll | A | Out of scope | Percussion roll variant; string repetition is represented by pt.tremolo. |
| `pt.tabla-ga` | Ga (open) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tabla-ge` | Ge (open) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tabla-gi` | Gi (muted open) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tabla-ke` | Ke (closed) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tabla-na` | Na (open) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tabla-ta` | Ta (muted open) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tabla-te` | Te (closed) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tabla-ti` | Ti (muted closed) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tabla-tu` | Tu (open) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tap` | Tap | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tapChoke` | Tap choke | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tapEdge` | Tap at edge | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tapHalfway` | Tap halfway | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tenuto` | Tenuto | A | Base | CC102; original articulation/length encoding |
| `pt.throw` | Throw | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.throwoffOff` | Throwoff off | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.throwoffOn` | Throwoff on | D | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.tongueClick` | Tongue click | A | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.tongueFingerClick` | Tongue and finger click | A | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.tongueStop` | Tongue stop | A | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.tremblement` | Tremblement | A | Add-on | tremblement |
| `pt.tremolo` | Tremolo | A | Add-on | tremolo |
| `pt.trill` | Trill | A | Add-on | trill |
| `pt.trill.half` | Trill (half-step) | A | Add-on | trill |
| `pt.trill.whole` | Trill (whole step) | A | Add-on | trill |
| `pt.tripleTongue` | Triple-tongue | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.unaCorda` | Una corda | D | Out of scope | Keyboard/pedal mechanism outside this profile. |
| `pt.up` | Up | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.upbow` | Upbow | A | Add-on | bow_direction |
| `pt.vacuumSuck` | Vacuum suck | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.vibrato` | Vibrato | D | Add-on | vibrato |
| `pt.vibratoBarDip` | Vibrato bar dip | A | Out of scope | Guitar vibrato-bar mechanism, not bowed orchestral strings. |
| `pt.vibratoBarLine` | Vibrato bar line | D | Out of scope | Guitar vibrato-bar mechanism, not bowed orchestral strings. |
| `pt.vibratoBarScoop` | Vibrato bar scoop | A | Out of scope | Guitar vibrato-bar mechanism, not bowed orchestral strings. |
| `pt.vocalDut` | Dut (vocal) | A | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.volumeSwell` | Volume swell | D | Add-on | dynamic_shape |
| `pt.wahWahGuitar` | Wah-wah | D | Out of scope | Electronic/FX control outside this acoustic technique profile. |
| `pt.wahWahOpen` | Wah-wah open | A | Out of scope | Electronic/FX control outside this acoustic technique profile. |
| `pt.wahWahStopped` | Wah-wah stopped | A | Out of scope | Electronic/FX control outside this acoustic technique profile. |
| `pt.whaleCall` | Whale call | A | Out of scope | Percussion-specific playing gestures outside this profile. |
| `pt.whisper` | Whisper | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.whistle` | Whistle | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.whistleTone` | Whistle tone | D | Out of scope | Wind/brass/voice-specific vocabulary outside this profile (generic muted/open explicitly included). |
| `pt.wideVibrato` | Wide vibrato | A | Add-on | vibrato |
| `pt.zing` | Zing | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.zingFast` | Zing (fast) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
| `pt.zingSlow` | Zing (slow) | A | Out of scope | Percussion-specific vocabulary outside this profile (generic ringing/damping explicitly included). |
