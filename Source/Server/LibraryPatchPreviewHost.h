#pragma once

#include "HostedPluginSlot.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fiddle {

/// Hosts Library Manager plug-in editors outside the audible/persistent mixer.
/// Preview instances are deliberately absent from MixerModel, strip snapshots,
/// mute/solo state, and the real-time audio graph.
class LibraryPatchPreviewHost {
public:
  using OpenCompletion =
      std::function<void(bool success, const juce::String &error)>;

  explicit LibraryPatchPreviewHost(
      juce::AudioPluginFormatManager &formatManager);
  ~LibraryPatchPreviewHost();

  LibraryPatchPreviewHost(const LibraryPatchPreviewHost &) = delete;
  LibraryPatchPreviewHost &
  operator=(const LibraryPatchPreviewHost &) = delete;

  bool open(const std::string &patchId, const juce::String &title,
            const juce::PluginDescription &description,
            const juce::MemoryBlock &initialState,
            OpenCompletion completion = nullptr);

  /// Capture the current editor state only when the preview still represents
  /// the expected plug-in. An empty state is a valid successful capture.
  bool captureState(const std::string &patchId, int expectedPluginUid,
                    std::vector<std::uint8_t> &destination);

  /// Consume editor/processor notifications from every preview. This is kept
  /// separate from MainComponent's project dirty tracking.
  bool consumeChangeNotifications() noexcept;

  void discard(const std::string &patchId);
  void closeAll();

  [[nodiscard]] std::size_t size() const noexcept { return previews_.size(); }

private:
  struct Preview {
    explicit Preview(const std::string &patchId);

    HostedPluginSlot slot{PluginSlotRole::instrument};
    int pluginUid = 0;
  };

  juce::AudioPluginFormatManager &formatManager_;
  std::map<std::string, std::unique_ptr<Preview>> previews_;
};

} // namespace fiddle
