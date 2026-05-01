#pragma once

#include "FiddleDatabase.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace fiddle {

/// Scans for installed VST3 plugins on the system and maintains a
/// KnownPluginList.  Supports incremental scanning using the database-backed
/// plugin_cache to avoid re-scanning unchanged plugins.
class PluginScanner {
public:
  PluginScanner() { formatManager_.addFormat(std::make_unique<juce::VST3PluginFormat>()); }

  /// Is a scan currently running?
  bool isScanning() const { return scanning_.load(); }

  /// Perform an incremental scan: only scan plugins whose file modification
  /// time has changed since the last scan. Loads cached data from the database
  /// for unchanged plugins. Removes entries for plugins that no longer exist.
  /// @param db         Database for reading/writing plugin_cache
  /// @param onComplete Called on the message thread when done
  void scanIncrementalAsync(FiddleDatabase &db,
                            std::function<void()> onComplete = {}) {
    if (scanning_.exchange(true))
      return;

    auto *thread =
        new ScanThread(*this, &db, /*fullRescan=*/false, std::move(onComplete));
    thread->startThread();
  }

  /// Full rescan: clears the cache and scans everything from scratch.
  void rescanAsync(FiddleDatabase &db, std::function<void()> onComplete = {}) {
    if (scanning_.exchange(true))
      return;

    auto *thread =
        new ScanThread(*this, &db, /*fullRescan=*/true, std::move(onComplete));
    thread->startThread();
  }

  /// Legacy async scan (no database cache — used if DB not available).
  void scanAsync(std::function<void()> onComplete = {}) {
    if (scanning_.exchange(true))
      return;

    auto *thread = new ScanThread(*this, nullptr, false, std::move(onComplete));
    thread->startThread();
  }

  /// Returns the known plugin list as a JSON string.
  /// Now includes a 'valid' field from the cache.
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

      // Check if this plugin is marked invalid in our cache
      bool valid = true;
      auto it = validityMap_.find(desc.uniqueId);
      if (it != validityMap_.end())
        valid = it->second;
      obj->setProperty("valid", valid);

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

  /// Map from plugin UID to validity (false = failed to load)
  std::unordered_map<int, bool> validityMap_;

  /// Background thread that performs the actual file-system scan.
  class ScanThread : public juce::Thread {
  public:
    ScanThread(PluginScanner &owner, FiddleDatabase *db, bool fullRescan,
               std::function<void()> onComplete)
        : juce::Thread("VST3 Scanner"), owner_(owner), db_(db),
          fullRescan_(fullRescan), onComplete_(std::move(onComplete)) {}

    void run() override {
      // Build the list of .vst3 files on disk
      juce::StringArray paths;
      paths.add("/Library/Audio/Plug-Ins/VST3");
      paths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                    .getFullPathName() +
                "/Library/Audio/Plug-Ins/VST3");

      auto *vst3Format = owner_.formatManager_.getFormat(0);
      if (vst3Format == nullptr) {
        owner_.scanning_ = false;
        return;
      }

      // Clear existing list to prevent duplicates when rescanning
      owner_.knownPlugins_.clear();
      owner_.validityMap_.clear();

      // Collect all .vst3 files
      std::vector<juce::File> allFiles;
      for (const auto &dir : paths) {
        juce::File folder(dir);
        if (!folder.isDirectory())
          continue;
        auto files = folder.findChildFiles(juce::File::findFilesAndDirectories,
                                           false, "*.vst3");
        for (const auto &f : files)
          allFiles.push_back(f);
      }

      if (db_) {
        scanWithCache(vst3Format, allFiles);
      } else {
        scanWithoutCache(vst3Format, allFiles);
      }

      owner_.scanning_ = false;

      if (onComplete_) {
        juce::MessageManager::callAsync(
            [cb = std::move(onComplete_)]() { cb(); });
      }

      juce::MessageManager::callAsync([this]() { delete this; });
    }

  private:
    void scanWithCache(juce::AudioPluginFormat *vst3Format,
                       const std::vector<juce::File> &allFiles) {
      if (fullRescan_)
        db_->clearPluginCache();

      // Load existing cache
      auto cached = db_->loadPluginCache();
      std::unordered_map<std::string, PluginCacheRow> cacheMap;
      for (auto &row : cached)
        cacheMap[row.path.toStdString()] = std::move(row);

      // Track which cache entries are still on disk
      std::set<std::string> presentPaths;

      for (const auto &file : allFiles) {
        if (threadShouldExit())
          break;

        auto path = file.getFullPathName().toStdString();
        presentPaths.insert(path);

        int64_t modTime = file.getLastModificationTime().toMilliseconds();
        auto it = cacheMap.find(path);

        if (!fullRescan_ && it != cacheMap.end() &&
            it->second.modTime == modTime) {
          // Cache hit — plugin unchanged, add from cache
          juce::PluginDescription desc;
          desc.name = it->second.name;
          desc.manufacturerName = it->second.manufacturer;
          desc.category = it->second.category;
          desc.pluginFormatName = it->second.format;
          desc.uniqueId = it->second.uid;
          desc.numInputChannels = it->second.numInputs;
          desc.numOutputChannels = it->second.numOutputs;
          desc.fileOrIdentifier = juce::String(path);
          owner_.knownPlugins_.addType(desc);
          owner_.validityMap_[desc.uniqueId] = it->second.valid;
          continue;
        }

        // Cache miss — introspect the plugin
        std::cerr << "[PluginScanner] Scanning: " << path << std::endl;
        juce::OwnedArray<juce::PluginDescription> results;
        vst3Format->findAllTypesForFile(results, file.getFullPathName());

        if (results.isEmpty()) {
          // Failed to load — mark as invalid
          PluginCacheRow row;
          row.path = juce::String(path);
          row.modTime = modTime;
          row.name = file.getFileNameWithoutExtension();
          row.valid = false;
          db_->savePluginCacheEntry(row);

          // Still add a minimal entry so it shows in the UI
          juce::PluginDescription desc;
          desc.name = row.name;
          desc.fileOrIdentifier = juce::String(path);
          desc.pluginFormatName = "VST3";
          desc.uniqueId = static_cast<int>(juce::String(path).hashCode());
          owner_.knownPlugins_.addType(desc);
          owner_.validityMap_[desc.uniqueId] = false;
        } else {
          for (auto *desc : results) {
            owner_.knownPlugins_.addType(*desc);
            owner_.validityMap_[desc->uniqueId] = true;

            PluginCacheRow row;
            row.path = juce::String(path);
            row.modTime = modTime;
            row.name = desc->name;
            row.manufacturer = desc->manufacturerName;
            row.category = desc->category;
            row.format = desc->pluginFormatName;
            row.uid = desc->uniqueId;
            row.numInputs = desc->numInputChannels;
            row.numOutputs = desc->numOutputChannels;
            row.valid = true;
            db_->savePluginCacheEntry(row);
          }
        }
      }

      // Remove cache entries for plugins that no longer exist on disk
      for (const auto &[path, _] : cacheMap) {
        if (presentPaths.find(path) == presentPaths.end()) {
          db_->removePluginCacheEntry(juce::String(path));
        }
      }
    }

    void scanWithoutCache(juce::AudioPluginFormat *vst3Format,
                          const std::vector<juce::File> &allFiles) {
      for (const auto &file : allFiles) {
        if (threadShouldExit())
          break;

        juce::OwnedArray<juce::PluginDescription> results;
        vst3Format->findAllTypesForFile(results, file.getFullPathName());
        for (auto *desc : results) {
          owner_.knownPlugins_.addType(*desc);
        }
      }
    }

    PluginScanner &owner_;
    FiddleDatabase *db_;
    bool fullRescan_;
    std::function<void()> onComplete_;
  };
};

} // namespace fiddle
