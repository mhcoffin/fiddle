#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <map>
#include <vector>

namespace fiddle {

/// Dorico's percussion kit definitions (kitDefinitions.xml in Dorico's
/// resources): each kit's instruments, in the kit's own order. A kit
/// instrument in instruments.xml names its definition with
/// <percussionKitDefinitionID>. Fiddle uses them as presets for kits of
/// ordinary chairs (docs/percussion-kits.md).
using KitDefinitions = std::map<juce::String, std::vector<juce::String>>;

inline KitDefinitions parseKitDefinitions(const juce::XmlElement &root) {
  KitDefinitions result;
  std::vector<const juce::XmlElement *> stack{&root};
  while (!stack.empty()) {
    const auto *element = stack.back();
    stack.pop_back();
    if (element->hasTagName("PercussionKitDefinitionDefinition")) {
      const auto id = element->getChildElementAllSubText("entityID", {}).trim();
      std::vector<std::pair<int, juce::String>> components;
      if (const auto *kit = element->getChildByName("kitDefinition"))
        if (const auto *list = kit->getChildByName("percussionKitComponents"))
          for (const auto *component : list->getChildWithTagNameIterator("percussionKitComponent")) {
            const auto instrument = component->getChildElementAllSubText("instrumentEntityID", {}).trim();
            if (instrument.isNotEmpty())
              components.emplace_back(component->getChildElementAllSubText("instrumentOrder", "0").getIntValue(),
                                      instrument);
          }
      std::stable_sort(components.begin(), components.end(),
                       [](const auto &a, const auto &b) { return a.first < b.first; });
      if (id.isNotEmpty() && !components.empty()) {
        auto &instruments = result[id];
        for (const auto &[order, instrument] : components) instruments.push_back(instrument);
      }
      continue;
    }
    for (const auto *child : element->getChildIterator()) stack.push_back(child);
  }
  return result;
}

/// The kit definition an instruments.xml entity names, or empty.
inline juce::String kitDefinitionIdOf(const juce::XmlElement &instrument) {
  if (const auto *direct = instrument.getChildByName("percussionKitDefinitionID"))
    return direct->getAllSubText().trim();
  std::vector<const juce::XmlElement *> stack{&instrument};
  while (!stack.empty()) {
    const auto *element = stack.back();
    stack.pop_back();
    if (element->hasTagName("percussionKitDefinitionID")) return element->getAllSubText().trim();
    for (const auto *child : element->getChildIterator()) stack.push_back(child);
  }
  return {};
}

} // namespace fiddle
