#include "ExpressionMapCommandService.h"

#include "ExpressionMapLibrary.h"
#include "ExpressionMapParser.h"
#include "MixerModel.h"
#include "UndoActions.h"
#include "UndoManager.h"

#include <memory>
#include <utility>

namespace fiddle {
namespace {

std::string currentEntityId(const MixerStrip &strip) {
  return strip.expressionMap ? strip.expressionMap->entityID : std::string{};
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

  const auto oldEntityId = currentEntityId(*strip);
  if (oldEntityId == entityId.toStdString())
    return false;

  undoManager_.perform(std::make_unique<SetExpressionMapAction>(
      mixer_, library_, stripId, oldEntityId, entityId.toStdString(), newData));
  return true;
}

bool ExpressionMapCommandService::clear(const juce::String &stripId) {
  auto *strip = mixer_.getStrip(stripId);
  if (strip == nullptr)
    return false;

  const auto oldEntityId = currentEntityId(*strip);
  if (oldEntityId.empty())
    return false;

  undoManager_.perform(std::make_unique<SetExpressionMapAction>(
      mixer_, library_, stripId, oldEntityId, "", nullptr));
  return true;
}

bool ExpressionMapCommandService::assignGroup(
    const std::vector<juce::String> &stripIds, const juce::String &entityId) {
  const auto newEntityId = entityId.toStdString();
  auto newData = entityId.isEmpty() ? nullptr : library_.load(newEntityId);
  if (entityId.isNotEmpty() && newData == nullptr)
    return false;

  std::vector<std::unique_ptr<UndoableAction>> actions;
  for (const auto &stripId : stripIds) {
    auto *strip = mixer_.getStrip(stripId);
    if (strip == nullptr)
      continue;

    const auto oldEntityId = currentEntityId(*strip);
    if (oldEntityId == newEntityId)
      continue;

    actions.push_back(std::make_unique<SetExpressionMapAction>(
        mixer_, library_, stripId, oldEntityId, newEntityId, newData));
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
  if (!parseExpressionMap(file, *data))
    return false;

  strip->setExpressionMap(std::move(data));
  strip->expressionMapPath = file.getFullPathName();
  return true;
}

} // namespace fiddle
