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
