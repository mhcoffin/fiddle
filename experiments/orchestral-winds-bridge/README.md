# Woodwind and brass bridge maps — r2 current, r1 frozen

**Current (2026-10-19): [Woodwinds r2](woodwinds-r2/README.md) and
[Brass r2](brass-r2/README.md)** re-encode r1 unchanged on wire schema 2
(typed Off values and an Init revision beacon on KS126/127). They are the
Bridge maps starting templates. r1 below stays frozen as schema-1 evidence.
`generate_winds.py` writes and checks both revisions.

Created 2026-10-16 under the [section-profile plan](../../docs/orchestral-bridge-maps.md).
These are two independent, importable Dorico expression maps, with exact built-in
PT IDs. Duplication is deliberate: both encode the common musical vocabulary,
then extend it with their section's techniques.

| Candidate | Add-on dimensions | Total conduits | CCs / KS pitches | Spare of 52 | Add-on rows | Encoded PT IDs | Explicit mutex groups |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| [Woodwinds r1](woodwinds-r1/README.md) | 35 | 36 | 18 / 18 | 16 | 64 | 68 | 14 |
| [Brass r1](brass-r1/README.md) | 36 | 37 | 18 / 19 | 15 | 71 | 75 | 14 |

Every map also has the original 25 articulation/length base rows on CC102 and
one Init switch. Total conduits include the base dimension; PT IDs include the
four base PTs. There are no source aliases added in these sections. Each generated
inventory accounts for all 298 recorded Dorico built-in PT IDs, including every
excluded ID with a visible reason.

**One Dorico voice per endpoint/port/channel.** These are inspection candidates,
not matched runtime map/decoder pairs. No new map has been installed, assigned,
host-tested, or integrated into Fiddle. The machine-readable allocation is the
future decoder contract, not an implemented decoder. Existing strings and live
Dorico configuration are unchanged.

## Encoding and musical choices

- All PTs shared with strings r3 retain their conduit, reset and named value.
  The additional PTs shared between woodwinds and brass also match exactly.
  Section-only dimensions reuse available conduits independently; absent
  dimensions consume no action, initialization command or allocation.
- Each independent dimension owns one CC number or one velocity-coded keyswitch
  pitch. Enums share a dimension only for alternatives of the same property.
  Off clears only its own dimension; physical keyswitch release is not a logical
  reset. Unspecified differs from explicit Non vibrato, Non legato and Open.
- Double/triple tonguing are alternatives. Flutter tongue, growl, multiphonics
  and fingering intent are independent. No blanket mutex group combines all
  wind effects. Multiphonic intent carries no fingering, chord or extra pitches.
- Brass has an inserted/generic-mute enum and a separate plunger dimension,
  allowing inserted mute plus plunger. Stopped horn and cuivré are separate too.
  Explicit Open cancels Stopped and Plunger through two additional groups.
  Natural is not a global reset and does not automatically remove mutes.
- Common dynamics and applicable entry/exit/pitch gestures are included. A scoop
  can precede a fall without global cancellation. Continuous dynamics, pitch
  curves, trill realization and other performance data may already be rendered
  by Dorico; a future decoder must not perform them twice.
- Guitar-only mechanisms, percussion controls, electronics, generic vocal
  production and speculative mouth-shape encodings are excluded. Saxophones,
  trumpet falls and trombone effects are section-relevant even though some were
  excluded from the strings default. Rare further gestures remain customization
  candidates rather than a controller for every imaginable effect.

These are explicit profile decisions, not claims that every instrument/library
supports every PT or combination. Instrument-specific layer coverage still needs
auditing. Cancellation, compound selection and all new groups remain subject
to host acceptance within the one-voice scope.

## Scope evidence

VSL documents woodwind flutter tonguing, interval trills, repetitions and
multiphonics ([Studio Woodwinds](https://www.vsl.co.at/instruments/synchronized/woodwinds)),
and bassoon keyed/pitched effects, slap tongue and multiphonics
([Bassoon Academy](https://www.vsl.co.at/academy/woodwinds/bassoon)).
Its saxophone collection explicitly covers orchestral use plus bends, falls,
slaps, key noises and growling
([Studio Saxophones](https://www.vsl.co.at/instruments/synchronized/saxophones)).
Flutist Helen Bledsoe's guide documents jet whistles and key clicks
([Composing for Flute](https://helenbledsoe.com/Composing%20for%20Flute%20advice%20and%20warnings.pdf)).

For brass, VSL distinguishes stopped horn, open playing, brassy tone and mutes
([Horn Academy](https://www.vsl.co.at/academy/brass/horn-f)),
and documents flutter tonguing and directional brass glissandos
([Synchron Brass](https://www.vsl.co.at/instruments/synchron/brass)).
Its notation-integration guide names saxophone/trumpet falls and trumpet slides
([Woodwind and Brass guide](https://preview.vsl.co.at/tutorials/guides/sibelius-integration/woodwind-brass)).
These sources support musical relevance; they do not establish Dorico PT
activation, identical vendor IDs, universal technique availability or map acceptance.

## Reproduce

From the repository root:

```sh
python3 experiments/orchestral-winds-bridge/generate_winds.py
python3 experiments/orchestral-winds-bridge/generate_winds.py --check
python3 experiments/orchestral-winds-bridge/generate_winds.py --check --dorico-catalog '/Applications/Dorico 6.app/Contents/Resources/playingTechniqueDefinitions.xml'
python3 -m unittest discover -s experiments/orchestral-winds-bridge -p 'test_*.py' -v
```

Use `--section woodwinds` or `--section brass` to generate/check just one profile.
Generation uses the same XML/validation engine as strings, preserving strings
artifact bytes. Tests cover counts, source IDs, base-row preservation, scope
boundaries, distinct identities, shared encoding, groups and action/reset consistency.
Inspection instructions are in each section's README. No new playback fixture is
required simply to review these candidates.
