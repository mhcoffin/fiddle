# Orchestral woodwinds r1

[Dorico map](Fiddle-Orchestral-Woodwinds-r1.doricolib) ·
[Allocation](allocation.md) · [Manifest](allocation.json) ·
[All 298 PT dispositions](inventory.md)

Display name: **Fiddle Bridge - Orchestral Woodwinds r1 - INSPECTION ONLY**.
35 add-on dimensions plus one base; 36/52 conduits, 18 CCs and 18 keyswitch
pitches (KS0–17), 16 spare. There are 25 base rows, 64 add-on rows, 68 distinct
encoded PT IDs and 14 explicit mutex groups.

## Instruments and vocabulary

| Instrument family covered | Context-specific additions |
| --- | --- |
| Piccolo, concert/alto/bass flutes | Overblown harmonics, jet whistle, whistle tones, keyed/tongue effects |
| Oboe, oboe d'amore, English horn, bass oboe/heckelphone | Tonguing, alternate fingerings, multiphonics, keyed effects where applicable |
| E-flat/B-flat/A clarinets, basset horn, bass/contrabass clarinets | Slap tongue, key clicks, multiphonics, growl, pitch travel where applicable |
| Bassoon, contrabassoon | Keyed/pitched clicks, tongue effects, alternate fingerings and multiphonics |
| Soprano/alto/tenor/baritone/bass saxophones in orchestral use | Slaps, growl, key noises, bends and falls |

All share connection, accent, vibrato, tremolo/trill intent, explicit dynamic
levels/attacks/shapes, fades, soft/ghosted notes, tremblement, glissando/portamento,
Simile, double/triple and flutter tonguing. The profile includes generic Muted/Open
and independent entry/exit/pitch effects. It is a section vocabulary union, not a
promise that every effect is possible on every named instrument.

The Natural harmonic 1/2 built-in IDs are retained for flute harmonics, with the
same wire values as strings; their numbering is not interpreted as a partial
number. No artificial, half-, harp or guitar pinch harmonic is included.
Key click and Key click with pitch are proposed alternatives in one dimension.
Jet whistle, Whistle tone, Tongue click, Mouthpiece pop and Tongue stop remain
independent; the map does not synthesize attacks or modify pitch for them.

Brass mute subtypes, stopped horn and cuivré are excluded. Vocal whisper/whistle,
mouth shapes and finger-click foley are not implicitly turned into breath-tone
or key-click commands. Guitar mechanisms and percussion controls are excluded.
Every omission is visible in [inventory.md](inventory.md).

## Review and status

This is an importable inspection candidate, locally checked but **not host-tested
or runtime-compatible**. For inspection, use a disposable Dorico project, Library >
Expression Maps > Import Library, and select this map. Review the 25 original
conditional bases and the representative tonguing, harmonic, key-click and pitch
gesture actions. Import alone does not assign an endpoint. Do not assign to
production Fiddle or a VSTi; no compatible decoder exists.

Use one voice per routed endpoint/port/channel. Common values match strings r3
and brass r1, but each profile owns its identity and section-only allocation.
The candidate command pool excludes musical notes outside MIDI21–114. The range
contract still needs endpoint/host validation, including extreme/transposed
instrument ranges; subcontrabass and other unlisted instruments are deferred.
The eventual decoder must consume command notes and releases before VSTi routing.

Off clears only its own dimension. Natural does not globally clear sustained
modifiers, mutes or dynamics. Mid-passage initialization, replacement, expiry,
explicit cancellation and new mutex groups still need bounded host acceptance.
Prior strings captures do not establish woodwind behavior.

See the [shared design, sources and regeneration instructions](../README.md).
