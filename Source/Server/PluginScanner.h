#pragma once

#include "FiddleDatabase.h"
#include "HostedPluginSlot.h"
#include "PluginCatalogScan.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace fiddle {

/// Scans for installed VST3 plugins on the system and maintains a
/// KnownPluginList.  Supports incremental scanning using the database-backed
/// plugin_cache to avoid re-scanning unchanged plugins.
class PluginScanner {
public:
  PluginScanner() = default;

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
      const auto instrumentCompatibility =
          PluginCompatibility::fromDescription(desc,
                                               PluginSlotRole::instrument);
      const auto effectCompatibility = PluginCompatibility::fromDescription(
          desc, PluginSlotRole::effect);
      obj->setProperty("isInstrument", desc.isInstrument);
      obj->setProperty("hasStereoInput",
                       instrumentCompatibility.hasStereoInput);
      obj->setProperty("hasStereoOutput",
                       instrumentCompatibility.hasStereoOutput);
      obj->setProperty("compatibleAsInstrument",
                       instrumentCompatibility.compatible);
      obj->setProperty("instrumentRequiresRuntimeValidation",
                       instrumentCompatibility.requiresRuntimeValidation);
      obj->setProperty("instrumentCompatibilityReason",
                       instrumentCompatibility.reason);
      obj->setProperty("compatibleAsEffect", effectCompatibility.compatible);
      obj->setProperty("effectRequiresRuntimeValidation",
                       effectCompatibility.requiresRuntimeValidation);
      obj->setProperty("effectCompatibilityReason",
                       effectCompatibility.reason);

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
  /// How long one bundle may take to describe in the helper process before it
  /// is killed. Large sample players with copy protection take seconds.
  static constexpr int probeTimeoutMs = 60000;

  juce::KnownPluginList knownPlugins_;
  std::atomic<bool> scanning_{false};

  /// Map from plugin UID to validity (false = failed to load). Message thread.
  std::unordered_map<int, bool> validityMap_;

  /// Message thread: replaces the catalog with a finished scan's result. The
  /// previous catalog stays visible for the whole scan, so a slow bundle never
  /// leaves plug-ins missing from the UI.
  void applyCatalog(const PluginCatalog &catalog) {
    knownPlugins_.clear();
    for (const auto &desc : catalog.types)
      knownPlugins_.addType(desc);
    validityMap_ = catalog.validity;
  }

  /// Keeps the scan's cache rows in the plugin_cache table.
  class DatabaseCacheStore : public PluginCacheStore {
  public:
    explicit DatabaseCacheStore(FiddleDatabase &db) : db_(db) {}
    std::vector<PluginCacheRow> load() override {
      return db_.loadPluginCache();
    }
    void save(const PluginCacheRow &row) override {
      db_.savePluginCacheEntry(row);
    }
    void remove(const juce::String &path) override {
      db_.removePluginCacheEntry(path);
    }
    void clear() override { db_.clearPluginCache(); }

  private:
    FiddleDatabase &db_;
  };

  /// Background thread that lists the bundles and describes changed ones in
  /// the FiddlePluginScan helper process. Plug-ins are never instantiated in
  /// FiddleServer's own process here: the layers may be hosting the same
  /// plug-in at that moment.
  class ScanThread : public juce::Thread {
  public:
    ScanThread(PluginScanner &owner, FiddleDatabase *db, bool fullRescan,
               std::function<void()> onComplete)
        : juce::Thread("VST3 Scanner"), owner_(owner), db_(db),
          fullRescan_(fullRescan), onComplete_(std::move(onComplete)) {}

    void run() override {
      juce::StringArray paths;
      paths.add("/Library/Audio/Plug-Ins/VST3");
      paths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                    .getFullPathName() +
                "/Library/Audio/Plug-Ins/VST3");

      std::vector<juce::File> bundles;
      for (const auto &dir : paths) {
        juce::File folder(dir);
        if (!folder.isDirectory())
          continue;
        for (const auto &f : folder.findChildFiles(
                 juce::File::findFilesAndDirectories, false, "*.vst3"))
          bundles.push_back(f);
      }

      const auto helper = defaultPluginScanHelper();
      auto shouldExit = [this] { return threadShouldExit(); };
      PluginProbe probe = [&](const juce::File &bundle) {
        return probePluginOutOfProcess(helper, bundle, probeTimeoutMs,
                                       shouldExit);
      };

      PluginCatalog catalog;
      if (db_ != nullptr) {
        DatabaseCacheStore store(*db_);
        catalog =
            scanPluginBundles(store, bundles, fullRescan_, probe, shouldExit);
      } else {
        NullPluginCacheStore store;
        catalog = scanPluginBundles(store, bundles, true, probe, shouldExit);
      }

      juce::MessageManager::callAsync(
          [&owner = owner_, catalog = std::move(catalog),
           cb = std::move(onComplete_)]() {
            owner.applyCatalog(catalog);
            owner.scanning_ = false;
            if (cb)
              cb();
          });
      juce::MessageManager::callAsync([this]() { delete this; });
    }

  private:
    PluginScanner &owner_;
    FiddleDatabase *db_;
    bool fullRescan_;
    std::function<void()> onComplete_;
  };
};

} // namespace fiddle
