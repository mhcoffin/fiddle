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
juce::String sortedIds(const std::set<std::string> &ids) {
  juce::StringArray parts;
  for (const auto &id : ids) parts.add(id);
  return parts.joinIntoString("+");
}
juce::String wire(const Action &a) { return (a.key ? "KS" : "CC") + juce::String(a.number) + "=" + juce::String(a.value); }
juce::XmlElement *beaconAction(juce::XmlElement &actions, int pitch) {
  for (auto *row : actions.getChildIterator())
    if (text(*row, "type") == "kKeySwitch" && text(*row, "param1") == juce::String(pitch)) return row;
  return nullptr;
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
    // The one supported override policy makes Dorico send written lengths:
    // all six note-duration options at 100%, no same-pitch truncation, and
    // switches at Dorico's default lead. Each layer then applies durations,
    // ends same-pitch notes and leads its own switches itself.
    const auto *overrides = map->getChildByName("playbackOptionsOverrides");
    const bool writtenDurations = overrides && overrides->getNumChildElements() > 0;
    if (writtenDurations) {
      std::map<juce::String, juce::String> required;
      for (const auto *name : durationOptionNames()) required[name] = "int:100";
      required[kTruncateSamePitchOption] = "bool:false";
      required[kSwitchLeadOption] = "int:" + juce::String(kDoricoDefaultSwitchLeadTicks);
      std::set<juce::String> pinned;
      for (const auto *row : overrides->getChildIterator()) {
        require(row->hasTagName("optionsOverride"), "Invalid playback override");
        fields(*row, "option value");
        const auto option = text(*row, "option");
        const auto expected = required.find(option);
        require(expected != required.end(), "Unsupported bridge playback override: " + option);
        require(text(*row, "value").removeCharacters(" ") == expected->second,
                "Bridge playback override " + option + " must be " + expected->second);
        require(pinned.insert(option).second, "Duplicate playback override: " + option);
      }
      require(pinned.size() == required.size(),
              "Pin all six note-duration options to 100%, same-pitch truncation off and "
              "switch lead to 10 ticks, or none");
    }
    auto result = std::make_shared<BridgeMapDocument>();
    result->sourceXml = xml; result->entityId = text(*map, "entityID"); result->name = text(*map, "name");
    result->revision = integer(text(*map, "version"), 1, 1000000);
    require(result->entityId.isNotEmpty() && result->name.isNotEmpty(), "Missing bridge identity or name");
    result->musicalLow = low; result->musicalHigh = high;
    auto compiled = std::make_shared<CompiledBridgeMap>();
    compiled->musicalLow = low; compiled->musicalHigh = high;
    const auto *init = map->getChildByName("initSwitchData");
    require(init && text(*init, "enabled") == "true", "Initialization must be enabled");
    fields(*init, "enabled initActions");
    const auto *actions = init->getChildByName("initActions");
    require(actions != nullptr, "Missing initialization commands");
    std::array<int, 2> beaconSeen{-1, -1};
    for (const auto *row : actions->getChildIterator()) {
      require(row->hasTagName("switchOnAction"), "Invalid Init action");
      const auto a = action(*row);
      const auto &pitches = CompiledBridgeMap::beaconPitches;
      if (const auto slot = std::find(pitches.begin(), pitches.end(), a.number); a.key && slot != pitches.end()) {
        auto &seen = beaconSeen[size_t(slot - pitches.begin())];
        require(seen < 0, "Duplicate revision beacon action");
        seen = a.value;
      }
    }
    require((beaconSeen[0] < 0) == (beaconSeen[1] < 0), "The revision beacon needs both KS126 and KS127");
    // Schema 2 is declared by the beacon; its add-ons must use typed Offs.
    const bool typed = beaconSeen[0] >= 0;
    compiled->schema = result->schema = typed ? 2 : 1;
    if (typed) {
      compiled->beacon = beaconFor(result->entityId, result->revision);
      require(beaconSeen == compiled->beacon,
              "Revision beacon does not match this map's identity; publish it through Fiddle to restamp it");
      require(high < CompiledBridgeMap::beaconPitches[0], "Musical range overlaps the revision beacon pitches");
    }
    juce::StringArray semantic;
    semantic.add("schema=" + juce::String(compiled->schema) + " range=" + juce::String(low) + "-" + juce::String(high)
                 + " id=" + result->entityId + " version=" + juce::String(result->revision)
                 + " multi=" + text(*map, "allowMultipleNotesAtSamePitch"));
    compiled->writtenDurations = writtenDurations;
    if (writtenDurations) semantic.add("durations=written");
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
      semantic.add("base|" + sortedIds(ids) + "|" + juce::String(length) + "|flags=" + text(*row, "flags")
                   + "|" + (ccDynamics ? "cc1" : "velocity") + "|attack=" + (attack ? text(*attack, "type") : juce::String{})
                   + "|" + wire(a));
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
        const auto &pitches = CompiledBridgeMap::beaconPitches;
        require(!typed || !on.key || std::find(pitches.begin(), pitches.end(), on.number) == pitches.end(),
                "Add-ons cannot use the revision beacon pitches");
        const int reset = typed ? (on.key ? 1 : 0) : off.value;
        if (typed) {
          require(off.value == on.value + CompiledBridgeMap::typedOffOffset && on.value != reset,
                  "Schema 2 add-ons need a typed Off: Off = On + 64, with On below 64");
        }
        auto ids = techniques(text(*row, "techniqueIDs"));
        for (const auto &id : ids) require(!vocabulary.count(id) && addonIds.insert(id).second, "Multiply encoded playback technique");
        const auto conduit = std::make_pair(on.key, on.number);
        auto it = axes.find(conduit);
        if (it == axes.end()) {
          axes[conduit] = compiled->dimensions.size();
          CompiledBridgeMap::Dimension d; d.key = on.key; d.number = on.number; d.reset = reset;
          d.values.emplace(reset, CompiledBridgeMap::Value{});
          compiled->dimensions.push_back(std::move(d));
          it = axes.find(conduit);
        }
        auto &d = compiled->dimensions[it->second];
        require(d.reset == reset && d.values.emplace(on.value, CompiledBridgeMap::Value{ids}).second, "Ambiguous add-on values or inconsistent reset");
        if (typed) require(d.typedOffs.emplace(off.value, on.value).second, "Ambiguous typed Off value");
        semantic.add("addon|" + sortedIds(ids) + "|" + wire(on) + "|" + wire(off));
      }
    vocabulary.insert(addonIds.begin(), addonIds.end());
    if (const auto *groups = map->getChildByName("mutualExclusionGroups")) {
      std::set<juce::String> groupIds;
      for (const auto *group : groups->getChildIterator()) {
        require(group->hasTagName("mutualExclusionGroup"), "Invalid mutex group");
        fields(*group, "groupID name techniqueIDs");
        require(text(*group, "groupID").isNotEmpty() && groupIds.insert(text(*group, "groupID")).second, "Duplicate/empty mutex group ID");
        std::set<std::string> members;
        for (const auto &pt : juce::StringArray::fromTokens(text(*group, "techniqueIDs"), ",", "")) {
          require(vocabulary.count(pt.trim().toStdString()), "Mutex group references an unencoded technique");
          members.insert(pt.trim().toStdString());
        }
        semantic.add("meg|" + text(*group, "groupID") + "|" + sortedIds(members));
      }
    }
    std::set<size_t> initialized;
    for (const auto *row : actions->getChildIterator()) {
      const auto a = action(*row);
      semantic.add("init|" + wire(a));
      const auto &pitches = CompiledBridgeMap::beaconPitches;
      if (typed && a.key && std::find(pitches.begin(), pitches.end(), a.number) != pitches.end()) continue;
      const auto it = axes.find({a.key, a.number});
      require(it != axes.end() && initialized.insert(it->second).second, "Unknown/duplicate Init conduit");
      const auto &d = compiled->dimensions[it->second];
      require(d.values.count(a.value), "Unknown Init value");
      require(it->second == 0 ? d.values.at(a.value).techniques == std::set<std::string>{"pt.natural"} : a.value == d.reset,
              "Init must set Natural and clear every add-on dimension");
    }
    require(initialized.size() == compiled->dimensions.size(), "Initialization must cover every conduit");
    require(compiled->dimensions.size() <= 52, "Bridge exceeds protocol budget");
    normalize(*map);
    // Keep schema 1's exact canonical form: persisted projects key on it.
    const auto canonical = map->toString(juce::XmlElement::TextFormat().singleLine()) + "\nbridge-schema-"
        + juce::String(compiled->schema) + ":" + juce::String(low) + ":" + juce::String(high);
    result->contentHash = juce::SHA256(canonical.toRawUTF8(), canonical.getNumBytesAsUTF8()).toHexString();
    semantic.sort(false);
    const auto meaning = semantic.joinIntoString("\n");
    result->semanticHash = juce::SHA256(meaning.toRawUTF8(), meaning.getNumBytesAsUTF8()).toHexString();
    result->compiled = std::move(compiled);
    return result;
  } catch (const std::exception &e) { error = e.what(); return {}; }
}

std::array<int, 2> BridgeMapDocument::beaconFor(const juce::String &entityId, int revision) {
  const auto identity = entityId + ":" + juce::String(revision);
  const auto digest = juce::SHA256(identity.toRawUTF8(), identity.getNumBytesAsUTF8()).getRawData();
  const auto *bytes = static_cast<const uint8_t *>(digest.getData());
  const uint32_t word = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
  const int value = int(word % (127u * 127u));
  return {1 + value / 127, 1 + value % 127};
}
bool BridgeMapDocument::stampBeacon(juce::XmlElement &map) {
  auto *init = map.getChildByName("initSwitchData");
  auto *actions = init ? init->getChildByName("initActions") : nullptr;
  auto *id = map.getChildByName("entityID");
  auto *version = map.getChildByName("version");
  if (!actions || !id || !version) return false;
  std::array<juce::XmlElement *, 2> rows{};
  for (size_t i = 0; i < rows.size(); ++i)
    if (!(rows[i] = beaconAction(*actions, CompiledBridgeMap::beaconPitches[i]))) return false;
  const auto velocities = beaconFor(id->getAllSubText().trim(), version->getAllSubText().trim().getIntValue());
  for (size_t i = 0; i < rows.size(); ++i)
    if (auto *param = rows[i]->getChildByName("param2")) {
      param->deleteAllChildElements(); param->addTextElement(juce::String(velocities[i]));
    }
  return true;
}

juce::var BridgeMapDocument::toJson() const {
  auto *o = new juce::DynamicObject();
  o->setProperty("schema", schema); o->setProperty("sourceXml", sourceXml);
  o->setProperty("contentHash", contentHash); o->setProperty("musicalLow", musicalLow); o->setProperty("musicalHigh", musicalHigh);
  return juce::var(o);
}
juce::String BridgeMapProject::serialize() const {
  if (documents.empty() && assignments.empty() && sections.empty()) return {};
  auto *o = new juce::DynamicObject(); o->setProperty("schema", 1);
  juce::Array<juce::var> docs, routes;
  for (const auto &[hash, doc] : documents) docs.add(doc->toJson());
  for (const auto &[ep, hash] : assignments) {
    auto *a = new juce::DynamicObject(); a->setProperty("port", ep.first); a->setProperty("channel", ep.second); a->setProperty("map", hash); routes.add(juce::var(a));
  }
  o->setProperty("documents", docs); o->setProperty("assignments", routes);
  if (!sections.empty()) { // Absent for older projects, keeping their hashes.
    auto *pins = new juce::DynamicObject();
    for (const auto &[key, hash] : sections) pins->setProperty(key, hash);
    o->setProperty("sections", juce::var(pins));
  }
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
      require(d["schema"].isInt() && int(d["schema"]) >= 1 && int(d["schema"]) <= BridgeMapDocument::latestSchema && d["sourceXml"].isString() && d["musicalLow"].isInt() && d["musicalHigh"].isInt(), "Unsupported bridge document schema");
      auto doc = BridgeMapDocument::compile(d["sourceXml"].toString(), error, int(d["musicalLow"]), int(d["musicalHigh"]));
      require(doc && doc->contentHash == d["contentHash"].toString() && doc->schema == int(d["schema"]), "Bridge source/fingerprint mismatch: " + error);
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
    if (const auto pins = value["sections"]; !pins.isVoid()) {
      const auto *object = pins.getDynamicObject();
      require(object != nullptr, "Invalid bridge sections");
      for (const auto &pin : object->getProperties()) {
        const auto hash = pin.value.toString();
        require(pin.value.isString() && result.documents.count(hash), "Bridge section pins a missing revision");
        result.sections.emplace(pin.name.toString(), hash);
      }
    }
    out = std::move(result); return true;
  } catch (const std::exception &e) { error = e.what(); return false; }
}
} // namespace fiddle
