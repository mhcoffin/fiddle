#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include "BridgeReachability.h"
#include "BridgeRepairPlan.h"

namespace fiddle {

// A read-only catalog. Never inserts into ExpressionMapLibrary, installs a
// Dorico library, or resolves conflicting definitions by filename/version.
class TechniqueCatalog {
public:
  struct Source {
    juce::File path;
    juce::String kind; // builtin, installed, assigned, reference, additional, bridge
    juce::String xml;  // optional immutable project snapshot; path is then a label
  };
  struct Usage {
    juce::String mapId, label, scope, sourcePath; // project or library
  };
  struct Request {
    std::vector<Source> sources;
    std::vector<Usage> usage;
    std::vector<ExpressionMap::Dimension> decoder;
    std::set<int> reservedControllers;
    std::vector<BridgeReachability::Assignment> assignments;
    bool includeReference = false;
  };
  using Cancel = std::function<bool()>;
  using Progress = std::function<void(int)>;

  static juce::var scan(const Request &, Cancel cancelled = {}, Progress progress = {});
};

// Only immutable request snapshots cross onto this low-priority worker. No
// mixer, SQLite, plug-in or audio-thread access is allowed here.
class TechniqueCatalogScan final : private juce::Thread {
public:
  TechniqueCatalogScan() : Thread("Technique catalog") {}
  ~TechniqueCatalogScan() override { signalThreadShouldExit(); stopThread(-1); }
  bool start(TechniqueCatalog::Request request);
  bool previewRepairs(BridgeRepairPlan::Options options);
  void cancel() { signalThreadShouldExit(); }
  bool running() const { return isThreadRunning(); }
  juce::var state() const;
private:
  void run() override;
  TechniqueCatalog::Request request_;
  std::atomic<int> files_{0};
  mutable juce::CriticalSection lock_;
  juce::var result_;
  juce::var previewBase_;
  BridgeRepairPlan::Options repairOptions_;
  bool previewOnly_ = false;
};

} // namespace fiddle
