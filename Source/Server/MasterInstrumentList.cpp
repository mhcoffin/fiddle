#include "MasterInstrumentList.h"
#include <iostream>

namespace fiddle {

MasterInstrumentList::MasterInstrumentList() {}

juce::File MasterInstrumentList::getDefaultFile() {
  return juce::File::getSpecialLocation(
             juce::File::userApplicationDataDirectory)
      .getChildFile("Antigravity/FiddleServer/master_instruments.json");
}

bool MasterInstrumentList::load(const juce::File &file) {
  if (!file.existsAsFile()) {
    std::cerr << "[MasterList] File not found: " << file.getFullPathName()
              << std::endl;
    return false;
  }

  juce::String content = file.loadFileAsString();
  return setSlotsFromJson(content);
}

bool MasterInstrumentList::save(const juce::File &file) const {
  // Ensure parent directory exists
  file.getParentDirectory().createDirectory();

  juce::String json = getSlotsAsJson();
  if (!file.replaceWithText(json)) {
    std::cerr << "[MasterList] Failed to write: " << file.getFullPathName()
              << std::endl;
    return false;
  }

  std::cerr << "[MasterList] Saved " << slots_.size() << " instrument slots to "
            << file.getFullPathName() << std::endl;
  return true;
}

bool MasterInstrumentList::loadDefault() { return load(getDefaultFile()); }

bool MasterInstrumentList::saveDefault() const {
  return save(getDefaultFile());
}

const std::vector<MasterInstrumentList::EnsembleSlot> &
MasterInstrumentList::getSlots() const {
  return slots_;
}

bool MasterInstrumentList::setSlotsFromJson(const juce::String &json) {
  slots_.clear();

  auto parsed = juce::JSON::parse(json);
  if (!parsed.isArray()) {
    std::cerr << "[MasterList] JSON is not an array" << std::endl;
    return false;
  }

  auto *arr = parsed.getArray();
  if (!arr)
    return false;

  for (const auto &item : *arr) {
    if (auto *obj = item.getDynamicObject()) {
      EnsembleSlot slot;
      slot.entityID = obj->getProperty("entityID").toString();
      slot.name = obj->getProperty("name").toString();
      slot.musicXMLSoundID = obj->getProperty("musicXMLSoundID").toString();
      slot.family = obj->getProperty("family").toString();

      // Backward-compatible: missing counts default to 1
      auto soloProp = obj->getProperty("soloCount");
      auto sectionProp = obj->getProperty("sectionCount");
      slot.soloCount = soloProp.isVoid() ? 1 : (int)soloProp;
      slot.sectionCount = sectionProp.isVoid() ? 1 : (int)sectionProp;

      if (slot.entityID.isNotEmpty()) {
        slots_.push_back(std::move(slot));
      }
    }
  }

  std::cerr << "[MasterList] Loaded " << slots_.size()
            << " instrument slots from JSON (" << totalSlotCount()
            << " total channels)" << std::endl;
  return true;
}

juce::String MasterInstrumentList::getSlotsAsJson() const {
  juce::Array<juce::var> arr;

  for (const auto &slot : slots_) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("entityID", slot.entityID);
    obj->setProperty("name", slot.name);
    obj->setProperty("musicXMLSoundID", slot.musicXMLSoundID);
    obj->setProperty("family", slot.family);
    obj->setProperty("soloCount", slot.soloCount);
    obj->setProperty("sectionCount", slot.sectionCount);
    arr.add(juce::var(obj));
  }

  return juce::JSON::toString(juce::var(arr), true);
}

std::vector<juce::String> MasterInstrumentList::getEntityIds() const {
  std::vector<juce::String> ids;
  ids.reserve(slots_.size());
  for (const auto &s : slots_)
    ids.push_back(s.entityID);
  return ids;
}

bool MasterInstrumentList::isEmpty() const { return slots_.empty(); }

int MasterInstrumentList::size() const { return (int)slots_.size(); }

int MasterInstrumentList::totalSlotCount() const {
  int total = 0;
  for (const auto &s : slots_)
    total += s.soloCount + s.sectionCount;
  return total;
}

// Convert integer to Roman numeral string.
static juce::String toRoman(int n) {
  static const std::pair<int, const char *> table[] = {
      {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
  juce::String result;
  for (auto &[val, sym] : table) {
    while (n >= val) {
      result += sym;
      n -= val;
    }
  }
  return result;
}

juce::String MasterInstrumentList::getChannelMapAsJson() const {
  // Count solos and sections per base name to decide if numbering is needed.
  std::map<juce::String, int> soloTotals, sectionTotals;
  for (const auto &slot : slots_) {
    soloTotals[slot.name] += slot.soloCount;
    sectionTotals[slot.name] += slot.sectionCount;
  }

  // Build entries with Dorico-style labels:
  //   Solo:    "Violin 1", "Violin 2"  (arabic, only if >1 solo)
  //   Section: "Violin I", "Violin II" (roman,  only if >1 section)
  std::map<juce::String, int> soloCounters, sectionCounters;
  juce::Array<juce::var> arr;
  int flatIndex = 0;

  for (const auto &slot : slots_) {
    for (int i = 0; i < slot.soloCount; ++i) {
      int num = ++soloCounters[slot.name];
      juce::String label = slot.name;
      if (soloTotals[slot.name] > 1)
        label += " " + juce::String(num);

      auto *obj = new juce::DynamicObject();
      obj->setProperty("port", flatIndex / 16);
      obj->setProperty("channel", flatIndex % 16);
      obj->setProperty("name", slot.name);
      obj->setProperty("label", label);
      obj->setProperty("family", slot.family);
      obj->setProperty("isSolo", true);
      arr.add(juce::var(obj));
      ++flatIndex;
    }
    for (int i = 0; i < slot.sectionCount; ++i) {
      int num = ++sectionCounters[slot.name];
      juce::String label = slot.name;
      if (sectionTotals[slot.name] > 1)
        label += " " + toRoman(num);

      auto *obj = new juce::DynamicObject();
      obj->setProperty("port", flatIndex / 16);
      obj->setProperty("channel", flatIndex % 16);
      obj->setProperty("name", slot.name);
      obj->setProperty("label", label);
      obj->setProperty("family", slot.family);
      obj->setProperty("isSolo", false);
      arr.add(juce::var(obj));
      ++flatIndex;
    }
  }

  return juce::JSON::toString(juce::var(arr), true);
}

} // namespace fiddle
