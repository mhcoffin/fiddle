#include "PercussionMap.h"
#include <algorithm>
#include <map>
#include <tuple>

namespace fiddle {
namespace {

std::string text(const juce::XmlElement &e, const char *tag) {
  const auto *child = e.getChildByName(tag);
  return child ? child->getAllSubText().trim().toStdString() : std::string{};
}

std::set<std::string> splitTechniques(const juce::String &joined) {
  std::set<std::string> result;
  for (const auto &part : juce::StringArray::fromTokens(joined, "+", ""))
    if (part.trim().isNotEmpty()) result.insert(part.trim().toStdString());
  return result;
}

// Dorico writes one keyswitch per entry; accept a comma/space list.
bool parseKeyswitches(const juce::String &field, std::vector<int> &out) {
  for (const auto &token : juce::StringArray::fromTokens(field, ", ", "")) {
    if (token.isEmpty()) continue;
    if (!token.containsOnly("0123456789")) return false;
    out.push_back(token.getIntValue());
  }
  return true;
}

// Aliases are separate Dorico IDs for the same instrument
// ("instrument.band.unpitched.tomtom.high.alias.tom-high").
std::string canonicalInstrument(const std::string &id) {
  const auto at = id.find(".alias.");
  return at == std::string::npos ? id : id.substr(0, at);
}

void addText(juce::XmlElement &parent, const char *tag, const juce::String &value) {
  parent.createNewChildElement(tag)->addTextElement(value);
}

} // namespace

std::string percussionNaturalEquivalent(const std::string &instrument) {
  // instruments.xml, Dorico 6: the unpitched playing techniques marked
  // <naturalEquivalent>true</naturalEquivalent>.
  static const std::map<std::string, std::string> table{
      {"instrument.latin.unpitched.surdo", "pt.open2"},
      {"instrument.orchestral.unpitched.hihat", "pt.closed"},
      {"instrument.orchestral.unpitched.hihat.pedal", "pt.pedal"},
      {"instrument.orchestral.unpitched.triangle", "pt.open2"},
  };
  const auto it = table.find(canonicalInstrument(instrument));
  return it == table.end() ? std::string{} : it->second;
}

bool isRollTechnique(const std::string &technique) {
  // The bridge's roll family (experiments/unpitched-percussion-bridge/profiles.py).
  static const std::set<std::string> rolls{
      "pt.roll", "pt.tremolo", "pt.buzzRoll", "pt.pressRoll", "pt.flatRoll", "pt.circularRoll",
      "pt.swooshRoll", "pt.trill", "pt.trill.half", "pt.trill.whole"};
  return rolls.count(technique) > 0;
}

std::string joinTechniques(const std::set<std::string> &techniques) {
  std::string joined;
  for (const auto &t : techniques) {
    if (!joined.empty()) joined += "+";
    joined += t;
  }
  return joined;
}

std::shared_ptr<const PercussionMap> compilePercussionMap(const juce::String &xml) {
  if (xml.trim().isEmpty())
    return nullptr;
  juce::String error;
  auto maps = PercussionMap::parseLibrary(xml, error);
  return maps.size() == 1 ? std::make_shared<const PercussionMap>(std::move(maps.front())) : nullptr;
}

bool PercussionMap::fromDefinition(const juce::XmlElement &definition, PercussionMap &out, juce::String &error) {
  if (!definition.hasTagName("DrumKitNoteMapDefinition")) {
    error = "Expected a DrumKitNoteMapDefinition";
    return false;
  }
  PercussionMap map;
  map.entityId = text(definition, "entityID");
  map.name = text(definition, "name");
  const auto version = juce::String(text(definition, "version"));
  map.version = version.containsOnly("0123456789") && version.isNotEmpty() ? version.getIntValue() : 1;
  map.forSingleInstrument = text(definition, "forSingleInstrument") != "false";
  if (const auto *entries = definition.getChildByName("entries")) {
    for (const auto *row : entries->getChildWithTagNameIterator("entry")) {
      PercussionMapEntry entry;
      entry.instrument = text(*row, "instrumentEntityID");
      entry.techniques = splitTechniques(text(*row, "techniqueIDs"));
      const auto pitch = juce::String(text(*row, "pitch"));
      entry.pitch = pitch.containsOnly("0123456789") && pitch.isNotEmpty() ? pitch.getIntValue() : -1;
      entry.name = text(*row, "name");
      entry.variant = text(*row, "variant");
      if (!parseKeyswitches(text(*row, "keyswitches"), entry.keyswitches)) {
        error = "Invalid keyswitches in \"" + juce::String(entry.name) + "\"";
        return false;
      }
      map.entries.push_back(std::move(entry));
    }
  }
  if (const auto problems = map.validate(); !problems.isEmpty()) {
    error = (map.name.empty() ? juce::String("Percussion map") : juce::String(map.name)) + ": " + problems[0];
    return false;
  }
  out = std::move(map);
  return true;
}

std::vector<PercussionMap> PercussionMap::parseLibrary(const juce::String &xml, juce::String &error) {
  std::vector<PercussionMap> maps;
  const auto root = juce::parseXML(xml);
  if (!root) {
    error = "Not an XML file";
    return maps;
  }
  std::vector<const juce::XmlElement *> definitions;
  if (root->hasTagName("DrumKitNoteMapDefinition"))
    definitions.push_back(root.get());
  else if (const auto *defs = root->getChildByName("drumKitNoteMapDefinitions"))
    if (const auto *entities = defs->getChildByName("entities"))
      for (const auto *d : entities->getChildWithTagNameIterator("DrumKitNoteMapDefinition"))
        definitions.push_back(d);
  for (const auto *d : definitions) {
    PercussionMap map;
    if (!fromDefinition(*d, map, error)) return {};
    maps.push_back(std::move(map));
  }
  if (maps.empty()) error = "The file contains no percussion maps";
  return maps;
}

std::unique_ptr<juce::XmlElement> PercussionMap::toDefinition() const {
  auto definition = std::make_unique<juce::XmlElement>("DrumKitNoteMapDefinition");
  addText(*definition, "name", juce::String(name));
  addText(*definition, "entityID", juce::String(entityId));
  definition->createNewChildElement("parentEntityID");
  addText(*definition, "inheritanceMask", "0");
  addText(*definition, "version", juce::String(version));
  addText(*definition, "forSingleInstrument", forSingleInstrument ? "true" : "false");
  addText(*definition, "applyStageTemplateSettings", "false");
  auto *list = definition->createNewChildElement("entries");
  list->setAttribute("array", "true");
  for (const auto &e : entries) {
    auto *row = list->createNewChildElement("entry");
    addText(*row, "instrumentEntityID", juce::String(e.instrument));
    addText(*row, "techniqueIDs", juce::String(joinTechniques(e.techniques)));
    addText(*row, "pitch", juce::String(e.pitch));
    if (!e.keyswitches.empty()) {
      juce::StringArray keys;
      for (const int k : e.keyswitches) keys.add(juce::String(k));
      addText(*row, "keyswitches", keys.joinIntoString(","));
    }
    addText(*row, "name", juce::String(e.name));
    if (!e.variant.empty()) addText(*row, "variant", juce::String(e.variant));
  }
  return definition;
}

juce::String PercussionMap::toDefinitionXml() const {
  return toDefinition()->toString(juce::XmlElement::TextFormat().withoutHeader());
}

juce::String PercussionMap::toLibraryXml() const {
  juce::XmlElement root("kScoreLibrary");
  addText(root, "fileVersion", "1.1438");
  auto *entities = root.createNewChildElement("drumKitNoteMapDefinitions")->createNewChildElement("entities");
  entities->setAttribute("array", "true");
  entities->addChildElement(toDefinition().release());
  return root.toString();
}

juce::var PercussionMap::toJson() const {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("entityId", juce::String(entityId));
  obj->setProperty("name", juce::String(name));
  obj->setProperty("version", version);
  obj->setProperty("forSingleInstrument", forSingleInstrument);
  juce::Array<juce::var> rows;
  for (const auto &e : entries) {
    auto *row = new juce::DynamicObject();
    row->setProperty("instrument", juce::String(e.instrument));
    juce::Array<juce::var> techniques, keys;
    for (const auto &t : e.techniques) techniques.add(juce::String(t));
    for (const int k : e.keyswitches) keys.add(k);
    row->setProperty("techniques", techniques);
    row->setProperty("pitch", e.pitch);
    row->setProperty("keyswitches", keys);
    row->setProperty("name", juce::String(e.name));
    row->setProperty("variant", juce::String(e.variant));
    rows.add(juce::var(row));
  }
  obj->setProperty("entries", rows);
  return juce::var(obj);
}

bool PercussionMap::fromJson(const juce::var &json, PercussionMap &out, juce::String &error) {
  if (!json.isObject()) {
    error = "Expected a percussion map object";
    return false;
  }
  PercussionMap map;
  map.entityId = json["entityId"].toString().trim().toStdString();
  map.name = json["name"].toString().trim().toStdString();
  map.version = json.hasProperty("version") ? int(json["version"]) : 1;
  map.forSingleInstrument = json.hasProperty("forSingleInstrument") ? bool(json["forSingleInstrument"]) : true;
  if (const auto *rows = json["entries"].getArray()) {
    for (const auto &row : *rows) {
      PercussionMapEntry entry;
      entry.instrument = row["instrument"].toString().trim().toStdString();
      if (const auto *techniques = row["techniques"].getArray())
        for (const auto &t : *techniques)
          if (t.toString().trim().isNotEmpty()) entry.techniques.insert(t.toString().trim().toStdString());
      entry.pitch = row.hasProperty("pitch") ? int(row["pitch"]) : -1;
      if (const auto *keys = row["keyswitches"].getArray())
        for (const auto &k : *keys) entry.keyswitches.push_back(int(k));
      entry.name = row["name"].toString().trim().toStdString();
      entry.variant = row["variant"].toString().trim().toStdString();
      map.entries.push_back(std::move(entry));
    }
  }
  if (const auto problems = map.validate(); !problems.isEmpty()) {
    error = problems[0];
    return false;
  }
  out = std::move(map);
  return true;
}

juce::StringArray PercussionMap::validate() const {
  juce::StringArray problems;
  if (entityId.empty()) problems.add("Missing entity ID");
  if (name.empty()) problems.add("Missing name");
  if (version < 1) problems.add("Version must be at least 1");
  if (entries.empty()) problems.add("Add at least one entry");
  std::set<std::tuple<std::string, std::string, std::set<std::string>>> seen;
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto &e = entries[i];
    const auto label = "Entry " + juce::String(int(i) + 1) + (e.name.empty() ? "" : " (" + juce::String(e.name) + ")");
    if (juce::String(e.instrument).startsWith("instrument.") == false)
      problems.add(label + ": expected a Dorico instrument ID");
    if (e.pitch < 0 || e.pitch > 127) problems.add(label + ": pitch must be 0-127");
    for (const int k : e.keyswitches)
      if (k < 0 || k > 127) problems.add(label + ": keyswitch must be 0-127");
    if (e.techniques.empty()) problems.add(label + ": needs at least one technique (Natural is pt.natural)");
    if (!seen.emplace(e.instrument, e.variant, e.techniques).second)
      problems.add(label + ": duplicate techniques for this instrument");
  }
  return problems;
}

std::set<std::string> PercussionMap::instruments() const {
  std::set<std::string> result;
  for (const auto &e : entries) result.insert(e.instrument);
  return result;
}

PercussionMap::Match PercussionMap::resolve(const std::string &instrument,
                                            const std::set<std::string> &techniques) const {
  Match match;
  // 1. Entries for this instrument.
  std::vector<const PercussionMapEntry *> rows;
  const auto collect = [&](auto &&same) {
    for (const auto &e : entries)
      if (same(e.instrument)) rows.push_back(&e);
  };
  collect([&](const std::string &id) { return id == instrument; });
  if (rows.empty())
    collect([&](const std::string &id) { return canonicalInstrument(id) == canonicalInstrument(instrument); });
  if (rows.empty() && instruments().size() == 1) {
    collect([](const std::string &) { return true; });
    match.instrumentFallback = !rows.empty();
  }
  // Variant entries only stand in when the instrument has no plain ones.
  if (std::any_of(rows.begin(), rows.end(), [](auto *e) { return e->variant.empty(); }))
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](auto *e) { return !e->variant.empty(); }), rows.end());

  // 2. Natural is implied, and so is the instrument's natural equivalent.
  const auto naturalEquivalent = percussionNaturalEquivalent(rows.empty() ? instrument : rows.front()->instrument);
  const auto reduce = [&](const std::set<std::string> &set) {
    auto reduced = set;
    reduced.erase("pt.natural");
    if (!naturalEquivalent.empty()) reduced.erase(naturalEquivalent);
    return reduced;
  };
  const auto note = reduce(techniques);

  // 3. Largest subset; the first listed wins a tie.
  size_t best = 0;
  for (const auto *e : rows) {
    const auto set = reduce(e->techniques);
    if (!std::includes(note.begin(), note.end(), set.begin(), set.end())) continue;
    if (!match.entry || set.size() > best) {
      match.entry = e;
      best = set.size();
    }
  }
  return match;
}

} // namespace fiddle
