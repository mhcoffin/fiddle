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

  for (const auto &instr : instruments_) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("name", instr.name);
    obj->setProperty("entityID", instr.entityID);
    obj->setProperty("musicXMLSoundID", instr.musicXMLSoundID);
    obj->setProperty("family", instr.family);
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

} // namespace fiddle
