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

  /// Capture the live state, or retained initial state when unloaded/loading/
  /// unavailable, only for the expected player. Empty state is valid.
  bool captureState(const std::string &patchId, int expectedPluginUid,
                    std::vector<std::uint8_t> &destination);
  void seedState(const std::string &id, int pluginUid,
                 std::vector<std::uint8_t> state);
  void showOnly(const std::vector<std::string> &ids);
  void retainOnly(const std::vector<std::string> &ids);
  bool isLoading() const;

  /// Consume editor/processor notifications from every preview and return the
  /// affected catalog patch IDs. This is kept separate from MainComponent's
  /// project dirty tracking.
  std::vector<std::string> consumeChangedPatchIds();

  void discard(const std::string &patchId);
  void closeAll();

  [[nodiscard]] std::size_t size() const noexcept { return previews_.size(); }

private:
  struct Preview {
    explicit Preview(const std::string &patchId);

    HostedPluginSlot slot{PluginSlotRole::instrument};
    int pluginUid = 0;
    bool active = true;
  };

  juce::AudioPluginFormatManager &formatManager_;
  std::map<std::string, std::unique_ptr<Preview>> previews_;
  struct Seed { int pluginUid; std::vector<std::uint8_t> state; };
  std::map<std::string, Seed> seeds_;
};

} // namespace fiddle
