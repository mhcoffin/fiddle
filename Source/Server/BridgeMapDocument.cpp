#include "BridgeMapDocument.h"
#include <juce_cryptography/juce_cryptography.h>
#include <stdexcept>

namespace fiddle {
namespace {
void require(bool ok, const juce::String &error) {
  if (!ok) throw std::runtime_error(error.toStdString());
}
juce::String text(const juce::XmlElement &node, const char *tag) {
  const auto *child = node.getChildByName(tag);
  if (child) for (const auto *part : child->getChildIterator())
    require(part->isTextElement(), "Nested data in scalar field: " + juce::String(tag));
  return child ? child->getAllSubText().trim() : juce::String{};
}
void attributes(const juce::XmlElement &node) {
  const juce::StringArray arrays{"entities", "initActions", "playingTechniqueCombinations", "techniqueAddOns", "mutualExclusionGroups", "switchOnActions", "switchOffActions", "playbackOptionsOverrides", "pluginNames"};
  for (int i = 0; i < node.getNumAttributes(); ++i)
    require(node.getAttributeName(i) == "array" && node.getAttributeValue(i) == "true" && arrays.contains(node.getTagName()),
            "Unsupported XML attribute on " + node.getTagName());
  for (const auto *child : node.getChildIterator()) if (!child->isTextElement()) attributes(*child);
}
bool neutralFactor(const juce::String &value) {
  if (value.isEmpty()) return true;
  try {
    size_t consumed = 0;
    const auto number = std::stod(value.toStdString(), &consumed);
    return consumed == size_t(value.length()) && number == 1.0;
  } catch (...) { return false; }
}
void fields(const juce::XmlElement &node, const juce::String &allowed) {
  const auto names = juce::StringArray::fromTokens(allowed, " ", "");
  std::set<juce::String> seen;
  for (const auto *child : node.getChildIterator()) {
    require(names.contains(child->getTagName()), "Unsupported bridge field: " + child->getTagName());
    require(seen.insert(child->getTagName()).second, "Duplicate bridge field: " + child->getTagName());
  }
}
int integer(const juce::String &s, int low, int high) {
  require(s.isNotEmpty() && s.containsOnly("0123456789"), "Expected an unsigned integer");
  const auto n = s.getLargeIntValue();
  require(n >= low && n <= high, "Bridge integer outside permitted range");
  return int(n);
}
std::set<std::string> techniques(const juce::String &s) {
  std::set<std::string> result;
  for (const auto &id : juce::StringArray::fromTokens(s, "+", "")) {
    const auto exact = id.trim();
    require(exact.startsWith("pt.") && exact.length() > 3, "Expected canonical playback-technique IDs");
    require(result.insert(exact.toStdString()).second, "Duplicate playback technique");
  }
  require(!result.empty(), "Empty playback-technique set");
  return result;
}
struct Action { bool key; int number, value; };
Action action(const juce::XmlElement &node) {
  fields(node, "type param1 param2");
  const auto type = text(node, "type");
  require(type == "kControlChange" || type == "kKeySwitch", "Only CC and velocity-coded keyswitch actions are supported");
  Action a{type == "kKeySwitch", integer(text(node, "param1"), 0, 127), integer(text(node, "param2"), 0, 127)};
  require(a.key ? a.value > 0 : a.number >= 102 && a.number <= 119, "Action outside bridge protocol pool");
  return a;
}
Action onlyAction(const juce::XmlElement &node, const char *tag) {
  const auto *list = node.getChildByName(tag);
  require(list && list->getNumChildElements() == 1, "Each bridge On/Off needs exactly one action");
  const auto *a = list->getFirstChildElement();
  require(a->hasTagName(juce::String(tag) == "switchOffActions" ? "switchOffAction" : "switchOnAction"), "Invalid action element");
  return action(*a);
}
void normalize(juce::XmlElement &node) {
  // Format-only whitespace is insignificant; all fields, order, and attributes
  // remain in the fingerprint. Unknown behavior is rejected, never discarded.
  for (auto *child = node.getFirstChildElement(); child;) {
    auto *next = child->getNextElement();
    if (child->isTextElement()) {
      if (child->getText().trim().isEmpty()) node.removeChildElement(child, true);
      else child->setText(child->getText().trim());
    } else normalize(*child);
    child = next;
  }
}
} // namespace

std::shared_ptr<const BridgeMapDocument> BridgeMapDocument::compile(
    const juce::String &xml, juce::String &error, int low, int high) {
  error.clear();
  try {
    require(xml.getNumBytesAsUTF8() <= 4 * 1024 * 1024, "Bridge XML exceeds 4 MiB");
    require(low >= 0 && high <= 127 && low <= high, "Invalid musical range");
    auto root = juce::parseXML(xml);
    require(root && root->hasTagName("kScoreLibrary"), "Expected a Dorico library");
    attributes(*root);
    fields(*root, "fileVersion expressionMapDefinitions");
    auto *defs = root->getChildByName("expressionMapDefinitions");
    require(defs != nullptr, "Missing expression maps");
    fields(*defs, "entities");
    auto *entities = defs->getChildByName("entities");
    require(entities && entities->getNumChildElements() == 1, "Import one bridge map at a time");
    auto *map = entities->getFirstChildElement();
    require(map->hasTagName("ExpressionMapDefinition"), "Expected an expression-map definition");
    fields(*map, "name entityID parentEntityID inheritanceMask creator description version pluginNames autoMutualExclusion allowMultipleNotesAtSamePitch applyStageTemplateSettings pitchBendRange microtonalPlaybackMethod initSwitchData playingTechniqueCombinations techniqueAddOns mutualExclusionGroups playbackOptionsOverrides");
    require(text(*map, "parentEntityID").isEmpty() && text(*map, "inheritanceMask") == "0", "Inherited bridge maps must be flattened before import");
    require(text(*map, "autoMutualExclusion") == "false", "Bridge maps require explicit mutex groups (Auto off)");
    require(text(*map, "applyStageTemplateSettings") == "false", "Disable Stage Template settings in the bridge map");
    require(text(*map, "microtonalPlaybackMethod") == "kAuto" && text(*map, "pitchBendRange") == "2", "Unsupported bridge pitch/microtonality configuration");
    const auto *overrides = map->getChildByName("playbackOptionsOverrides");
    require(!overrides || overrides->getNumChildElements() == 0, "Playback overrides need an explicit supported policy");
    auto result = std::make_shared<BridgeMapDocument>();
    result->sourceXml = xml; result->entityId = text(*map, "entityID"); result->name = text(*map, "name");
    result->revision = integer(text(*map, "version"), 1, 1000000);
    require(result->entityId.isNotEmpty() && result->name.isNotEmpty(), "Missing bridge identity or name");
    result->musicalLow = low; result->musicalHigh = high;
    auto compiled = std::make_shared<CompiledBridgeMap>();
    compiled->musicalLow = low; compiled->musicalHigh = high;
    CompiledBridgeMap::Dimension base; base.number = 102;
    std::set<std::string> vocabulary;
    std::set<juce::String> switchIds;
    const auto *bases = map->getChildByName("playingTechniqueCombinations");
    require(bases && bases->getNumChildElements() > 0, "Missing base switches");
    const juce::StringArray conditions{"NoteLength==kVeryShort", "NoteLength==kShort", "NoteLength==kMedium", "NoteLength==kLong", "NoteLength>kLong"};
    for (const auto *row : bases->getChildIterator()) {
      require(row->hasTagName("playingTechniqueCombination"), "Invalid base row");
      fields(*row, "baseSwitchID name techniqueIDs enabled flags conditionString velocityRange pitchRange transpose ticksBefore millisecondsBefore velocityFactor lengthFactor monophonic applyMillisecondsBeforeToEndOffsets applyMillisecondsBeforeToControllers volumeType attackType switchOnActions switchOffActions");
      require(text(*row, "enabled") == "true", "Disabled switches must be removed before publishing a decoder");
      require(text(*row, "baseSwitchID").isNotEmpty() && switchIds.insert(text(*row, "baseSwitchID")).second, "Duplicate/empty switch ID");
      // Dorico's stock staccato rows carry flag 1. Preserve/fingerprint these
      // observed host selection flags; decoding still follows received actions.
      require(text(*row, "flags").isEmpty() || text(*row, "flags") == "0" || text(*row, "flags") == "1", "Unrecognized Dorico base flags");
      for (const auto *tag : {"transpose", "ticksBefore", "millisecondsBefore"})
        require(text(*row, tag).isEmpty() || text(*row, tag) == "0", "Non-neutral base modifier: " + juce::String(tag));
      for (const auto *tag : {"velocityFactor", "lengthFactor"})
        require(neutralFactor(text(*row, tag)), "Non-neutral base modifier: " + juce::String(tag));
      require(text(*row, "monophonic") != "true", "Monophonic legato behavior is outside this decoder schema");
      require(text(*row, "pitchRange") == "0,127" && text(*row, "velocityRange") == "1,127", "Conditional pitch/velocity ranges are unsupported");
      auto condition = text(*row, "conditionString").removeCharacters(" \t\r\n");
      int length = conditions.indexOf(condition);
      require(length >= 0, "Base must transmit one explicit note-length bucket");
      const auto a = onlyAction(*row, "switchOnActions");
      require(!a.key && a.number == 102, "Base must write only CC102");
      const auto *off = row->getChildByName("switchOffActions");
      require(!off || off->getNumChildElements() == 0, "Base Off actions are unsupported");
      auto ids = techniques(text(*row, "techniqueIDs"));
      vocabulary.insert(ids.begin(), ids.end());
      bool ccDynamics = false;
      const auto *volume = row->getChildByName("volumeType");
      require(volume != nullptr, "Missing dynamics input policy");
      fields(*volume, "type param1");
      if (text(*volume, "type") == "kCC") {
        require(text(*volume, "param1") == "1", "Only CC1 or velocity dynamics ingress is supported");
        ccDynamics = true;
      } else require(text(*volume, "type") == "kNoteVelocity", "Unsupported dynamics ingress");
      const auto *attack = row->getChildByName("attackType");
      if (attack) {
        fields(*attack, "type param1");
        require(text(*attack, "type") == "kNoteVelocity", "Unsupported attack dynamics");
      }
      require(base.values.emplace(a.value, CompiledBridgeMap::Value{ids, length, ccDynamics}).second, "Ambiguous base code");
    }
    compiled->dimensions.push_back(std::move(base));
    std::map<std::pair<bool, int>, size_t> axes{{{false, 102}, 0}};
    std::set<std::string> addonIds;
    if (const auto *addons = map->getChildByName("techniqueAddOns"))
      for (const auto *row : addons->getChildIterator()) {
        require(row->hasTagName("techniqueAddOn"), "Invalid add-on row");
        fields(*row, "techAddOnSwitchID name techniqueIDs enabled switchOnActions switchOffActions");
        require(text(*row, "enabled") == "true", "Disabled add-ons must be removed before publishing");
        require(text(*row, "techAddOnSwitchID").isNotEmpty() && switchIds.insert("addon:" + text(*row, "techAddOnSwitchID")).second, "Duplicate/empty switch ID");
        const auto on = onlyAction(*row, "switchOnActions"), off = onlyAction(*row, "switchOffActions");
        require(on.key == off.key && on.number == off.number && on.value != off.value, "Add-on On/Off must own the same independent conduit");
        require(on.key || on.number != 102, "Add-ons cannot write the base conduit");
        require(!on.key || on.number < low || on.number > high, "Command pitch overlaps musical range");
        auto ids = techniques(text(*row, "techniqueIDs"));
        for (const auto &id : ids) require(!vocabulary.count(id) && addonIds.insert(id).second, "Multiply encoded playback technique");
        const auto wire = std::make_pair(on.key, on.number);
        auto it = axes.find(wire);
        if (it == axes.end()) {
          axes[wire] = compiled->dimensions.size();
          CompiledBridgeMap::Dimension d; d.key = on.key; d.number = on.number; d.reset = off.value;
          d.values.emplace(off.value, CompiledBridgeMap::Value{});
          compiled->dimensions.push_back(std::move(d));
          it = axes.find(wire);
        }
        auto &d = compiled->dimensions[it->second];
        require(d.reset == off.value && d.values.emplace(on.value, CompiledBridgeMap::Value{ids}).second, "Ambiguous add-on values or inconsistent reset");
      }
    vocabulary.insert(addonIds.begin(), addonIds.end());
    if (const auto *groups = map->getChildByName("mutualExclusionGroups")) {
      std::set<juce::String> groupIds;
      for (const auto *group : groups->getChildIterator()) {
        require(group->hasTagName("mutualExclusionGroup"), "Invalid mutex group");
        fields(*group, "groupID name techniqueIDs");
        require(text(*group, "groupID").isNotEmpty() && groupIds.insert(text(*group, "groupID")).second, "Duplicate/empty mutex group ID");
        for (const auto &pt : juce::StringArray::fromTokens(text(*group, "techniqueIDs"), ",", ""))
          require(vocabulary.count(pt.trim().toStdString()), "Mutex group references an unencoded technique");
      }
    }
    const auto *init = map->getChildByName("initSwitchData");
    require(init && text(*init, "enabled") == "true", "Initialization must be enabled");
    fields(*init, "enabled initActions");
    const auto *actions = init->getChildByName("initActions");
    require(actions != nullptr, "Missing initialization commands");
    std::set<size_t> initialized;
    for (const auto *row : actions->getChildIterator()) {
      require(row->hasTagName("switchOnAction"), "Invalid Init action");
      const auto a = action(*row); const auto it = axes.find({a.key, a.number});
      require(it != axes.end() && initialized.insert(it->second).second, "Unknown/duplicate Init conduit");
      const auto &d = compiled->dimensions[it->second];
      require(d.values.count(a.value), "Unknown Init value");
      require(it->second == 0 ? d.values.at(a.value).techniques == std::set<std::string>{"pt.natural"} : a.value == d.reset,
              "Init must set Natural and clear every add-on dimension");
    }
    require(initialized.size() == compiled->dimensions.size(), "Initialization must cover every conduit");
    require(compiled->dimensions.size() <= 52, "Bridge exceeds protocol budget");
    normalize(*map);
    const auto canonical = map->toString(juce::XmlElement::TextFormat().singleLine()) + "\nbridge-schema-1:" + juce::String(low) + ":" + juce::String(high);
    result->contentHash = juce::SHA256(canonical.toRawUTF8(), canonical.getNumBytesAsUTF8()).toHexString();
    result->compiled = std::move(compiled);
    return result;
  } catch (const std::exception &e) { error = e.what(); return {}; }
}

juce::var BridgeMapDocument::toJson() const {
  auto *o = new juce::DynamicObject();
  o->setProperty("schema", schema); o->setProperty("sourceXml", sourceXml);
  o->setProperty("contentHash", contentHash); o->setProperty("musicalLow", musicalLow); o->setProperty("musicalHigh", musicalHigh);
  return juce::var(o);
}
juce::String BridgeMapProject::serialize() const {
  if (documents.empty() && assignments.empty()) return {};
  auto *o = new juce::DynamicObject(); o->setProperty("schema", 1);
  juce::Array<juce::var> docs, routes;
  for (const auto &[hash, doc] : documents) docs.add(doc->toJson());
  for (const auto &[ep, hash] : assignments) {
    auto *a = new juce::DynamicObject(); a->setProperty("port", ep.first); a->setProperty("channel", ep.second); a->setProperty("map", hash); routes.add(juce::var(a));
  }
  o->setProperty("documents", docs); o->setProperty("assignments", routes);
  return juce::JSON::toString(juce::var(o), true);
}
bool BridgeMapProject::parse(const juce::String &json, BridgeMapProject &out, juce::String &error) {
  error.clear();
  try {
    BridgeMapProject result;
    if (json.isEmpty()) { out = {}; return true; }
    require(json.getNumBytesAsUTF8() <= 16 * 1024 * 1024, "Bridge project exceeds 16 MiB");
    const auto value = juce::JSON::parse(json);
    require(value["schema"].isInt() && int(value["schema"]) == 1, "Unsupported bridge project schema");
    const auto *docs = value["documents"].getArray(), *routes = value["assignments"].getArray();
    require(docs && routes && docs->size() <= 128 && routes->size() <= 256, "Invalid bridge project contents");
    std::set<juce::String> entityIds;
    for (const auto &d : *docs) {
      require(d["schema"].isInt() && int(d["schema"]) == BridgeMapDocument::schema && d["sourceXml"].isString() && d["musicalLow"].isInt() && d["musicalHigh"].isInt(), "Unsupported bridge document schema");
      auto doc = BridgeMapDocument::compile(d["sourceXml"].toString(), error, int(d["musicalLow"]), int(d["musicalHigh"]));
      require(doc && doc->contentHash == d["contentHash"].toString(), "Bridge source/fingerprint mismatch: " + error);
      require(entityIds.insert(doc->entityId).second, "Conflicting Dorico map IDs in bridge project");
      require(result.documents.emplace(doc->contentHash, doc).second, "Duplicate bridge revision");
    }
    for (const auto &a : *routes) {
      require(a["port"].isInt() && a["channel"].isInt() && a["map"].isString(), "Invalid bridge assignment");
      const int port = a["port"], channel = a["channel"];
      const auto hash = a["map"].toString();
      require(port >= 0 && port < 16 && channel >= 1 && channel <= 16 && result.documents.count(hash), "Missing revision or invalid endpoint");
      require(port != 0 || channel != 1, "Port 1/channel 1 is reserved for the metronome");
      require(result.assignments.emplace(Endpoint{port, channel}, hash).second, "Duplicate endpoint assignment");
    }
    out = std::move(result); return true;
  } catch (const std::exception &e) { error = e.what(); return false; }
}
} // namespace fiddle
