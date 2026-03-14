#include "MasterInstrumentList.h"
#include "../FiddlePaths.h"
#include "FiddleDatabase.h"
#include <iostream>

namespace fiddle {

MasterInstrumentList::MasterInstrumentList() {}

juce::File MasterInstrumentList::getDefaultFile() {
  return getUserAppSupportDir().getChildFile(
      "Antigravity/FiddleServer/master_instruments.json");
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

bool MasterInstrumentList::loadFromDB(FiddleDatabase &db) {
  juce::String json = db.loadEnsemble();

  if (json.isNotEmpty()) {
    // Loaded from DB
    return setSlotsFromJson(json);
  }

  // DB is empty — try legacy JSON file for one-time migration
  auto legacyFile = getDefaultFile();
  if (legacyFile.existsAsFile()) {
    std::cerr << "[MasterList] Migrating from legacy JSON file..." << std::endl;
    if (load(legacyFile)) {
      saveToDB(db);
      std::cerr << "[MasterList] Migration complete (" << slots_.size()
                << " slots)" << std::endl;
      return true;
    }
  }

  // No data anywhere — start empty
  return false;
}

bool MasterInstrumentList::saveToDB(FiddleDatabase &db) const {
  juce::String json = getSlotsAsJson();
  db.saveEnsemble(json);
  std::cerr << "[MasterList] Saved " << slots_.size() << " ensemble slots to DB"
            << std::endl;
  return true;
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

  // Build entries with Dorico-style labels, using persistent assignments
  // for port/channel if available.
  std::map<juce::String, int> soloCounters, sectionCounters;
  juce::Array<juce::var> arr;
  int fallbackIndex = 0;

  for (const auto &slot : slots_) {
    for (int i = 0; i < slot.soloCount; ++i) {
      int num = ++soloCounters[slot.name];
      juce::String label = slot.name;
      if (soloTotals[slot.name] > 1)
        label += " " + juce::String(num);

      // Look up persistent assignment
      auto key = std::make_tuple(slot.entityID, true, num);
      auto it = assignments_.find(key);
      int idx = (it != assignments_.end()) ? it->second : fallbackIndex;

      auto *obj = new juce::DynamicObject();
      obj->setProperty("port", idx / 16);
      obj->setProperty("channel", idx % 16);
      obj->setProperty("name", slot.name);
      obj->setProperty("label", label);
      obj->setProperty("family", slot.family);
      obj->setProperty("isSolo", true);
      arr.add(juce::var(obj));
      ++fallbackIndex;
    }
    for (int i = 0; i < slot.sectionCount; ++i) {
      int num = ++sectionCounters[slot.name];
      juce::String label = slot.name;
      if (sectionTotals[slot.name] > 1)
        label += " " + toRoman(num);

      auto key = std::make_tuple(slot.entityID, false, num);
      auto it = assignments_.find(key);
      int idx = (it != assignments_.end()) ? it->second : fallbackIndex;

      auto *obj = new juce::DynamicObject();
      obj->setProperty("port", idx / 16);
      obj->setProperty("channel", idx % 16);
      obj->setProperty("name", slot.name);
      obj->setProperty("label", label);
      obj->setProperty("family", slot.family);
      obj->setProperty("isSolo", false);
      arr.add(juce::var(obj));
      ++fallbackIndex;
    }
  }

  // Sort by (port, channel) ascending — foundational stability invariant.
  {
    std::vector<juce::var> entries;
    entries.reserve((size_t)arr.size());
    for (int i = 0; i < arr.size(); ++i)
      entries.push_back(arr[i]);

    std::stable_sort(entries.begin(), entries.end(),
                     [](const juce::var &a, const juce::var &b) {
                       auto *oa = a.getDynamicObject();
                       auto *ob = b.getDynamicObject();
                       if (!oa || !ob)
                         return false;
                       int pa = (int)oa->getProperty("port");
                       int pb = (int)ob->getProperty("port");
                       if (pa != pb)
                         return pa < pb;
                       int ca = (int)oa->getProperty("channel");
                       int cb = (int)ob->getProperty("channel");
                       return ca < cb;
                     });

    arr.clearQuick();
    for (auto &e : entries)
      arr.add(e);

    std::cerr << "[MasterList] Channel map sorted: " << arr.size()
              << " entries (port/channel ascending)" << std::endl;
  }

  return juce::JSON::toString(juce::var(arr), true);
}

bool MasterInstrumentList::reconcileAssignments(FiddleDatabase &db) {
  // 1. Build the set of needed chairs from current slots
  using Key = std::tuple<juce::String, bool, int>; // entityID, isSolo, instNum
  std::map<Key, bool> needed;

  std::map<juce::String, int> soloCounters, sectionCounters;
  for (const auto &slot : slots_) {
    for (int i = 0; i < slot.soloCount; ++i) {
      int num = ++soloCounters[slot.entityID];
      needed[{slot.entityID, true, num}] = true;
    }
    for (int i = 0; i < slot.sectionCount; ++i) {
      int num = ++sectionCounters[slot.entityID];
      needed[{slot.entityID, false, num}] = true;
    }
  }

  // 2. Load existing assignments
  auto existing = db.loadChannelAssignments();

  // Build map of existing: key → flatIndex, and reverse: flatIndex → row
  std::map<Key, int> existingMap;
  for (const auto &r : existing) {
    existingMap[{r.entityID, r.isSolo, r.instanceNum}] = r.flatIndex;
  }

  // 3. Partition: keep vs. remove
  std::map<Key, int> kept; // assignments to keep
  int maxIndex = -1;

  for (const auto &r : existing) {
    Key k = {r.entityID, r.isSolo, r.instanceNum};
    if (needed.count(k)) {
      kept[k] = r.flatIndex;
      if (r.flatIndex > maxIndex)
        maxIndex = r.flatIndex;
    } else {
      // Move to graveyard
      db.moveToGraveyard(r);
    }
  }

  // 4. For each needed chair not yet assigned, try graveyard, then append
  for (const auto &[k, _] : needed) {
    if (kept.count(k))
      continue;

    auto &[entityID, isSolo, instNum] = k;

    // Try graveyard reclamation
    int reclaimed = db.reclaimFromGraveyard(entityID, isSolo);
    if (reclaimed >= 0) {
      kept[k] = reclaimed;
      if (reclaimed > maxIndex)
        maxIndex = reclaimed;
      std::cerr << "[ChannelAssign] Reclaimed index " << reclaimed << " for "
                << entityID << (isSolo ? " solo" : " section") << " #"
                << instNum << std::endl;
    } else {
      // Append at next available index
      ++maxIndex;
      kept[k] = maxIndex;
      std::cerr << "[ChannelAssign] New index " << maxIndex << " for "
                << entityID << (isSolo ? " solo" : " section") << " #"
                << instNum << std::endl;
    }
  }

  // 5. Check compaction threshold (240 = 256 - 16 headroom)
  bool compacted = false;
  if (maxIndex >= 240) {
    // Compact: renumber sequentially from 0
    std::cerr << "[ChannelAssign] Compacting! maxIndex=" << maxIndex
              << std::endl;
    int idx = 0;
    for (auto &[k, flatIdx] : kept) {
      flatIdx = idx++;
    }
    db.clearGraveyard();
    compacted = true;
  }

  // 6. Save assignments to DB and update in-memory map
  std::vector<ChannelAssignmentRow> rows;
  rows.reserve(kept.size());
  assignments_.clear();

  for (const auto &[k, flatIdx] : kept) {
    auto &[entityID, isSolo, instNum] = k;
    rows.push_back({flatIdx, entityID, isSolo, instNum});
    assignments_[k] = flatIdx;
  }
  db.saveChannelAssignments(rows);

  return compacted;
}

} // namespace fiddle
