#pragma once

#include "AudioSend.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <set>
#include <vector>

namespace fiddle {

/// One layer's output and sends, as saved before chair buses existed.
struct LegacyLayerRouting {
  juce::String output; // empty = Master
  std::vector<AudioSend> sends;
};

/// The chair bus routing that replaces a chair's per-layer routing.
struct MigratedChairRouting {
  juce::String output;
  std::vector<AudioSend> sends;
  /// False when the layers disagreed, so the chair now sounds different.
  bool exact = true;
};

/// Combine a chair's per-layer routing into one chair bus (docs/chair-buses.md,
/// "Persistence and migration"):
///
/// - Output: the layers' common output; if they differ, the most common one,
///   with Master winning a tie.
/// - Sends: one per destination any layer sent to. When every layer has the
///   same send to it, it is copied; otherwise the loudest enabled send (or the
///   loudest send if none is enabled) supplies level, tap and enablement.
///
/// A chair bus sends the whole chair, so a send that only some layers had,
/// or that differed between them, is not reproduced exactly.
inline MigratedChairRouting migrateChairRouting(
    const std::vector<LegacyLayerRouting> &layers,
    const std::function<juce::String()> &newId) {
  MigratedChairRouting result;
  if (layers.empty())
    return result;

  std::map<juce::String, int> outputCounts;
  for (const auto &layer : layers)
    ++outputCounts[layer.output];
  int best = -1;
  for (const auto &layer : layers) { // first-seen order breaks ties between buses
    const int count = outputCounts[layer.output];
    if (count > best) { best = count; result.output = layer.output; }
  }
  if (outputCounts.count({}) && outputCounts[{}] == best)
    result.output = {};
  if (outputCounts.size() > 1)
    result.exact = false;

  std::vector<juce::String> destinations;
  for (const auto &layer : layers)
    for (const auto &send : layer.sends)
      if (std::find(destinations.begin(), destinations.end(), send.busId) == destinations.end())
        destinations.push_back(send.busId);

  std::set<juce::String> usedIds;
  for (const auto &destination : destinations) {
    std::vector<const AudioSend *> found;
    for (const auto &layer : layers)
      for (const auto &send : layer.sends)
        if (send.busId == destination) { found.push_back(&send); break; }
    const bool everyLayer = found.size() == layers.size();
    const bool identical = std::all_of(found.begin(), found.end(), [&](const AudioSend *send) {
      return send->gainDb == found.front()->gainDb && send->enabled == found.front()->enabled &&
             send->preFader == found.front()->preFader;
    });
    if (!everyLayer || !identical)
      result.exact = false;

    const AudioSend *chosen = found.front();
    const bool anyEnabled = std::any_of(found.begin(), found.end(),
                                        [](const AudioSend *send) { return send->enabled; });
    for (const auto *send : found) {
      if (anyEnabled && !send->enabled) continue;
      if ((anyEnabled && !chosen->enabled) || send->gainDb > chosen->gainDb)
        chosen = send;
    }
    AudioSend merged = *chosen;
    merged.showOnStrip = std::any_of(found.begin(), found.end(),
                                     [](const AudioSend *send) { return send->showOnStrip; });
    if (merged.id.isEmpty() || !usedIds.insert(merged.id).second) {
      merged.id = newId();
      usedIds.insert(merged.id);
    }
    result.sends.push_back(std::move(merged));
  }
  return result;
}

} // namespace fiddle
