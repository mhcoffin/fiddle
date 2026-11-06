#pragma once

#include "ChairBusMigration.h"
#include "MixerModel.h"
#include "LibraryRoutingRepository.h"
#include "VersionStore.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
    /// Libraries made or extended for a version saved before chairs existed.
    std::vector<std::string> generatedLibraries;
    /// Chairs of a version saved before chair buses whose layers had different
    /// outputs or sends, so their combined chair routing is not exact.
    std::vector<std::string> inexactChairRouting;
  };

  ProjectRestoreService(MixerModel &, versioning::VersionStore &,
                        LibraryRoutingRepository *, Callbacks);
  ~ProjectRestoreService();
  Result restore(const versioning::FiddleState &);
  /// Restore only a version's audio buses and chair buses onto the layers
  /// already in the mixer, replacing any audio buses, and move a pre-chair-bus
  /// version's layer routing to its chair buses. At startup the layers come
  /// from the session database, but buses and routing are kept only in
  /// versions.
  Result restoreBusRouting(const versioning::FiddleState &);
  bool isLoading() const;

private:
  struct Session;
  std::shared_ptr<Session> beginSession();
  void restoreAudioRack(StripAudioEngine &, const std::vector<uint8_t> &,
                        const std::shared_ptr<Session> &);
  void restoreBuses(const versioning::FiddleState &,
                    const std::map<std::string, std::vector<AudioSend>> &chairSends,
                    const std::shared_ptr<Session> &);
  std::vector<std::string> migrateLegacyRouting(
      const std::map<juce::String, std::vector<LegacyLayerRouting>> &,
      const std::map<std::string, std::vector<AudioSend>> &chairSends,
      const std::map<juce::String, juce::String> &chairNames);
  MixerModel &mixer_;
  versioning::VersionStore &versions_;
  LibraryRoutingRepository *repository_;
  Callbacks callbacks_;
  std::shared_ptr<Session> session_;
};

} // namespace fiddle
