#pragma once

#include <functional>
#include <juce_core/juce_core.h>
#include <vector>

namespace fiddle {

struct PluginCatalogState {
  bool scanning = false;
  int count = 0;
  juce::var plugins;
};

/// Operations exposed by the hosted-plugin command boundary.
class PluginCommands {
public:
  using ScanCompletion = std::function<void(PluginCatalogState)>;
  using LoadCompletion = std::function<void()>;

  virtual ~PluginCommands() = default;

  [[nodiscard]] virtual PluginCatalogState catalogState() const = 0;
  [[nodiscard]] virtual bool isPluginAvailable(int pluginUid) const = 0;

  virtual bool beginScan(bool fullRescan, ScanCompletion completion) = 0;
  virtual bool setPlugin(const juce::String &stripId, int pluginUid,
                         LoadCompletion completion) = 0;
  virtual bool setGroupPlugin(const std::vector<juce::String> &stripIds,
                              int pluginUid,
                              LoadCompletion completion) = 0;
  virtual bool showEditor(const juce::String &stripId) = 0;
  virtual bool restoreLibraryState(const juce::String &stripId,
                                   const juce::String &libraryId,
                                   const juce::String &entityId,
                                   int instanceNumber) = 0;
};

} // namespace fiddle
