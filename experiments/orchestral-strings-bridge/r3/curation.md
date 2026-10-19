# Strings r3 — curation decisions (2026-10-16)

Goal: cover Dorico built-in PTs relevant to bowed orchestral strings without
allocating an independent conduit to every imaginable guitar/jazz effect.
Guitar gets a later bridge; customization can opt into deferred vocabulary.
These are explicit default-scope decisions, not claims of physical impossibility.
No new mutex relationships or aliases were invented to reduce the budget.

## Removed dimensions

Each row removes one independent binary dimension and its On/Off/Init commands.
All 17 removed dimensions were singleton flags.

| Dimension | Dorico PT | Reason |
| --- | --- | --- |
| shake | `pt.shake` | Jazz shake gesture; default strings retain the explicit trill and tremblement PTs. Revisit in another profile or customization. |
| slide | `pt.slide` | Generic slide gesture deferred; default strings cover explicit Glissando and Portamento IDs without inventing aliases. |
| smear | `pt.smear` | Generic jazz pitch effect deferred to another profile or customization. |
| scoop | `pt.scoop` | Jazz entry gesture deferred to another profile or customization. |
| plop | `pt.plop` | Jazz entry gesture deferred to another profile or customization. |
| fall | `pt.fallDrop` | Jazz ending gesture deferred to another profile or customization. |
| doit | `pt.doit` | Jazz ending gesture deferred to another profile or customization. |
| bend | `pt.bend` | Generic bend gesture deferred to the guitar profile/customization; explicit string Glissando/Portamento retained. |
| dip_bend | `pt.dipbend` | Guitar/jazz bend variant deferred to another profile or customization. |
| slight_bend | `pt.slightBend` | Guitar/jazz bend variant deferred to another profile or customization. |
| pre_bend | `pt.preBend` | Guitar pre-bend gesture deferred to the future guitar profile/customization. |
| flip | `pt.flip` | Generic jazz pitch effect deferred to another profile or customization. |
| jazz_turn | `pt.jazzTurn` | Jazz ornament deferred; not part of the default orchestral-strings vocabulary. |
| rip | `pt.rip` | Generic jazz pitch effect deferred to another profile or customization. |
| squeeze | `pt.squeeze` | Generic jazz pitch effect deferred to another profile or customization. |
| muffled_damped | `pt.muffledDamped` | Generic percussion damping variant omitted; strings retain Muffled strings, Damp and Laissez vibrer as distinct IDs. |
| release | `pt.release` | Generic percussion release intent omitted; no distinct bowed-string meaning established for this PT. Available for future customization. |

## Removed alternatives within retained groups

| Exact PT | Reason |
| --- | --- |
| `pt.guitarPizzicato` | Guitar-specific pizzicato PT deferred to the future guitar profile; string Pizzicato and Pizzicato alla chitarra retained separately. |
| `pt.pinchHarmonic` | Guitar pinch-harmonic production deferred to the future guitar profile; bowed-string harmonic modes retained. |

Removing these two alternatives saves switch rows but no additional conduit.
Both excitation and harmonic production retain their other alternatives.

## Deliberately retained boundary cases

- **Nail pizzicato:** an established bowed-string effect; VSL documents a nail-based pizzicato in its orchestral string sections. The source supports keeping a nail-pizzicato category, not a guarantee that every library uses Dorico’s ID identically. [VSL Duality Strings FX](https://www.vsl.co.at/de/instruments/synchron/duality-strings-fx).
- **Strum up/down and pizzicato alla chitarra:** retained for strummed pizzicato on bowed strings. Spitfire explicitly provides strummed pizzicato for violin and viola sections. Mapping these Dorico IDs to that intent is our profile decision, not a claim of matching vendor PT IDs. [Hans Zimmer Strings manual, pp. 14 and 23](https://spitfire-www-assets.s3.amazonaws.com/manuals/HansZimmerStrings_UserManual.pdf).
- **Buzz pizzicato, left-hand pizzicato, special bowing and string attacks:** retained as string technique vocabulary. No new conduit is needed for individual pizzicato alternatives within the excitation enum.
- **Glissando and portamento:** retain their exact direction-specific built-in IDs. Excluded Slide/Smear/bend IDs are not silently substituted or treated as aliases.
- **Damp, Muffled strings and Laissez vibrer:** keep explicit damping/muffling/ringing intent; exclude generic Muffled/Damped and Release controls whose distinct string meaning was not established.
- **Dynamics, fades, Soft, Ghost note, Tremblement and Simile:** retained as applicable general musical intent. Simile remains only an explicit marker, not a previous-state stack. Dynamic/ornament PT coverage does not replace Dorico performance MIDI.

## Result and compatibility

49→32 add-on dimensions; 50→33 total conduits; 106→87 add-on rows; 110→91
distinct encoded PT IDs. CC use stays at 18; keyswitch use falls from 32 to 15.
There are 19 spare pool entries instead of 2. Base articulation/length encoding
and reviewed mutex memberships for retained PTs remain unchanged. Canonical IDs
are preserved exactly; no excluded PT gets a guessed fallback.

The compact r3 allocation has a new identity and needs a matching future decoder.
Existing r2 maps, manifests and raw captures remain frozen. R3 has no host
acceptance or runtime integration yet.
