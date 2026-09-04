#pragma once

#include "PluginCommands.h"

namespace fiddle {

class FiddleDatabase;
class MixerModel;
class PluginScanner;
class UndoManager;

/// Coordinates scanner, hosted-plugin, persistence, and undo operations.
class PluginCommandService final : public PluginCommands {
public:
  PluginCommandService(MixerModel &mixer, PluginScanner &scanner,
                       UndoManager &undoManager, FiddleDatabase &database);

  [[nodiscard]] PluginCatalogState catalogState() const override;
  [[nodiscard]] bool isPluginAvailable(int pluginUid) const override;

  bool beginScan(bool fullRescan, ScanCompletion completion) override;
  bool setPlugin(const juce::String &stripId, int pluginUid,
                 LoadCompletion completion) override;
  bool setGroupPlugin(const std::vector<juce::String> &stripIds, int pluginUid,
                      LoadCompletion completion) override;
  bool showEditor(const juce::String &stripId) override;
  bool restoreLibraryState(const juce::String &stripId,
                           const juce::String &libraryId,
                           const juce::String &entityId,
                           int instanceNumber) override;
  bool restoreLibraryPatchState(const juce::String &stripId,
                                const juce::String &patchId) override;

private:
  MixerModel &mixer_;
  PluginScanner &scanner_;
  UndoManager &undoManager_;
  FiddleDatabase &database_;
};

} // namespace fiddle
