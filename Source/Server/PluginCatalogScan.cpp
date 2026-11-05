#include "PluginCatalogScan.h"

#include <iostream>
#include <set>
#include <unordered_map>

namespace fiddle {

namespace {

constexpr const char *resultTag = "FIDDLE_PLUGIN_SCAN";

PluginCacheRow rowFromDescription(const juce::String &path, int64_t modTime,
                                  const juce::PluginDescription &desc) {
  PluginCacheRow row;
  row.path = path;
  row.modTime = modTime;
  row.name = desc.name;
  row.manufacturer = desc.manufacturerName;
  row.category = desc.category;
  row.format = desc.pluginFormatName;
  row.uid = desc.uniqueId;
  row.numInputs = desc.numInputChannels;
  row.numOutputs = desc.numOutputChannels;
  row.isInstrument = desc.isInstrument;
  row.valid = true;
  return row;
}

void addType(PluginCatalog &catalog, const juce::PluginDescription &desc,
             bool valid) {
  catalog.types.push_back(desc);
  catalog.validity[desc.uniqueId] = valid;
}

/// A bundle that could not be loaded still appears in the UI, marked invalid.
void addInvalidBundle(PluginCatalog &catalog, PluginCacheStore &store,
                      const juce::File &bundle, int64_t modTime) {
  PluginCacheRow row;
  row.path = bundle.getFullPathName();
  row.modTime = modTime;
  row.name = bundle.getFileNameWithoutExtension();
  row.valid = false;
  store.save(row);

  juce::PluginDescription desc;
  desc.name = row.name;
  desc.fileOrIdentifier = row.path;
  desc.pluginFormatName = "VST3";
  desc.uniqueId = static_cast<int>(row.path.hashCode());
  addType(catalog, desc, false);
}

} // namespace

juce::PluginDescription descriptionFromCacheRow(const PluginCacheRow &row) {
  juce::PluginDescription desc;
  desc.name = row.name;
  desc.manufacturerName = row.manufacturer;
  desc.category = row.category;
  desc.pluginFormatName = row.format;
  desc.uniqueId = row.uid;
  desc.numInputChannels = row.numInputs;
  desc.numOutputChannels = row.numOutputs;
  desc.isInstrument = row.isInstrument;
  desc.fileOrIdentifier = row.path;
  return desc;
}

PluginCatalog scanPluginBundles(PluginCacheStore &store,
                                const std::vector<juce::File> &bundles,
                                bool fullRescan, const PluginProbe &probe,
                                const std::function<bool()> &shouldExit) {
  if (fullRescan)
    store.clear();

  // path → cached rows (the table keys on path, so normally one row).
  std::unordered_map<std::string, std::vector<PluginCacheRow>> cache;
  for (auto &row : store.load())
    cache[row.path.toStdString()].push_back(std::move(row));

  std::set<std::string> presentPaths;
  for (const auto &bundle : bundles)
    presentPaths.insert(bundle.getFullPathName().toStdString());

  PluginCatalog catalog;
  for (const auto &bundle : bundles) {
    if (shouldExit && shouldExit())
      return catalog;

    const auto path = bundle.getFullPathName();
    const int64_t modTime = bundle.getLastModificationTime().toMilliseconds();
    const auto cached = cache.find(path.toStdString());
    const bool hasCache = cached != cache.end() && !cached->second.empty();

    if (hasCache && cached->second.front().modTime == modTime) {
      for (const auto &row : cached->second)
        addType(catalog, descriptionFromCacheRow(row), row.valid);
      continue;
    }

    std::cerr << "[PluginScanner] Scanning: " << path << std::endl;
    auto result = probe(bundle);
    using Outcome = PluginProbeResult::Outcome;

    if (result.outcome == Outcome::described && !result.descriptions.empty()) {
      for (const auto &desc : result.descriptions) {
        addType(catalog, desc, true);
        store.save(rowFromDescription(path, modTime, desc));
      }
      continue;
    }

    if (result.outcome == Outcome::noPlugins ||
        result.outcome == Outcome::described) {
      std::cerr << "[PluginScanner] No plug-ins in " << path << std::endl;
      addInvalidBundle(catalog, store, bundle, modTime);
      continue;
    }

    std::cerr << "[PluginScanner] Could not describe " << path << ": "
              << result.detail << std::endl;
    if (hasCache) {
      // Keep the last known description and its old modification time, so the
      // plug-in stays usable now and is described again next launch.
      for (const auto &row : cached->second)
        addType(catalog, descriptionFromCacheRow(row), row.valid);
    } else if (result.outcome == Outcome::failed) {
      addInvalidBundle(catalog, store, bundle, modTime);
    }
  }

  for (const auto &[path, rows] : cache) {
    if (presentPaths.find(path) == presentPaths.end())
      store.remove(juce::String(path));
  }
  return catalog;
}

juce::File defaultPluginScanHelper() {
  return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
      .getSiblingFile("FiddlePluginScan");
}

PluginProbeResult probePluginOutOfProcess(const juce::File &helper,
                                          const juce::File &bundle,
                                          int timeoutMs,
                                          const std::function<bool()> &shouldExit) {
  PluginProbeResult result;
  if (!helper.existsAsFile()) {
    result.outcome = PluginProbeResult::Outcome::unavailable;
    result.detail = "scan helper not found at " + helper.getFullPathName();
    return result;
  }

  juce::TemporaryFile resultFile(".xml");
  // A helper killed mid-write leaves the partial file writePluginScanResult
  // was about to rename.
  struct PartialCleanup {
    juce::File file;
    ~PartialCleanup() { file.deleteFile(); }
  } partial{resultFile.getFile().getSiblingFile(
      resultFile.getFile().getFileName() + ".partial")};
  juce::ChildProcess child;
  // Stream flags 0: the helper's (and the plug-in's) output goes to /dev/null,
  // so a chatty plug-in cannot block on a full pipe.
  if (!child.start(juce::StringArray{helper.getFullPathName(),
                                     bundle.getFullPathName(),
                                     resultFile.getFile().getFullPathName()},
                   0)) {
    result.outcome = PluginProbeResult::Outcome::unavailable;
    result.detail = "could not start " + helper.getFullPathName();
    return result;
  }

  const auto start = juce::Time::getMillisecondCounter();
  while (child.isRunning()) {
    const bool cancelled = shouldExit && shouldExit();
    const auto elapsed = juce::Time::getMillisecondCounter() - start;
    if (cancelled || elapsed > static_cast<juce::uint32>(timeoutMs)) {
      child.kill();
      child.waitForProcessToFinish(2000); // reap it
      result.detail = cancelled ? juce::String("scan cancelled")
                                : "no result after " +
                                      juce::String(timeoutMs / 1000) + " s";
      return result;
    }
    juce::Thread::sleep(20);
  }

  auto xml = juce::parseXML(resultFile.getFile());
  if (xml == nullptr || !xml->hasTagName(resultTag)) {
    result.detail = "helper exited (code " +
                    juce::String(static_cast<int>(child.getExitCode())) +
                    ") without a result";
    return result;
  }

  for (auto *element : xml->getChildIterator()) {
    juce::PluginDescription desc;
    if (desc.loadFromXml(*element))
      result.descriptions.push_back(desc);
  }
  result.outcome = result.descriptions.empty()
                       ? PluginProbeResult::Outcome::noPlugins
                       : PluginProbeResult::Outcome::described;
  return result;
}

bool writePluginScanResult(
    const juce::File &resultFile,
    const juce::OwnedArray<juce::PluginDescription> &descriptions) {
  juce::XmlElement root(resultTag);
  root.setAttribute("version", 1);
  for (const auto *desc : descriptions)
    root.addChildElement(desc->createXml().release());

  const auto partial = resultFile.getSiblingFile(resultFile.getFileName() +
                                                 ".partial");
  return root.writeTo(partial) && partial.moveFileTo(resultFile);
}

} // namespace fiddle
