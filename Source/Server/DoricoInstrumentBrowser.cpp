#include "DoricoInstrumentBrowser.h"
#include <iostream>
#include <set>

namespace fiddle {

DoricoInstrumentBrowser::DoricoInstrumentBrowser() {}

juce::File DoricoInstrumentBrowser::findInstrumentsXml() const {
  juce::File appsDir("/Applications");

  // Scan for Dorico installations
  int highestVersion = 0;
  juce::File bestInstrumentsXml;

  for (const auto &entry : juce::RangedDirectoryIterator(
           appsDir, false, "Dorico*", juce::File::findDirectories)) {
    juce::File appDir = entry.getFile();
    juce::String name = appDir.getFileName();

    // Extract version number from "Dorico <N>.app" or "Dorico <N>"
    // Strip .app if present
    if (name.endsWith(".app"))
      name = name.dropLastCharacters(4);

    // Find the version number after "Dorico "
    int version = 0;
    if (name.startsWith("Dorico ")) {
      version = name.substring(7).getIntValue();
    } else if (name == "Dorico") {
      version = 1; // "Dorico.app" without version = treat as version 1
    }

    if (version > highestVersion) {
      juce::File candidate =
          appDir.getChildFile("Contents/Resources/instruments.xml");
      if (candidate.existsAsFile()) {
        highestVersion = version;
        bestInstrumentsXml = candidate;
      }
    }
  }

  if (bestInstrumentsXml.existsAsFile()) {
    std::cerr << "[InstrumentBrowser] Found instruments.xml in Dorico "
              << highestVersion << ": " << bestInstrumentsXml.getFullPathName()
              << std::endl;
  } else {
    std::cerr << "[InstrumentBrowser] No Dorico installation found"
              << std::endl;
  }

  return bestInstrumentsXml;
}

bool DoricoInstrumentBrowser::parseInstrumentsXml(const juce::File &file) {
  instruments_.clear();

  auto xml = juce::XmlDocument::parse(file);
  if (!xml) {
    std::cerr << "[InstrumentBrowser] Failed to parse XML: "
              << file.getFullPathName() << std::endl;
    return false;
  }

  // Navigate: <kScoreLibrary> -> <instruments> -> <entities>
  auto *instrumentsElem = xml->getChildByName("instruments");
  if (!instrumentsElem) {
    std::cerr << "[InstrumentBrowser] No <instruments> element found"
              << std::endl;
    return false;
  }

  auto *entitiesElem = instrumentsElem->getChildByName("entities");
  if (!entitiesElem) {
    std::cerr << "[InstrumentBrowser] No <entities> element found" << std::endl;
    return false;
  }

  // Each child of <entities> is an <InstrumentEntityDefinition>
  for (auto *instrElem = entitiesElem->getFirstChildElement(); instrElem;
       instrElem = instrElem->getNextElement()) {

    if (instrElem->getTagName() != "InstrumentEntityDefinition")
      continue;

    BrowsableInstrument instr;

    // Extract name
    auto *nameElem = instrElem->getChildByName("name");
    if (nameElem)
      instr.name = nameElem->getAllSubText().trim();

    // Extract entityID
    auto *entityElem = instrElem->getChildByName("entityID");
    if (entityElem)
      instr.entityID = entityElem->getAllSubText().trim();

    // Extract musicXMLSoundID
    auto *soundElem = instrElem->getChildByName("musicXMLSoundID");
    if (soundElem)
      instr.musicXMLSoundID = soundElem->getAllSubText().trim();

    // Derive family from musicXMLSoundID (first segment)
    if (instr.musicXMLSoundID.isNotEmpty()) {
      int dotPos = instr.musicXMLSoundID.indexOfChar('.');
      if (dotPos >= 0)
        instr.family = instr.musicXMLSoundID.substring(0, dotPos);
      else
        instr.family = instr.musicXMLSoundID;
    }

    // Check for parentEntityID — if present, this is a variant, not the default
    auto *parentElem = instrElem->getChildByName("parentEntityID");
    if (parentElem && parentElem->getAllSubText().trim().isNotEmpty())
      instr.isDefault = false;

    // Skip entries without essential fields
    if (instr.name.isEmpty() || instr.entityID.isEmpty())
      continue;

    instruments_.push_back(std::move(instr));
  }

  std::cerr << "[InstrumentBrowser] Parsed " << instruments_.size()
            << " instruments" << std::endl;
  return !instruments_.empty();
}

bool DoricoInstrumentBrowser::loadFromDorico() {
  juce::File instrXml = findInstrumentsXml();
  if (!instrXml.existsAsFile())
    return false;
  bool ok = parseInstrumentsXml(instrXml);

  // Also parse score orders from the same directory
  juce::File scoreOrderXml =
      instrXml.getParentDirectory().getChildFile("instrumentScoreOrders.xml");
  if (scoreOrderXml.existsAsFile()) {
    parseScoreOrders(scoreOrderXml);
  } else {
    std::cerr << "[InstrumentBrowser] instrumentScoreOrders.xml not found"
              << std::endl;
  }

  if (ok)
    buildJsonCache();
  return ok;
}

const std::vector<BrowsableInstrument> &
DoricoInstrumentBrowser::getInstruments() const {
  return instruments_;
}

void DoricoInstrumentBrowser::buildJsonCache() {
  juce::Array<juce::var> arr;

  int idx = 0;
  for (const auto &instr : instruments_) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("name", instr.name);
    obj->setProperty("entityID", instr.entityID);
    obj->setProperty("musicXMLSoundID", instr.musicXMLSoundID);
    obj->setProperty("family", instr.family);
    obj->setProperty("xmlIndex", idx++);
    obj->setProperty("isDefault", instr.isDefault);

    // Include score order position if available
    auto it = scoreOrder_.find(instr.entityID);
    if (it != scoreOrder_.end()) {
      obj->setProperty("scoreOrder", it->second);
    } else {
      obj->setProperty("scoreOrder", 99999);
    }

    arr.add(juce::var(obj));
  }

  cachedJson_ = juce::JSON::toString(juce::var(arr), true);
}

std::vector<juce::String> DoricoInstrumentBrowser::getFamilies() const {
  std::set<juce::String> familySet;
  for (const auto &instr : instruments_) {
    if (instr.family.isNotEmpty())
      familySet.insert(instr.family);
  }
  return {familySet.begin(), familySet.end()};
}

bool DoricoInstrumentBrowser::parseScoreOrders(const juce::File &file) {
  scoreOrder_.clear();

  auto xml = juce::XmlDocument::parse(file);
  if (!xml) {
    std::cerr << "[InstrumentBrowser] Failed to parse score orders XML: "
              << file.getFullPathName() << std::endl;
    return false;
  }

  // Navigate: <kScoreLibrary> -> <instrumentScoreOrders> -> <entities>
  auto *ordersElem = xml->getChildByName("instrumentScoreOrders");
  if (!ordersElem) {
    std::cerr << "[InstrumentBrowser] No <instrumentScoreOrders> element"
              << std::endl;
    return false;
  }

  auto *entitiesElem = ordersElem->getChildByName("entities");
  if (!entitiesElem) {
    std::cerr << "[InstrumentBrowser] No <entities> element in score orders"
              << std::endl;
    return false;
  }

  // Find the "Orchestral" order
  for (auto *defElem = entitiesElem->getFirstChildElement(); defElem;
       defElem = defElem->getNextElement()) {

    if (defElem->getTagName() != "InstrumentScoreOrderEntityDefinition")
      continue;

    auto *nameElem = defElem->getChildByName("name");
    if (!nameElem || nameElem->getAllSubText().trim() != "Orchestral")
      continue;

    // Found the Orchestral order — extract instrument IDs
    auto *dataElem = defElem->getChildByName("data");
    if (!dataElem)
      break;

    auto *idsElem = dataElem->getChildByName("instrumentIDs");
    if (!idsElem)
      break;

    juce::String idList = idsElem->getAllSubText().trim();
    juce::StringArray ids;
    ids.addTokens(idList, ",", "");

    int position = 0;
    for (auto &id : ids) {
      juce::String trimmed = id.trim();
      if (trimmed.isNotEmpty()) {
        scoreOrder_[trimmed] = position++;
      }
    }

    std::cerr << "[InstrumentBrowser] Parsed Orchestral score order: "
              << position << " entries" << std::endl;
    return true;
  }

  std::cerr << "[InstrumentBrowser] No 'Orchestral' score order found"
            << std::endl;
  return false;
}

const std::unordered_map<juce::String, int> &
DoricoInstrumentBrowser::getScoreOrder() const {
  return scoreOrder_;
}

} // namespace fiddle
