#pragma once

#include "ExpressionMapCommands.h"
#include "MixerStrip.h"
#include <functional>
#include <string>

namespace fiddle {

class ExpressionMapLibrary;
class MixerModel;
class UndoManager;

/// Where layers' expression maps come from (docs/library-map-import.md).
/// Each hook is optional; without it the service falls back to Dorico's
/// library folders, as before per-library maps.
struct ExpressionMapSources {
  /// The map a strip plays for entityId, from the strip's library. Null data
  /// when the library has no such map.
  std::function<ExpressionMapAssignment(const juce::String &stripId, const std::string &entityId)>
      forStrip;
  /// A library's map, for the viewer.
  std::function<ExpressionMapAssignment(const juce::String &libraryId, const std::string &entityId)>
      forLibrary;
  /// Import a file's first map into the strip's library; its entity ID, or
  /// empty on failure.
  std::function<std::string(const juce::String &stripId, const juce::File &file)> importForStrip;
};

/// Applies expression-map assignments and records undoable library changes.
class ExpressionMapCommandService final : public ExpressionMapCommands {
public:
  ExpressionMapCommandService(MixerModel &mixer, ExpressionMapLibrary &library,
                              UndoManager &undoManager);
  void setSources(ExpressionMapSources sources) { sources_ = std::move(sources); }

  [[nodiscard]] juce::var catalog() const override;
  [[nodiscard]] juce::var details(const juce::String &entityId,
                                  const juce::String &libraryId = {}) override;
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
