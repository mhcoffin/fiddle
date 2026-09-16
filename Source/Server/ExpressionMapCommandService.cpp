#include "ExpressionMapCommandService.h"

#include "ExpressionMapLibrary.h"
#include "ExpressionMapParser.h"
#include "MixerModel.h"
#include "UndoActions.h"
#include "UndoManager.h"

#include <memory>
#include <set>
#include <utility>

namespace fiddle {
namespace {

juce::var stringArray(const std::set<std::string> &values) {
  juce::Array<juce::var> result;
  for (const auto &value : values)
    result.add(juce::String(value));
  return result;
}

juce::var stringArray(const std::vector<std::string> &values) {
  juce::Array<juce::var> result;
  for (const auto &value : values)
    result.add(juce::String(value));
  return result;
}

juce::String actionTypeName(SwitchActionType type) {
  switch (type) {
  case SwitchActionType::KeySwitch: return "keySwitch";
  case SwitchActionType::NoteVelocity: return "noteVelocity";
  case SwitchActionType::CC: return "cc";
  case SwitchActionType::ControlChange: return "controlChange";
  case SwitchActionType::ChannelSwitch: return "channelSwitch";
  case SwitchActionType::ProgramChange: return "programChange";
  case SwitchActionType::Unknown: return "unknown";
  }
  return "unknown";
}

juce::var serializeActions(const std::vector<SwitchAction> &actions) {
  juce::Array<juce::var> result;
  for (const auto &action : actions) {
    auto *object = new juce::DynamicObject();
    object->setProperty("type", actionTypeName(action.type));
    object->setProperty("param1", action.param1);
    object->setProperty("param2", action.param2);
    result.add(juce::var(object));
  }
  return result;
}

juce::String volumeTypeName(VolumeType type) {
  return type == VolumeType::kCC ? "cc" : "noteVelocity";
}

juce::var serializeExpressionMap(const ExpressionMapData &data,
                                 const juce::String &sourcePath) {
  auto *root = new juce::DynamicObject();
  root->setProperty("found", true);
  root->setProperty("name", juce::String(data.name));
  root->setProperty("entityID", juce::String(data.entityID));
  root->setProperty("creator", juce::String(data.creator));
  root->setProperty("description", juce::String(data.description));
  root->setProperty("version", data.version);
  root->setProperty("sourcePath", sourcePath);
  root->setProperty("autoMutualExclusion", data.autoMutualExclusion);
  root->setProperty("pitchBendRange", data.pitchBendRange);

  juce::Array<juce::var> combinations;
  for (std::size_t index = 0; index < data.combinations.size(); ++index) {
    const auto &combination = data.combinations[index];
    auto *object = new juce::DynamicObject();
    object->setProperty("index", static_cast<int>(index));
    object->setProperty("name", juce::String(combination.name));
    object->setProperty("techniqueIDs", stringArray(combination.techniqueIDs));
    object->setProperty("baseSwitchID", combination.baseSwitchID);
    object->setProperty("isAddOn", combination.isAddOn);
    object->setProperty("switchOnActions",
                        serializeActions(combination.switchOnActions));
    object->setProperty("switchOffActions",
                        serializeActions(combination.switchOffActions));
    object->setProperty("ticksBefore", combination.ticksBefore);
    object->setProperty("millisecondsBefore", combination.millisecondsBefore);
    object->setProperty("condition", juce::String(combination.conditionString));
    object->setProperty("velocityFactor", combination.velocityFactor);
    object->setProperty("lengthFactor", combination.lengthFactor);
    object->setProperty("monophonic", combination.monophonic);
    object->setProperty("velocityMin", combination.velocityMin);
    object->setProperty("velocityMax", combination.velocityMax);
    object->setProperty("pitchMin", combination.pitchMin);
    object->setProperty("pitchMax", combination.pitchMax);
    object->setProperty("transpose", combination.transpose);
    object->setProperty("volumeType", volumeTypeName(combination.volumeType));
    object->setProperty("volumeCC", combination.volumeCC);
    object->setProperty("volumeType2", volumeTypeName(combination.volumeType2));
    object->setProperty("volumeCC2", combination.volumeCC2);
    combinations.add(juce::var(object));
  }
  root->setProperty("combinations", combinations);

  juce::Array<juce::var> megs;
  for (const auto &meg : data.megs) {
    auto *object = new juce::DynamicObject();
    object->setProperty("name", juce::String(meg.name));
    object->setProperty("techniqueIDs", stringArray(meg.techniqueIDs));
    object->setProperty("defaultID", juce::String(meg.defaultID));
    megs.add(juce::var(object));
  }
  root->setProperty("mutualExclusionGroups", megs);

  auto *timing = new juce::DynamicObject();
  timing->setProperty("natural", data.timingOptions.noteDurationPercent);
  timing->setProperty("staccato", data.timingOptions.staccatoDurationPercent);
  timing->setProperty("staccatissimo",
                      data.timingOptions.staccatissimoDurationPercent);
  timing->setProperty("legato", data.timingOptions.legatoDurationPercent);
  timing->setProperty("tenuto", data.timingOptions.tenutoDurationPercent);
  timing->setProperty("marcato", data.timingOptions.marcatoDurationPercent);
  root->setProperty("timing", juce::var(timing));
  return juce::var(root);
}

bool isCatalogAssignment(const ExpressionMapAssignment &assignment,
                         const std::string &entityId) {
  return assignment.data && assignment.data->entityID == entityId &&
         assignment.sourcePath.isEmpty() && assignment.sourceXml.isEmpty();
}

bool isSameImportedAssignment(const ExpressionMapAssignment &assignment,
                              const ExpressionMapData &data,
                              const juce::File &file,
                              const juce::String &sourceXml) {
  return assignment.data && assignment.data->entityID == data.entityID &&
         assignment.sourcePath == file.getFullPathName() &&
         assignment.sourceXml == sourceXml;
}

} // namespace

ExpressionMapCommandService::ExpressionMapCommandService(
    MixerModel &mixer, ExpressionMapLibrary &library, UndoManager &undoManager)
    : mixer_(mixer), library_(library), undoManager_(undoManager) {}

juce::var ExpressionMapCommandService::catalog() const {
  return juce::JSON::parse(library_.toJson());
}

juce::var ExpressionMapCommandService::details(const juce::String &entityId) {
  const auto requestedId = entityId.toStdString();
  const auto data = library_.load(requestedId);
  if (data == nullptr) {
    auto *error = new juce::DynamicObject();
    error->setProperty("found", false);
    error->setProperty("entityID", entityId);
    error->setProperty("error", "Expression map not found");
    return juce::var(error);
  }

  juce::String sourcePath;
  for (const auto &entry : library_.getEntries()) {
    if (entry.entityID == requestedId) {
      sourcePath = entry.sourceFile.getFullPathName();
      break;
    }
  }
  return serializeExpressionMap(*data, sourcePath);
}

bool ExpressionMapCommandService::assign(const juce::String &stripId,
                                         const juce::String &entityId) {
  auto *strip = mixer_.getStrip(stripId);
  if (strip == nullptr || entityId.isEmpty())
    return false;

  const auto newData = library_.load(entityId.toStdString());
  if (newData == nullptr)
    return false;

  auto before = strip->snapshotExpressionMap();
  if (isCatalogAssignment(before, entityId.toStdString()))
    return false;

  return undoManager_.perform(std::make_unique<SetExpressionMapAction>(
      mixer_, stripId, std::move(before),
      ExpressionMapAssignment{newData, {}, {}}));
}

bool ExpressionMapCommandService::clear(const juce::String &stripId) {
  auto *strip = mixer_.getStrip(stripId);
  if (strip == nullptr)
    return false;

  auto before = strip->snapshotExpressionMap();
  if (!before.data)
    return false;

  return undoManager_.perform(std::make_unique<SetExpressionMapAction>(
      mixer_, stripId, std::move(before), ExpressionMapAssignment{}));
}

bool ExpressionMapCommandService::assignGroup(
    const std::vector<juce::String> &stripIds, const juce::String &entityId) {
  const auto newEntityId = entityId.toStdString();
  auto newData = entityId.isEmpty() ? nullptr : library_.load(newEntityId);
  if (entityId.isNotEmpty() && newData == nullptr)
    return false;

  std::set<juce::String> uniqueIds;
  for (const auto &stripId : stripIds)
    if (stripId.isEmpty() || !uniqueIds.insert(stripId).second ||
        mixer_.getStrip(stripId) == nullptr)
      return false;

  std::vector<std::unique_ptr<UndoableAction>> actions;
  for (const auto &stripId : stripIds) {
    auto *strip = mixer_.getStrip(stripId);
    auto before = strip->snapshotExpressionMap();
    const bool alreadyAssigned = entityId.isEmpty()
                                     ? !before.data
                                     : isCatalogAssignment(before, newEntityId);
    if (alreadyAssigned)
      continue;

    actions.push_back(std::make_unique<SetExpressionMapAction>(
        mixer_, stripId, std::move(before),
        ExpressionMapAssignment{newData, {}, {}}));
  }

  if (actions.empty())
    return false;

  undoManager_.perform(std::make_unique<CompoundAction>(
      "Group set expression map", std::move(actions)));
  return true;
}

bool ExpressionMapCommandService::importFile(const juce::String &stripId,
                                             const juce::File &file) {
  auto *strip = mixer_.getStrip(stripId);
  if (strip == nullptr)
    return false;

  auto data = std::make_shared<ExpressionMapData>();
  const auto sourceXml = file.loadFileAsString();
  if (sourceXml.isEmpty() || !parseExpressionMapXml(sourceXml, *data))
    return false;

  auto before = strip->snapshotExpressionMap();
  if (isSameImportedAssignment(before, *data, file, sourceXml))
    return false;
  return undoManager_.perform(std::make_unique<SetExpressionMapAction>(
      mixer_, stripId, std::move(before),
      ExpressionMapAssignment{std::move(data), file.getFullPathName(),
                              sourceXml}));
}

} // namespace fiddle
