#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fiddle {

// A sample library's percussion map: Dorico's DrumKitNoteMapDefinition,
// instrument + technique set -> library pitch (and optional keyswitches).
// Design and matching rule: docs/library-percussion-maps.md.
struct PercussionMapEntry {
  std::string instrument; // Dorico instrument entity ID
  std::set<std::string> techniques;
  int pitch = 60;
  std::vector<int> keyswitches;
  std::string name, variant;
};

struct PercussionMap {
  std::string entityId, name;
  int version = 1;
  bool forSingleInstrument = true;
  std::vector<PercussionMapEntry> entries;

  // Every DrumKitNoteMapDefinition in a .doricolib (or a bare definition).
  static std::vector<PercussionMap> parseLibrary(const juce::String &xml, juce::String &error);
  static bool fromDefinition(const juce::XmlElement &definition, PercussionMap &out, juce::String &error);
  std::unique_ptr<juce::XmlElement> toDefinition() const;
  juce::String toDefinitionXml() const;
  // A complete .doricolib holding just this map, for Dorico.
  juce::String toLibraryXml() const;

  juce::var toJson() const;
  static bool fromJson(const juce::var &json, PercussionMap &out, juce::String &error);
  // Empty when valid.
  juce::StringArray validate() const;

  std::set<std::string> instruments() const;

  struct Match {
    const PercussionMapEntry *entry = nullptr;
    bool instrumentFallback = false; // single-instrument map on another chair
  };
  // Dorico's rule: entries for the instrument (exact, then alias-stripped,
  // then a single-instrument map's own), Natural implied on both sides,
  // largest subset wins, ties by entry order.
  Match resolve(const std::string &instrument, const std::set<std::string> &techniques) const;
};

// The single map in a stored definition (or library) XML, or null.
std::shared_ptr<const PercussionMap> compilePercussionMap(const juce::String &xml);

// The technique Dorico treats as an instrument's Natural (instruments.xml
// naturalEquivalent), or empty.
std::string percussionNaturalEquivalent(const std::string &instrument);
bool isRollTechnique(const std::string &technique);
std::string joinTechniques(const std::set<std::string> &techniques);

} // namespace fiddle
