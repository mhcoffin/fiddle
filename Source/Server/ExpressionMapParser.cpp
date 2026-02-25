#include "ExpressionMapParser.h"
#include <sstream>

namespace fiddle {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string getChildText(const juce::XmlElement *el,
                                const juce::String &childName) {
  if (!el)
    return {};
  auto *child = el->getChildByName(childName);
  if (!child)
    return {};
  return child->getAllSubText().trim().toStdString();
}

static int getChildInt(const juce::XmlElement *el,
                       const juce::String &childName, int fallback = 0) {
  auto text = getChildText(el, childName);
  if (text.empty())
    return fallback;
  try {
    return std::stoi(text);
  } catch (...) {
    return fallback;
  }
}

static float getChildFloat(const juce::XmlElement *el,
                           const juce::String &childName,
                           float fallback = 0.0f) {
  auto text = getChildText(el, childName);
  if (text.empty())
    return fallback;
  try {
    return std::stof(text);
  } catch (...) {
    return fallback;
  }
}

static bool getChildBool(const juce::XmlElement *el,
                         const juce::String &childName, bool fallback = false) {
  auto text = getChildText(el, childName);
  return text == "true" ? true : text == "false" ? false : fallback;
}

/// Map XML type string to our enum.
static SwitchActionType parseActionType(const std::string &typeStr) {
  if (typeStr == "kKeySwitch")
    return SwitchActionType::KeySwitch;
  if (typeStr == "kNoteVelocity")
    return SwitchActionType::NoteVelocity;
  if (typeStr == "kCC")
    return SwitchActionType::CC;
  if (typeStr == "kControlChange")
    return SwitchActionType::ControlChange;
  if (typeStr == "kChannelSwitch")
    return SwitchActionType::ChannelSwitch;
  if (typeStr == "kProgramChange")
    return SwitchActionType::ProgramChange;
  return SwitchActionType::Unknown;
}

/// Parse <techniqueIDs> text into a set of technique ID strings.
/// Handles two formats:
///   "+"-separated: "pt.legato+pt.marcato"
///   ", "-separated: "pt.detache, pt.legato, pt.staccato"
static std::set<std::string> parseTechniqueIDs(const std::string &raw) {
  std::set<std::string> result;
  if (raw.empty())
    return result;

  // Determine delimiter: if it contains ", " use comma; otherwise use "+"
  char delim = '+';
  if (raw.find(", ") != std::string::npos)
    delim = ',';

  std::istringstream stream(raw);
  std::string token;
  while (std::getline(stream, token, delim)) {
    // Trim whitespace
    auto start = token.find_first_not_of(" \t\n\r");
    auto end = token.find_last_not_of(" \t\n\r");
    if (start != std::string::npos)
      result.insert(token.substr(start, end - start + 1));
  }
  return result;
}

/// Parse a list of <switchOnAction> or <switchOffAction> elements.
static std::vector<SwitchAction>
parseActions(const juce::XmlElement *actionsEl) {
  std::vector<SwitchAction> actions;
  if (!actionsEl)
    return actions;

  for (auto *action : actionsEl->getChildIterator()) {
    SwitchAction sa;
    sa.type = parseActionType(getChildText(action, "type"));
    sa.param1 = getChildInt(action, "param1");
    sa.param2 = getChildInt(action, "param2");
    actions.push_back(sa);
  }
  return actions;
}

/// Parse velocity or pitch range "min,max" → {min, max}.
static std::pair<int, int> parseRange(const std::string &rangeStr,
                                      int defaultMin, int defaultMax) {
  auto comma = rangeStr.find(',');
  if (comma == std::string::npos)
    return {defaultMin, defaultMax};
  try {
    int minVal = std::stoi(rangeStr.substr(0, comma));
    int maxVal = std::stoi(rangeStr.substr(comma + 1));
    return {minVal, maxVal};
  } catch (...) {
    return {defaultMin, defaultMax};
  }
}

// ---------------------------------------------------------------------------
// Core parser — operates on a single ExpressionMapDefinition element
// ---------------------------------------------------------------------------

static bool parseDefinition(const juce::XmlElement *mapDef,
                            ExpressionMapData &result) {
  if (!mapDef)
    return false;

  // Populate metadata
  result.name = getChildText(mapDef, "name");
  result.entityID = getChildText(mapDef, "entityID");
  result.creator = getChildText(mapDef, "creator");
  result.description = getChildText(mapDef, "description");
  result.version = getChildInt(mapDef, "version", 0);
  result.autoMutualExclusion =
      getChildBool(mapDef, "autoMutualExclusion", true);
  result.pitchBendRange = getChildInt(mapDef, "pitchBendRange", 2);

  // Parse playingTechniqueCombinations
  auto *combosEl = mapDef->getChildByName("playingTechniqueCombinations");
  if (!combosEl)
    return false;

  result.combinations.clear();

  for (auto *combo : combosEl->getChildIterator()) {
    if (combo->getTagName() != "playingTechniqueCombination")
      continue;

    TechniqueCombination tc;
    tc.name = getChildText(combo, "name");
    tc.baseSwitchID = getChildInt(combo, "baseSwitchID");
    tc.techniqueIDs = parseTechniqueIDs(getChildText(combo, "techniqueIDs"));

    tc.switchOnActions = parseActions(combo->getChildByName("switchOnActions"));
    tc.switchOffActions =
        parseActions(combo->getChildByName("switchOffActions"));

    tc.ticksBefore = getChildInt(combo, "ticksBefore");
    tc.millisecondsBefore = getChildInt(combo, "millisecondsBefore");

    tc.conditionString = getChildText(combo, "conditionString");

    tc.velocityFactor = getChildFloat(combo, "velocityFactor", 1.0f);
    tc.lengthFactor = getChildFloat(combo, "lengthFactor", 1.0f);
    tc.monophonic = getChildBool(combo, "monophonic");
    tc.transpose = getChildInt(combo, "transpose");

    // Parse ranges
    auto velRange = parseRange(getChildText(combo, "velocityRange"), 1, 127);
    tc.velocityMin = velRange.first;
    tc.velocityMax = velRange.second;

    auto pitchRange = parseRange(getChildText(combo, "pitchRange"), 0, 127);
    tc.pitchMin = pitchRange.first;
    tc.pitchMax = pitchRange.second;

    result.combinations.push_back(std::move(tc));
  }

  return !result.combinations.empty();
}

/// Navigate to the entities element, or nullptr.
static const juce::XmlElement *findEntities(const juce::XmlElement *xml) {
  if (!xml)
    return nullptr;
  auto *exprMapDefs = xml->getChildByName("expressionMapDefinitions");
  if (!exprMapDefs)
    return nullptr;
  return exprMapDefs->getChildByName("entities");
}

/// Parse all ExpressionMapDefinitions from an XML tree.
static std::vector<ExpressionMapData>
parseAllFromXml(const juce::XmlElement *xml) {
  std::vector<ExpressionMapData> results;
  auto *entities = findEntities(xml);
  if (!entities)
    return results;

  for (auto *child : entities->getChildIterator()) {
    if (child->getTagName() != "ExpressionMapDefinition")
      continue;
    ExpressionMapData data;
    if (parseDefinition(child, data))
      results.push_back(std::move(data));
  }
  return results;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool parseExpressionMap(const juce::File &file, ExpressionMapData &result) {
  if (!file.existsAsFile())
    return false;
  auto xml = juce::XmlDocument::parse(file);
  auto all = parseAllFromXml(xml.get());
  if (all.empty())
    return false;
  result = std::move(all[0]);
  return true;
}

bool parseExpressionMapXml(const juce::String &xmlString,
                           ExpressionMapData &result) {
  auto xml = juce::XmlDocument::parse(xmlString);
  auto all = parseAllFromXml(xml.get());
  if (all.empty())
    return false;
  result = std::move(all[0]);
  return true;
}

std::vector<ExpressionMapData> parseAllExpressionMaps(const juce::File &file) {
  if (!file.existsAsFile())
    return {};
  auto xml = juce::XmlDocument::parse(file);
  return parseAllFromXml(xml.get());
}

std::vector<ExpressionMapData>
parseAllExpressionMapsXml(const juce::String &xmlString) {
  auto xml = juce::XmlDocument::parse(xmlString);
  return parseAllFromXml(xml.get());
}

std::vector<ExpressionMapMeta>
scanExpressionMapMetadata(const juce::File &file) {
  std::vector<ExpressionMapMeta> results;
  if (!file.existsAsFile())
    return results;

  auto xml = juce::XmlDocument::parse(file);
  auto *entities = findEntities(xml.get());
  if (!entities)
    return results;

  for (auto *child : entities->getChildIterator()) {
    if (child->getTagName() != "ExpressionMapDefinition")
      continue;
    ExpressionMapMeta meta;
    meta.name = getChildText(child, "name");
    meta.entityID = getChildText(child, "entityID");
    meta.version = getChildInt(child, "version", 0);
    meta.creator = getChildText(child, "creator");
    if (!meta.entityID.empty())
      results.push_back(std::move(meta));
  }
  return results;
}

} // namespace fiddle
