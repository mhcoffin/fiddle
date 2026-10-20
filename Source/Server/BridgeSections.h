#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace fiddle {

// The four orchestral bridge profiles. Every other instrument keeps the
// universal Fiddle map. Membership is an explicit list of Dorico instrument
// entity IDs (stems match their keyed variants and aliases), following each
// profile's documented instrument coverage, not Dorico's broad sound families:
// Dorico files timpani under drums and band/folk instruments under brass/wind.
enum class BridgeSection { None, Strings, Woodwinds, Brass, PitchedPercussion };

struct BridgeSectionInfo {
  BridgeSection section;
  const char *key;          // stable project-settings key
  const char *label;
  const char *templateFile; // built-in map in Resources/BridgeMaps
};

inline const std::array<BridgeSectionInfo, 4> &bridgeSections() {
  static const std::array<BridgeSectionInfo, 4> sections{{
      {BridgeSection::Strings, "strings", "Orchestral strings", "Fiddle-Orchestral-Strings-r4.doricolib"},
      {BridgeSection::Woodwinds, "woodwinds", "Orchestral woodwinds", "Fiddle-Orchestral-Woodwinds-r2.doricolib"},
      {BridgeSection::Brass, "brass", "Orchestral brass", "Fiddle-Orchestral-Brass-r2.doricolib"},
      {BridgeSection::PitchedPercussion, "pitched-percussion", "Orchestral pitched percussion",
       "Fiddle-Orchestral-PitchedPercussion-r1.doricolib"},
  }};
  return sections;
}

inline const BridgeSectionInfo *bridgeSectionInfo(BridgeSection section) {
  for (const auto &info : bridgeSections())
    if (info.section == section) return &info;
  return nullptr;
}

inline BridgeSection bridgeSectionForInstrument(const juce::String &entityId) {
  struct Rule { const char *stem; BridgeSection section; };
  // Exclusions first: they share a stem with an included instrument.
  static const char *excluded[] = {
      "instrument.wind.flute.bansuri", "instrument.wind.flute.pan", "instrument.brass.horn.tenor",
      "instrument.wind.saxophone.sopranino", "instrument.wind.saxophone.soprillo",
      "instrument.wind.saxophone.mezzo", "instrument.wind.saxophone.c-melody",
      "instrument.wind.saxophone.contrabass", "instrument.wind.saxophone.subcontrabass"};
  static const Rule rules[] = {
      {"instrument.strings.violin", BridgeSection::Strings},
      {"instrument.strings.viola", BridgeSection::Strings},
      {"instrument.strings.violoncello", BridgeSection::Strings},
      {"instrument.strings.contrabass", BridgeSection::Strings},
      {"instrument.wind.piccolo", BridgeSection::Woodwinds},
      {"instrument.wind.flute", BridgeSection::Woodwinds},
      {"instrument.wind.oboe", BridgeSection::Woodwinds},
      {"instrument.wind.coranglais", BridgeSection::Woodwinds},
      {"instrument.wind.heckelphone", BridgeSection::Woodwinds},
      {"instrument.wind.clarinet", BridgeSection::Woodwinds},
      {"instrument.wind.basset-horn", BridgeSection::Woodwinds},
      {"instrument.wind.bassoon", BridgeSection::Woodwinds},
      {"instrument.wind.contrabassoon", BridgeSection::Woodwinds},
      {"instrument.wind.saxophone", BridgeSection::Woodwinds},
      {"instrument.brass.horn", BridgeSection::Brass},
      {"instrument.brass.tuba", BridgeSection::Brass},
      {"instrument.brass.trumpet", BridgeSection::Brass},
      {"instrument.brass.cornet", BridgeSection::Brass},
      {"instrument.brass.flugelhorn", BridgeSection::Brass},
      {"instrument.brass.trombone", BridgeSection::Brass},
      {"instrument.brass.euphonium", BridgeSection::Brass},
      {"instrument.brass.cimbasso", BridgeSection::Brass},
      {"instrument.pitchedpercussion.timpani", BridgeSection::PitchedPercussion},
      {"instrument.pitchedpercussion.glockenspiel", BridgeSection::PitchedPercussion},
      {"instrument.pitchedpercussion.crotales", BridgeSection::PitchedPercussion},
      {"instrument.pitchedpercussion.xylophone", BridgeSection::PitchedPercussion},
      {"instrument.pitchedpercussion.xylorimba", BridgeSection::PitchedPercussion},
      {"instrument.pitchedpercussion.marimba", BridgeSection::PitchedPercussion},
      {"instrument.pitchedpercussion.vibraphone", BridgeSection::PitchedPercussion},
      {"instrument.pitchedpercussion.bells.tubular", BridgeSection::PitchedPercussion},
  };
  const auto matches = [&entityId](const char *stem) {
    const juce::String s(stem);
    return entityId == s || entityId.startsWith(s + ".");
  };
  for (const auto *stem : excluded)
    if (matches(stem)) return BridgeSection::None;
  for (const auto &rule : rules)
    if (matches(rule.stem)) return rule.section;
  return BridgeSection::None;
}

} // namespace fiddle
