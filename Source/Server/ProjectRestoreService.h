#pragma once

#include "MixerModel.h"
#include "LibraryRoutingRepository.h"
#include "VersionStore.h"

#include <functional>
#include <memory>
#include <optional>

namespace fiddle {

/// Message-thread orchestration shared by version checkout and integration
/// tests. Resources/UI are supplied by the application; loading, topology and
/// saved processor state follow the same path in both environments.
class ProjectRestoreService {
public:
  struct Callbacks {
    std::function<std::optional<juce::PluginDescription>(int)> findInstrument;
    std::function<std::shared_ptr<ExpressionMapData>(
        const std::string &, const juce::String &)> loadMap;
    std::function<std::string(const std::string &)> resolveLua;
    std::function<void(MixerStrip &)> stripCreated;
    std::function<void(GroupBus &)> busCreated;
    std::function<void(MixerStrip &)> instrumentReady;
    /// Deferred until all asynchronous loads finish (missing plug-ins count
    /// as finished). Never called for a rejected or superseded restore.
    std::function<void()> finished;
  };
  struct Result {
    bool accepted = false;
    bool restoredTopology = false;
    std::string error;
  };

  ProjectRestoreService(MixerModel &, versioning::VersionStore &,
                        LibraryRoutingRepository *, Callbacks);
  ~ProjectRestoreService();
  Result restore(const versioning::FiddleState &);
  bool isLoading() const;

private:
  struct Session;
  MixerModel &mixer_;
  versioning::VersionStore &versions_;
  LibraryRoutingRepository *repository_;
  Callbacks callbacks_;
  std::shared_ptr<Session> session_;
};

} // namespace fiddle
