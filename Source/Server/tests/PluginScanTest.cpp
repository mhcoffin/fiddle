// Plug-in catalog scanning (PluginCatalogScan.h).
//
// Regression for 2026-11-04: Vienna Synchron Player was updated after its
// plugin_cache row was written, so every launch described it again inside
// FiddleServer while the layers were instantiating it. The duplicate instance
// aborted startup (spdlog "logger ... already exists") or stalled the scan, and
// the stale row was never updated. These tests check that a changed bundle is
// described once and its new modification time stored, that a failed or hung
// describer keeps the cached plug-in, and that describing happens in the
// FiddlePluginScan helper process, not in the caller's process.
//
// Usage: PluginScanTest <FiddlePluginScan> [<bundle.vst3 to describe>]

#include "PluginCatalogScan.h"

#include <mach-o/dyld.h>

#include <iostream>
#include <map>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition)                                                             \
      ++passed;                                                                \
    else {                                                                     \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__                     \
                << "]: " << #condition << std::endl;                           \
    }                                                                          \
  } while (false)

using fiddle::PluginCacheRow;
using fiddle::PluginProbeResult;
using Outcome = PluginProbeResult::Outcome;

class MemoryStore : public fiddle::PluginCacheStore {
public:
  std::vector<PluginCacheRow> load() override {
    std::vector<PluginCacheRow> out;
    for (const auto &[path, row] : rows)
      out.push_back(row);
    return out;
  }
  void save(const PluginCacheRow &row) override {
    rows[row.path.toStdString()] = row;
  }
  void remove(const juce::String &path) override {
    rows.erase(path.toStdString());
  }
  void clear() override { rows.clear(); }

  const PluginCacheRow *find(const juce::File &bundle) const {
    auto it = rows.find(bundle.getFullPathName().toStdString());
    return it == rows.end() ? nullptr : &it->second;
  }

  std::map<std::string, PluginCacheRow> rows;
};

struct TempDir {
  TempDir()
      : dir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getNonexistentChildFile("PluginScanTest", "")) {
    dir.createDirectory();
  }
  ~TempDir() { dir.deleteRecursively(); }
  juce::File dir;
};

juce::File makeBundle(const juce::File &dir, const juce::String &name,
                      juce::Time modified) {
  auto bundle = dir.getChildFile(name + ".vst3");
  bundle.getChildFile("Contents").createDirectory();
  bundle.setLastModificationTime(modified);
  return bundle;
}

juce::PluginDescription description(const juce::String &name, int uid,
                                    const juce::File &bundle) {
  juce::PluginDescription desc;
  desc.name = name;
  desc.manufacturerName = "VSL";
  desc.pluginFormatName = "VST3";
  desc.uniqueId = uid;
  desc.isInstrument = true;
  desc.numOutputChannels = 2;
  desc.fileOrIdentifier = bundle.getFullPathName();
  return desc;
}

PluginCacheRow cachedRow(const juce::File &bundle, int64_t modTime, int uid) {
  PluginCacheRow row;
  row.path = bundle.getFullPathName();
  row.modTime = modTime;
  row.name = bundle.getFileNameWithoutExtension();
  row.format = "VST3";
  row.uid = uid;
  row.isInstrument = true;
  row.valid = true;
  return row;
}

bool catalogHas(const fiddle::PluginCatalog &catalog, int uid) {
  for (const auto &desc : catalog.types)
    if (desc.uniqueId == uid)
      return true;
  return false;
}

int64_t modTimeOf(const juce::File &bundle) {
  return bundle.getLastModificationTime().toMilliseconds();
}

const juce::Time august(2026, 7, 27, 17, 19, 10); // months are 0-based
const juce::Time october(2026, 9, 30, 9, 32, 30);

void changedBundleIsDescribedOnceAndCached() {
  TempDir temp;
  auto player = makeBundle(temp.dir, "Vienna Synchron Player", october);
  auto unchanged = makeBundle(temp.dir, "Vienna Imager Pro", august);

  MemoryStore store;
  store.save(cachedRow(player, august.toMilliseconds(), 111));
  store.save(cachedRow(unchanged, modTimeOf(unchanged), 222));

  int probes = 0;
  fiddle::PluginProbe probe = [&](const juce::File &bundle) {
    ++probes;
    CHECK(bundle == player);
    PluginProbeResult result;
    result.outcome = Outcome::described;
    result.descriptions.push_back(
        description("Vienna Synchron Player", 333, bundle));
    return result;
  };

  auto catalog =
      fiddle::scanPluginBundles(store, {player, unchanged}, false, probe);
  CHECK(probes == 1);
  CHECK(catalogHas(catalog, 333));
  CHECK(catalogHas(catalog, 222));
  CHECK(catalog.validity[333]);
  const auto *row = store.find(player);
  CHECK(row != nullptr && row->modTime == modTimeOf(player));
  CHECK(row != nullptr && row->uid == 333);

  // Next launch: nothing changed, nothing described.
  catalog = fiddle::scanPluginBundles(store, {player, unchanged}, false, probe);
  CHECK(probes == 1);
  CHECK(catalogHas(catalog, 333));
}

void failedDescribeKeepsCachedPluginAndRetries() {
  TempDir temp;
  auto player = makeBundle(temp.dir, "Vienna Synchron Player", october);
  MemoryStore store;
  store.save(cachedRow(player, august.toMilliseconds(), 111));

  int probes = 0;
  fiddle::PluginProbe hung = [&](const juce::File &) {
    ++probes;
    PluginProbeResult result;
    result.outcome = Outcome::failed;
    result.detail = "no result after 60 s";
    return result;
  };

  auto catalog = fiddle::scanPluginBundles(store, {player}, false, hung);
  // Still offered in the layer drop-down, from the cache.
  CHECK(catalogHas(catalog, 111));
  CHECK(catalog.validity[111]);
  // Old modification time kept, so the next launch tries again.
  CHECK(store.find(player) != nullptr &&
        store.find(player)->modTime == august.toMilliseconds());
  fiddle::scanPluginBundles(store, {player}, false, hung);
  CHECK(probes == 2);
}

void failedDescribeWithoutCacheIsRecordedInvalid() {
  TempDir temp;
  auto crasher = makeBundle(temp.dir, "Crasher", october);
  MemoryStore store;
  fiddle::PluginProbe crash = [](const juce::File &) {
    return PluginProbeResult{}; // failed
  };
  auto catalog = fiddle::scanPluginBundles(store, {crasher}, false, crash);
  CHECK(catalog.types.size() == 1);
  CHECK(catalog.types.size() == 1 &&
        !catalog.validity[catalog.types.front().uniqueId]);
  CHECK(store.find(crasher) != nullptr && !store.find(crasher)->valid &&
        store.find(crasher)->modTime == modTimeOf(crasher));
}

void unavailableDescriberChangesNothing() {
  TempDir temp;
  auto fresh = makeBundle(temp.dir, "Fresh", october);
  auto player = makeBundle(temp.dir, "Vienna Synchron Player", october);
  MemoryStore store;
  store.save(cachedRow(player, august.toMilliseconds(), 111));
  fiddle::PluginProbe missing = [](const juce::File &) {
    PluginProbeResult result;
    result.outcome = Outcome::unavailable;
    return result;
  };
  auto catalog =
      fiddle::scanPluginBundles(store, {fresh, player}, false, missing);
  CHECK(catalog.types.size() == 1 && catalogHas(catalog, 111));
  CHECK(store.find(fresh) == nullptr);
  CHECK(store.find(player)->modTime == august.toMilliseconds());
}

void removedBundlesLeaveTheCacheUnlessCancelled() {
  TempDir temp;
  auto kept = makeBundle(temp.dir, "Kept", august);
  auto gone = temp.dir.getChildFile("Gone.vst3");
  MemoryStore store;
  store.save(cachedRow(kept, modTimeOf(kept), 1));
  store.save(cachedRow(gone, august.toMilliseconds(), 2));
  fiddle::PluginProbe never = [](const juce::File &) {
    CHECK(false);
    return PluginProbeResult{};
  };

  fiddle::scanPluginBundles(store, {kept}, false, never, [] { return true; });
  CHECK(store.find(gone) != nullptr); // cancelled scan removes nothing

  fiddle::scanPluginBundles(store, {kept}, false, never);
  CHECK(store.find(gone) == nullptr);
  CHECK(store.find(kept) != nullptr);
}

juce::File writeScript(const juce::File &dir, const juce::String &name,
                       const juce::String &body) {
  auto script = dir.getChildFile(name);
  script.replaceWithText("#!/bin/sh\n" + body + "\n");
  script.setExecutePermission(true);
  return script;
}

void hungHelperIsKilledAtTheTimeout() {
  TempDir temp;
  auto bundle = makeBundle(temp.dir, "Slow", october);
  auto helper = writeScript(temp.dir, "hang.sh", "exec sleep 30");
  const auto start = juce::Time::getMillisecondCounter();
  auto result = fiddle::probePluginOutOfProcess(helper, bundle, 300);
  const auto elapsed = juce::Time::getMillisecondCounter() - start;
  CHECK(result.outcome == Outcome::failed);
  CHECK(elapsed < 5000);

  bool cancel = false;
  auto cancelled = fiddle::probePluginOutOfProcess(
      helper, bundle, 60000, [&] { return cancel = true; });
  CHECK(cancelled.outcome == Outcome::failed);
}

void crashingHelperIsAFailure() {
  TempDir temp;
  auto bundle = makeBundle(temp.dir, "Crashy", october);
  auto helper = writeScript(temp.dir, "crash.sh", "kill -SEGV $$");
  auto result = fiddle::probePluginOutOfProcess(helper, bundle, 10000);
  CHECK(result.outcome == Outcome::failed);
  CHECK(result.descriptions.empty());

  auto missing = fiddle::probePluginOutOfProcess(
      temp.dir.getChildFile("NoSuchHelper"), bundle, 10000);
  CHECK(missing.outcome == Outcome::unavailable);
}

bool imageLoaded(const juce::String &fragment) {
  for (uint32_t i = 0; i < _dyld_image_count(); ++i)
    if (juce::String(_dyld_get_image_name(i)).contains(fragment))
      return true;
  return false;
}

void realHelperDescribesBundles(const juce::File &helper,
                                const juce::File &probeBundle) {
  CHECK(helper.existsAsFile());

  // A bundle with no binary loads nothing: the helper reports no plug-ins.
  TempDir temp;
  auto empty = makeBundle(temp.dir, "Empty", october);
  auto none = fiddle::probePluginOutOfProcess(helper, empty, 30000);
  CHECK(none.outcome == Outcome::noPlugins);

  if (probeBundle == juce::File()) {
    std::cerr << "(no probe bundle given; skipping real VST3 describe)"
              << std::endl;
    return;
  }
  CHECK(probeBundle.isDirectory());
  auto result = fiddle::probePluginOutOfProcess(helper, probeBundle, 30000);
  CHECK(result.outcome == Outcome::described);
  CHECK(!result.descriptions.empty());
  for (const auto &desc : result.descriptions) {
    CHECK(desc.pluginFormatName == "VST3");
    CHECK(desc.uniqueId != 0);
    CHECK(desc.fileOrIdentifier.isNotEmpty());
  }
  // Described in the helper, never loaded into this process.
  CHECK(!imageLoaded(probeBundle.getFileName()));
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "usage: PluginScanTest <FiddlePluginScan> [<bundle.vst3>]"
              << std::endl;
    return 2;
  }
  const juce::File helper(juce::String::fromUTF8(argv[1]));
  const juce::File probeBundle =
      argc > 2 && argv[2][0] != '\0' ? juce::File(juce::String::fromUTF8(argv[2]))
                                     : juce::File();

  changedBundleIsDescribedOnceAndCached();
  failedDescribeKeepsCachedPluginAndRetries();
  failedDescribeWithoutCacheIsRecordedInvalid();
  unavailableDescriberChangesNothing();
  removedBundlesLeaveTheCacheUnlessCancelled();
  hungHelperIsKilledAtTheTimeout();
  crashingHelperIsAFailure();
  realHelperDescribesBundles(helper, probeBundle);

  std::cout << "PluginScanTest: " << passed << " passed, " << failed
            << " failed" << std::endl;
  return failed == 0 ? 0 : 1;
}
