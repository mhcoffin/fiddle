#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace fiddle {

/// Scans for installed VST3 plugins on the system and maintains a
/// KnownPluginList.  All scanning happens on a background thread;
/// a completion callback fires on the message thread when done.
class PluginScanner {
public:
  PluginScanner() { formatManager_.addFormat(new juce::VST3PluginFormat()); }

  /// Is a scan currently running?
  bool isScanning() const { return scanning_.load(); }

  /// Trigger an asynchronous scan of standard VST3 directories.
  /// @param onComplete  Called on the message thread when the scan finishes.
  void scanAsync(std::function<void()> onComplete = {}) {
    if (scanning_.exchange(true))
      return; // already running

    auto *thread = new ScanThread(*this, std::move(onComplete));
    thread->startThread();
  }

  /// Returns the known plugin list as a JSON string:
  /// [{ name, manufacturer, category, format, uid, numInputChannels,
  ///    numOutputChannels }, ...]
  juce::String getPluginListAsJson() const {
    juce::Array<juce::var> arr;
    for (const auto &desc : knownPlugins_.getTypes()) {
      auto *obj = new juce::DynamicObject();
      obj->setProperty("name", desc.name);
      obj->setProperty("manufacturer", desc.manufacturerName);
      obj->setProperty("category", desc.category);
      obj->setProperty("format", desc.pluginFormatName);
      obj->setProperty("uid", desc.uniqueId);
      obj->setProperty("numInputChannels", desc.numInputChannels);
      obj->setProperty("numOutputChannels", desc.numOutputChannels);
      arr.add(juce::var(obj));
    }
    return juce::JSON::toString(juce::var(arr), true);
  }

  int getPluginCount() const { return knownPlugins_.getNumTypes(); }

  const juce::KnownPluginList &getKnownPluginList() const {
    return knownPlugins_;
  }

  juce::KnownPluginList &getKnownPluginListMutable() { return knownPlugins_; }

private:
  juce::AudioPluginFormatManager formatManager_;
  juce::KnownPluginList knownPlugins_;
  std::atomic<bool> scanning_{false};

  /// Background thread that performs the actual file-system scan.
  class ScanThread : public juce::Thread {
  public:
    ScanThread(PluginScanner &owner, std::function<void()> onComplete)
        : juce::Thread("VST3 Scanner"), owner_(owner),
          onComplete_(std::move(onComplete)) {}

    void run() override {
      juce::StringArray paths;
      // System-wide
      paths.add("/Library/Audio/Plug-Ins/VST3");
      // Per-user
      paths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                    .getFullPathName() +
                "/Library/Audio/Plug-Ins/VST3");

      auto *vst3Format = owner_.formatManager_.getFormat(0);
      if (vst3Format == nullptr) {
        owner_.scanning_ = false;
        return;
      }

      for (const auto &dir : paths) {
        juce::File folder(dir);
        if (!folder.isDirectory())
          continue;

        auto files = folder.findChildFiles(juce::File::findFilesAndDirectories,
                                           false, "*.vst3");

        for (const auto &file : files) {
          if (threadShouldExit())
            break;

          juce::OwnedArray<juce::PluginDescription> results;
          vst3Format->findAllTypesForFile(results, file.getFullPathName());
          for (auto *desc : results) {
            owner_.knownPlugins_.addType(*desc);
          }
        }
      }

      owner_.scanning_ = false;

      // Notify on the message thread
      if (onComplete_) {
        juce::MessageManager::callAsync(
            [cb = std::move(onComplete_)]() { cb(); });
      }

      // Self-delete (detached thread pattern)
      juce::MessageManager::callAsync([this]() { delete this; });
    }

  private:
    PluginScanner &owner_;
    std::function<void()> onComplete_;
  };
};

} // namespace fiddle
