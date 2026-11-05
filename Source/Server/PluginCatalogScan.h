#pragma once

// Builds the VST3 plug-in catalog from the plugin_cache table and the bundles
// on disk. A bundle whose modification time differs from its cache row is
// described again by a PluginProbe. In the server the probe runs the
// FiddlePluginScan helper process (probePluginOutOfProcess), never the
// server's own process: describing a VST3 that has no moduleinfo.json means
// instantiating it, and doing that inside FiddleServer while the layers host
// the same plug-in aborted startup (Vienna Synchron Player, 2026-11-04).

#include "PluginCacheRow.h"

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <functional>
#include <unordered_map>
#include <vector>

namespace fiddle {

/// Result of describing one plug-in bundle.
struct PluginProbeResult {
  enum class Outcome {
    described, ///< The bundle was loaded and listed its plug-ins.
    noPlugins, ///< The bundle was loaded but contains no usable plug-in.
    failed,    ///< The describer crashed or hung on this bundle.
    unavailable ///< The describer itself could not run (helper missing).
  };
  Outcome outcome = Outcome::failed;
  std::vector<juce::PluginDescription> descriptions;
  juce::String detail; ///< Human-readable reason, for the log.
};

using PluginProbe = std::function<PluginProbeResult(const juce::File &bundle)>;

/// Storage for plugin_cache rows (FiddleDatabase in the server).
class PluginCacheStore {
public:
  virtual ~PluginCacheStore() = default;
  virtual std::vector<PluginCacheRow> load() = 0;
  virtual void save(const PluginCacheRow &row) = 0;
  virtual void remove(const juce::String &path) = 0;
  virtual void clear() = 0;
};

/// A store that remembers nothing: every bundle is described.
class NullPluginCacheStore : public PluginCacheStore {
public:
  std::vector<PluginCacheRow> load() override { return {}; }
  void save(const PluginCacheRow &) override {}
  void remove(const juce::String &) override {}
  void clear() override {}
};

/// The plug-ins found by a scan and whether each one loaded.
struct PluginCatalog {
  std::vector<juce::PluginDescription> types;
  std::unordered_map<int, bool> validity; ///< uid → false if it failed to load
};

juce::PluginDescription descriptionFromCacheRow(const PluginCacheRow &row);

/// Scans `bundles` against the cache in `store`.
/// - Unchanged bundles (same modification time) come from the cache.
/// - Changed or new bundles are described by `probe`. A described bundle's
///   rows are saved with the new modification time, so it is not described
///   again on the next launch.
/// - If the probe fails (crash, hang) for a bundle that has cached rows, the
///   cached description is kept in the catalog and left unchanged in the store,
///   so the plug-in stays usable and is retried next time. Without cached rows
///   it is recorded as invalid.
/// - Cache rows for bundles no longer on disk are removed.
/// Stops early, without removing rows, when `shouldExit` returns true.
PluginCatalog scanPluginBundles(PluginCacheStore &store,
                                const std::vector<juce::File> &bundles,
                                bool fullRescan, const PluginProbe &probe,
                                const std::function<bool()> &shouldExit = {});

/// The FiddlePluginScan helper next to the running executable.
juce::File defaultPluginScanHelper();

/// Describes `bundle` by running `helper <bundle> <result.xml>` and reading the
/// result file. Kills the helper after `timeoutMs`, or when `shouldExit`
/// returns true.
PluginProbeResult
probePluginOutOfProcess(const juce::File &helper, const juce::File &bundle,
                        int timeoutMs,
                        const std::function<bool()> &shouldExit = {});

/// Helper side: writes the scan result file read by probePluginOutOfProcess.
/// Writes to a temporary sibling and renames it, so a partial file is never
/// read as a result.
bool writePluginScanResult(
    const juce::File &resultFile,
    const juce::OwnedArray<juce::PluginDescription> &descriptions);

} // namespace fiddle
