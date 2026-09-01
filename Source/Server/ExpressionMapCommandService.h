#pragma once

#include "ExpressionMapCommands.h"

namespace fiddle {

class ExpressionMapLibrary;
class MixerModel;
class UndoManager;

/// Applies expression-map assignments and records undoable library changes.
class ExpressionMapCommandService final : public ExpressionMapCommands {
public:
  ExpressionMapCommandService(MixerModel &mixer, ExpressionMapLibrary &library,
                              UndoManager &undoManager);

  [[nodiscard]] juce::var catalog() const override;
  bool assign(const juce::String &stripId,
              const juce::String &entityId) override;
  bool clear(const juce::String &stripId) override;
  bool assignGroup(const std::vector<juce::String> &stripIds,
                   const juce::String &entityId) override;
  bool importFile(const juce::String &stripId,
                  const juce::File &file) override;

private:
  MixerModel &mixer_;
  ExpressionMapLibrary &library_;
  UndoManager &undoManager_;
};

} // namespace fiddle
