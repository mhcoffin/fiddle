#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace fiddle {

/**
 * Categories for grouping instruments in the Dorico setup UI.
 */
enum class InstrumentCategory {
  Strings,
  Woodwinds,
  Brass,
  Percussion,
  Keyboards,
  Plucked,
  Voices
};

inline juce::String categoryToString(InstrumentCategory cat) {
  switch (cat) {
  case InstrumentCategory::Strings:
    return "Strings";
  case InstrumentCategory::Woodwinds:
    return "Woodwinds";
  case InstrumentCategory::Brass:
    return "Brass";
  case InstrumentCategory::Percussion:
    return "Percussion";
  case InstrumentCategory::Keyboards:
    return "Keyboards";
  case InstrumentCategory::Plucked:
    return "Plucked";
  case InstrumentCategory::Voices:
    return "Voices";
  }
  return "Unknown";
}

/**
 * Represents an orchestral instrument with its Dorico entity ID mapping.
 *
 * A single preset (e.g., "Fiddle_Clarinet") may be shared by multiple
 * Dorico entity IDs (all clarinet variants use the same sound).
 */
struct DoricoInstrument {
  juce::String commonName; // e.g., "Clarinet"
  juce::String presetName; // e.g., "Fiddle_Clarinet" (used in presets.xml)
  InstrumentCategory category;

  // All Dorico entity IDs that should map to this preset.
  // The first one is the "primary" ID.
  std::vector<juce::String> doricoEntityIds;
};

/**
 * Returns the default set of orchestral instruments supported by Fiddle.
 * Each instrument includes its Dorico entity IDs for automatic assignment.
 *
 * Entity IDs follow Dorico's dot-notation convention, e.g.:
 *   instrument.strings.violin
 *   instrument.wind.flute
 */
inline std::vector<DoricoInstrument> getDefaultInstruments() {
  return {
      // ── Strings ──────────────────────────────────────────────
      {"Violin",
       "Fiddle_Violin",
       InstrumentCategory::Strings,
       {"instrument.strings.violin"}},

      {"Viola",
       "Fiddle_Viola",
       InstrumentCategory::Strings,
       {"instrument.strings.viola"}},

      {"Cello",
       "Fiddle_Cello",
       InstrumentCategory::Strings,
       {"instrument.strings.violoncello"}},

      {"Contrabass",
       "Fiddle_Contrabass",
       InstrumentCategory::Strings,
       {"instrument.strings.contrabass",
        "instrument.strings.contrabass.alias.contrabass"}},

      {"Harp",
       "Fiddle_Harp",
       InstrumentCategory::Strings,
       {"instrument.strings.harp"}},

      // ── Woodwinds ────────────────────────────────────────────
      {"Piccolo",
       "Fiddle_Piccolo",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.flute.piccolo", "instrument.wind.piccolo"}},

      {"Flute",
       "Fiddle_Flute",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.flute", "instrument.wind.flute.alto",
        "instrument.wind.flute.bass"}},

      {"Oboe",
       "Fiddle_Oboe",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.oboe"}},

      {"English Horn",
       "Fiddle_EnglishHorn",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.corAnglais"}},

      {"Clarinet",
       "Fiddle_Clarinet",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.clarinet", "instrument.wind.clarinet.bflat",
        "instrument.wind.clarinet.bflat.alias.soprano-clarinet",
        "instrument.wind.clarinet.a",
        "instrument.wind.clarinet.a.alias.soprano-clarinet",
        "instrument.wind.clarinet.c",
        "instrument.wind.clarinet.c.alias.soprano-clarinet",
        "instrument.wind.clarinet.d",
        "instrument.wind.clarinet.d.alias.soprano-clarinet",
        "instrument.wind.clarinet.eflat",
        "instrument.wind.clarinet.eflat.alias.sopranino-clarinet",
        "instrument.wind.clarinet.aflat",
        "instrument.wind.clarinet.aflat.alias.piccolo-clarinet",
        "instrument.wind.clarinet.aflat.alias.sopranino-clarinet",
        "instrument.wind.clarinet.alto.eflat",
        "instrument.wind.clarinet.alto.eflat.bassclef"}},

      {"Bass Clarinet",
       "Fiddle_BassClarinet",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.clarinet.bass", "instrument.wind.clarinet.bass.bflat",
        "instrument.wind.clarinet.bass.bflat.bassclef",
        "instrument.wind.clarinet.bass.bflat.8vb",
        "instrument.wind.clarinet.bass.a"}},

      {"Bassoon",
       "Fiddle_Bassoon",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.bassoon"}},

      {"Contrabassoon",
       "Fiddle_Contrabassoon",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.contrabassoon"}},

      {"Alto Saxophone",
       "Fiddle_AltoSax",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.saxophone.alto",
        "instrument.wind.saxophone.alto.eflat"}},

      {"Tenor Saxophone",
       "Fiddle_TenorSax",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.saxophone.tenor",
        "instrument.wind.saxophone.tenor.bflat"}},

      {"Baritone Saxophone",
       "Fiddle_BaritoneSax",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.saxophone.baritone"}},

      {"Soprano Saxophone",
       "Fiddle_SopranoSax",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.saxophone.soprano"}},

      {"Recorder",
       "Fiddle_Recorder",
       InstrumentCategory::Woodwinds,
       {"instrument.wind.recorder", "instrument.wind.recorder.soprano",
        "instrument.wind.recorder.alto", "instrument.wind.recorder.tenor",
        "instrument.wind.recorder.bass"}},

      // ── Brass ────────────────────────────────────────────────
      {"Trumpet",
       "Fiddle_Trumpet",
       InstrumentCategory::Brass,
       {"instrument.brass.trumpet",
        "instrument.brass.trumpet.bflat",
        "instrument.brass.trumpet.bflat.nokeysig",
        "instrument.brass.trumpet.c",
        "instrument.brass.trumpet.c.nokeysig",
        "instrument.brass.trumpet.a",
        "instrument.brass.trumpet.a.nokeysig",
        "instrument.brass.trumpet.b",
        "instrument.brass.trumpet.b.nokeysig",
        "instrument.brass.trumpet.d",
        "instrument.brass.trumpet.d.nokeysig",
        "instrument.brass.trumpet.e",
        "instrument.brass.trumpet.e.nokeysig",
        "instrument.brass.trumpet.eflat",
        "instrument.brass.trumpet.eflat.nokeysig",
        "instrument.brass.trumpet.f",
        "instrument.brass.trumpet.f.nokeysig",
        "instrument.brass.trumpet.piccolo.a",
        "instrument.brass.trumpet.piccolo.bflat",
        "instrument.brass.trumpet.baroque.bflat",
        "instrument.brass.trumpet.tenor.eflat"}},

      {"French Horn",
       "Fiddle_FrenchHorn",
       InstrumentCategory::Brass,
       {"instrument.brass.horn", "instrument.brass.horn.f",
        "instrument.brass.horn.a.alto", "instrument.brass.horn.bflat.alto",
        "instrument.brass.horn.bflat.basso", "instrument.brass.horn.c.alto",
        "instrument.brass.horn.c.basso", "instrument.brass.horn.d",
        "instrument.brass.horn.eflat", "instrument.brass.horn.eflat.alto",
        "instrument.brass.horn.e", "instrument.brass.horn.e.alto",
        "instrument.brass.horn.g"}},

      {"Trombone",
       "Fiddle_Trombone",
       InstrumentCategory::Brass,
       {"instrument.brass.trombone", "instrument.brass.trombone.tenor"}},

      {"Bass Trombone",
       "Fiddle_BassTrombone",
       InstrumentCategory::Brass,
       {"instrument.brass.trombone.bass"}},

      {"Tuba",
       "Fiddle_Tuba",
       InstrumentCategory::Brass,
       {"instrument.brass.tuba", "instrument.brass.tuba.bass",
        "instrument.brass.tuba.alias.bass-tuba"}},

      {"Cornet",
       "Fiddle_Cornet",
       InstrumentCategory::Brass,
       {"instrument.brass.cornet"}},

      {"Flugelhorn",
       "Fiddle_Flugelhorn",
       InstrumentCategory::Brass,
       {"instrument.brass.flugelhorn"}},

      {"Euphonium",
       "Fiddle_Euphonium",
       InstrumentCategory::Brass,
       {"instrument.brass.euphonium"}},

      // ── Percussion ──────────────────────────────────────────
      {"Timpani",
       "Fiddle_Timpani",
       InstrumentCategory::Percussion,
       {"instrument.percussion.timpani", "instrument.pitchedpercussion.timpani",
        "instrument.pitchedpercussion.timpani.nokey"}},

      {"Glockenspiel",
       "Fiddle_Glockenspiel",
       InstrumentCategory::Percussion,
       {"instrument.percussion.glockenspiel",
        "instrument.pitchedpercussion.glockenspiel"}},

      {"Xylophone",
       "Fiddle_Xylophone",
       InstrumentCategory::Percussion,
       {"instrument.percussion.xylophone",
        "instrument.pitchedpercussion.xylophone"}},

      {"Marimba",
       "Fiddle_Marimba",
       InstrumentCategory::Percussion,
       {"instrument.percussion.marimba",
        "instrument.pitchedpercussion.marimba"}},

      {"Vibraphone",
       "Fiddle_Vibraphone",
       InstrumentCategory::Percussion,
       {"instrument.percussion.vibraphone",
        "instrument.pitchedpercussion.vibraphone"}},

      {"Tubular Bells",
       "Fiddle_TubularBells",
       InstrumentCategory::Percussion,
       {"instrument.percussion.tubularBells",
        "instrument.pitchedpercussion.tubularBells"}},

      {"Crotales",
       "Fiddle_Crotales",
       InstrumentCategory::Percussion,
       {"instrument.percussion.crotales",
        "instrument.pitchedpercussion.crotales"}},

      // ── Keyboards ───────────────────────────────────────────
      {"Piano",
       "Fiddle_Piano",
       InstrumentCategory::Keyboards,
       {"instrument.keyboard.piano"}},

      {"Celesta",
       "Fiddle_Celesta",
       InstrumentCategory::Keyboards,
       {"instrument.keyboard.celesta"}},

      {"Harpsichord",
       "Fiddle_Harpsichord",
       InstrumentCategory::Keyboards,
       {"instrument.keyboard.harpsichord"}},

      {"Organ",
       "Fiddle_Organ",
       InstrumentCategory::Keyboards,
       {"instrument.keyboard.organ", "instrument.keyboard.organ.pipe",
        "instrument.keyboard.organ.reed"}},

      {"Accordion",
       "Fiddle_Accordion",
       InstrumentCategory::Keyboards,
       {"instrument.keyboard.accordion"}},

      // ── Plucked ─────────────────────────────────────────────
      {"Acoustic Guitar",
       "Fiddle_AcousticGuitar",
       InstrumentCategory::Plucked,
       {"instrument.pluckedStrings.guitar",
        "instrument.pluckedStrings.guitar.nylonString",
        "instrument.pluckedStrings.guitar.steelString"}},

      {"Electric Guitar",
       "Fiddle_ElectricGuitar",
       InstrumentCategory::Plucked,
       {"instrument.pluckedStrings.guitar.electric"}},

      {"Bass Guitar",
       "Fiddle_BassGuitar",
       InstrumentCategory::Plucked,
       {"instrument.pluckedStrings.bassGuitar",
        "instrument.pluckedStrings.bassGuitar.electric"}},

      {"Banjo",
       "Fiddle_Banjo",
       InstrumentCategory::Plucked,
       {"instrument.pluckedStrings.banjo"}},

      {"Mandolin",
       "Fiddle_Mandolin",
       InstrumentCategory::Plucked,
       {"instrument.pluckedStrings.mandolin"}},

      {"Ukulele",
       "Fiddle_Ukulele",
       InstrumentCategory::Plucked,
       {"instrument.pluckedStrings.ukulele"}},

      // ── Voices ──────────────────────────────────────────────
      {"Soprano",
       "Fiddle_Soprano",
       InstrumentCategory::Voices,
       {"instrument.voice.soprano"}},

      {"Alto",
       "Fiddle_Alto",
       InstrumentCategory::Voices,
       {"instrument.voice.alto"}},

      {"Tenor",
       "Fiddle_Tenor",
       InstrumentCategory::Voices,
       {"instrument.voice.tenor"}},

      {"Bass",
       "Fiddle_BassVoice",
       InstrumentCategory::Voices,
       {"instrument.voice.bass"}},

      {"Choir",
       "Fiddle_Choir",
       InstrumentCategory::Voices,
       {"instrument.voice.choir"}},
  };
}

} // namespace fiddle
