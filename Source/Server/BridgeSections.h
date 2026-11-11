#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace fiddle {

// The five orchestral bridge profiles. Every other instrument keeps the
// universal Fiddle map. Membership is an explicit list of Dorico instrument
// entity IDs (stems match their keyed variants and aliases), following each
// profile's documented instrument coverage, not Dorico's broad sound families:
// Dorico files timpani under drums and band/folk instruments under brass/wind.
enum class BridgeSection { None, Strings, Woodwinds, Brass, PitchedPercussion, UnpitchedPercussion };

struct BridgeSectionInfo {
  BridgeSection section;
  const char *key;          // stable project-settings key
  const char *label;
  const char *templateFile; // built-in map in Resources/BridgeMaps
};

inline const std::array<BridgeSectionInfo, 5> &bridgeSections() {
  static const std::array<BridgeSectionInfo, 5> sections{{
      {BridgeSection::Strings, "strings", "Orchestral strings", "Fiddle-Orchestral-Strings-r5.doricolib"},
      {BridgeSection::Woodwinds, "woodwinds", "Orchestral woodwinds", "Fiddle-Orchestral-Woodwinds-r3.doricolib"},
      {BridgeSection::Brass, "brass", "Orchestral brass", "Fiddle-Orchestral-Brass-r3.doricolib"},
      {BridgeSection::PitchedPercussion, "pitched-percussion", "Orchestral pitched percussion",
       "Fiddle-Orchestral-PitchedPercussion-r2.doricolib"},
      // Single unpitched instruments, including every instrument of
      // Dorico's kits; kit IDs (instrument.kit.*) are not mapped, because a
      // kit's instruments have chairs of their own (docs/percussion-kits.md).
      // The stems must match the percussion map's instrument list
      // (experiments/unpitched-percussion-bridge/profiles.py); a test keeps them so.
      {BridgeSection::UnpitchedPercussion, "unpitched-percussion", "Orchestral unpitched percussion",
       "Fiddle-Orchestral-UnpitchedPercussion-r3.doricolib"},
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
      {"instrument.band.unpitched.floortom.high", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.floortom.low", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.roto.tom.high", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.roto.tom.low", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.roto.tom.medium", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.roto.tom.medium-high", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.roto.tom.medium-low", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.tomtom.high", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.tomtom.low", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.tomtom.medium", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.tomtom.medium-high", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.tomtom.medium-low", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.bongo.bell.high", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.bongo.bell.low", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.bongo.high", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.bongo.low", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.conga.high", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.conga.low", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.conga.medium", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.conga.very.high", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.conga.very.low", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.anvil", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.bass.drum", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.belltree", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.clash.cymbals", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.crash.cymbal.high", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.crash.cymbal.low", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.hihat", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.large.gong", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.opera.gong", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.ratchet", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.ride.cymbal.high", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.ride.cymbal.low", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.side.drum", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.sizzle.cymbal", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.sleigh.bells", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.small.gong", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.snare.drum", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.suspended.cymbal", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.tamtam", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.temple.high", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.temple.low", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.temple.medium", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.tenor.drum", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.thundersheet", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.triangle", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.whip", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.whistle", BridgeSection::UnpitchedPercussion},
      {"instrument.orchestral.unpitched.wind.gong", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.cowbell.high", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.cowbell.low", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.tambourine", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.wood.block.high", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.wood.block.low", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.wood.block.medium", BridgeSection::UnpitchedPercussion},
      {"instrument.unpitched.china.cymbal", BridgeSection::UnpitchedPercussion},
      {"instrument.unpitched.flexatone", BridgeSection::UnpitchedPercussion},
      {"instrument.unpitched.kick.drum.low", BridgeSection::UnpitchedPercussion},
      {"instrument.unpitched.kick.drum.very.low", BridgeSection::UnpitchedPercussion},
      {"instrument.unpitched.ribboncrasher", BridgeSection::UnpitchedPercussion},
      {"instrument.unpitched.splash.cymbal", BridgeSection::UnpitchedPercussion},
      // Unpitched r3: the instruments of Dorico's kits that r2 lacked.
      {"instrument.band.marching.tenor.drum.10inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.marching.tenor.drum.12inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.marching.tenor.drum.13inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.marching.tenor.drum.14inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.marching.tenor.drum.6inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.marching.tenor.drum.8inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.bass.drum.14inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.bass.drum.16inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.bass.drum.20inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.bass.drum.24inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.bass.drum.28inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.bass.drum.32inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.bass.drum.ensemble", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.cymbals.16inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.cymbals.16inch.ensemble", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.cymbals.18inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.cymbals.18inch.ensemble", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.cymbals.20inch", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.cymbals.20inch.ensemble", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.hihat", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.ride.cymbal", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.snare.drum", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.snare.drum.ensemble.snares-off", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.snare.drum.ensemble.snares-on", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.snare.drum.kevlar", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.snare.drum.rim", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.snare.drum.solo.snares-off", BridgeSection::UnpitchedPercussion},
      {"instrument.band.unpitched.marching.snare.drum.solo.snares-on", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.agogo.high", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.agogo.low", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.cajon.high", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.cajon.high.alias.box-high-tone", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.cajon.low", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.cajon.low.alias.box-low-tone", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.cuica.high", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.cuica.low", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.maraca.high", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.maraca.low", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.timbale.medium.high", BridgeSection::UnpitchedPercussion},
      {"instrument.latin.unpitched.timbale.medium.low", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.jam.block.high", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.jam.block.low", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.jam.block.medium", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.jam.block.medium-high", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.jam.block.medium-low", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.kempyang", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.ketuk", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.tabla.baiya", BridgeSection::UnpitchedPercussion},
      {"instrument.percussion.other.tabla.daiya", BridgeSection::UnpitchedPercussion},
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
