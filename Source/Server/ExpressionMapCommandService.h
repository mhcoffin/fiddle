#pragma once

#include "ExpressionMapCommands.h"
#include "MixerStrip.h"
#include <functional>
#include <string>

namespace fiddle {

class ExpressionMapLibrary;
class MixerModel;
class UndoManager;

/// Where layers' expression maps come from (docs/maps.md). Each hook is
/// optional; without it the service falls back to Dorico's library folders,
/// as before Fiddle kept its own maps.
struct ExpressionMapSources {
  /// Fiddle's stored map with this ID. Null data when there is none.
  std::function<ExpressionMapAssignment(const std::string &entityId)> stored;
  /// Import a file's first map into Fiddle's maps for the strip; its entity
  /// ID, or empty on failure.
  std::function<std::string(const juce::String &stripId, const juce::File &file)> importForStrip;
};

/// Applies expression-map assignments and records undoable library changes.
class ExpressionMapCommandService final : public ExpressionMapCommands {
public:
  ExpressionMapCommandService(MixerModel &mixer, ExpressionMapLibrary &library,
                              UndoManager &undoManager);
  void setSources(ExpressionMapSources sources) { sources_ = std::move(sources); }

  [[nodiscard]] juce::var catalog() const override;
  [[nodiscard]] juce::var details(const juce::String &entityId) override;
  bool assign(const juce::String &stripId,
              const juce::String &entityId) override;
  bool clear(const juce::String &stripId) override;
  bool assignGroup(const std::vector<juce::String> &stripIds,
                   const juce::String &entityId) override;
  bool importFile(const juce::String &stripId,
                  const juce::File &file) override;

private:
  MixerModel &mixer_;
  ExpressionMapAssignment resolve(const juce::String &stripId, const std::string &entityId);
  ExpressionMapLibrary &library_;
  UndoManager &undoManager_;
  ExpressionMapSources sources_;
};

} // namespace fiddle
