#pragma once

#include "DoricoPlaybackOptions.h"

#include <algorithm>
#include <iomanip>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace fiddle {

struct ChairLevelState {
  double targetDb = 0;
  std::map<std::string, double> weights;
  bool operator==(const ChairLevelState &other) const {
    return targetDb == other.targetDb && weights == other.weights;
  }
};

struct ProjectSettings {
  int playbackDelayMs = 1000;
  std::set<std::string> lockedChairIds;
  std::map<std::string, ChairLevelState> chairLevels;
  // Immutable bridge documents and endpoint assignments. No live verification
  // status is persisted: reopening a score requires a fresh Dorico export check.
  std::string bridgeMapsJson;
  // A copy of the Dorico project's note-duration Playback Options. Bridge
  // maps pin Dorico's to 100%, so layers apply these (unless their own
  // expression map overrides a category).
  DurationPercents doricoDurationPercents = kDoricoDefaultDurationPercents;
  // The project's Dorico "Expression map switches before notes" (ticks),
  // applied to each layer's own switches (bridge maps pin Dorico's).
  int doricoSwitchLeadTicks = kDoricoDefaultSwitchLeadTicks;
  // The project's Dorico dynamics options, applied to each layer's dynamics
  // (bridge maps pin a linear curve) unless the layer's map overrides them.
  DoricoDynamics doricoDynamics;
  bool operator==(const ProjectSettings &other) const {
    return playbackDelayMs == other.playbackDelayMs &&
           lockedChairIds == other.lockedChairIds && chairLevels == other.chairLevels &&
           bridgeMapsJson == other.bridgeMapsJson &&
           doricoDurationPercents == other.doricoDurationPercents &&
           doricoSwitchLeadTicks == other.doricoSwitchLeadTicks &&
           doricoDynamics == other.doricoDynamics;
  }

  std::string serialize() const {
    std::ostringstream stream;
    // V4 only when Dorico timing differs from its defaults, so older projects
    // keep their exact serialized form.
    const bool version4 = doricoDurationPercents != kDoricoDefaultDurationPercents ||
                          doricoSwitchLeadTicks != kDoricoDefaultSwitchLeadTicks ||
                          doricoDynamics != DoricoDynamics{};
    stream << (version4 ? "FIDDLE_SETTINGS_V4\n"
               : bridgeMapsJson.empty() ? "FIDDLE_SETTINGS_V2\n" : "FIDDLE_SETTINGS_V3\n") << std::setprecision(17)
           << playbackDelayMs << '\n' << lockedChairIds.size() << '\n';
    for (const auto &id : lockedChairIds)
      stream << std::quoted(id) << '\n';
    stream << chairLevels.size() << '\n';
    for (const auto &[id, level] : chairLevels) {
      stream << std::quoted(id) << ' ' << level.targetDb << ' ' << level.weights.size() << '\n';
      for (const auto &[strip, weight] : level.weights)
        stream << std::quoted(strip) << ' ' << weight << '\n';
    }
    if (version4 || !bridgeMapsJson.empty()) stream << std::quoted(bridgeMapsJson) << '\n';
    if (version4) {
      for (const int percent : doricoDurationPercents) stream << percent << ' ';
      stream << doricoSwitchLeadTicks << '\n';
      const auto &d = doricoDynamics;
      stream << d.midpoint << ' ' << d.lowerPower << ' ' << d.upperPower << ' ' << d.minLevel << ' '
             << d.maxLevel << ' ' << d.accent << ' ' << d.marcato << ' ' << d.tenuto << ' '
             << d.ghost << '\n';
    }
    return stream.str();
  }
  static ProjectSettings deserialize(const std::string &text) {
    ProjectSettings settings;
    std::istringstream stream(text);
    const bool version4 = text.rfind("FIDDLE_SETTINGS_V4\n", 0) == 0;
    const bool version3 = version4 || text.rfind("FIDDLE_SETTINGS_V3\n", 0) == 0;
    if (version3) settings.bridgeMapsJson = "invalid truncated bridge project";
    const bool version2 = version3 || text.rfind("FIDDLE_SETTINGS_V2\n", 0) == 0;
    if (version2) { std::string header; std::getline(stream, header); }
    int delay = 1000;
    if (!(stream >> delay)) return settings;
    settings.playbackDelayMs = std::clamp(delay, 0, 5000);
    std::string id;
    if (version2) {
      size_t count = 0;
      if (!(stream >> count) || count > 100000) return settings;
      for (size_t i = 0; i < count; ++i) {
        if (!(stream >> std::quoted(id))) return settings;
        if (!id.empty()) settings.lockedChairIds.insert(id);
      }
      if (!(stream >> count) || count > 100000) return settings;
      for (size_t i = 0; i < count; ++i) {
        ChairLevelState level;
        size_t weights = 0;
        if (!(stream >> std::quoted(id) >> level.targetDb >> weights) || weights > 100000)
          return settings;
        if (!std::isfinite(level.targetDb) || level.targetDb < -120 || level.targetDb > 60)
          return settings;
        for (size_t j = 0; j < weights; ++j) {
          std::string strip;
          double weight;
          if (!(stream >> std::quoted(strip) >> weight) || !std::isfinite(weight) || weight < 0 || weight > 1e6)
            return settings;
          level.weights[strip] = weight;
        }
        if (settings.lockedChairIds.count(id)) settings.chairLevels[id] = std::move(level);
      }
      if (version3 && (!(stream >> std::quoted(settings.bridgeMapsJson)) ||
                       (!version4 && settings.bridgeMapsJson.empty())))
        settings.bridgeMapsJson = "invalid truncated bridge project";
      if (version4) {
        DurationPercents durations{};
        for (auto &percent : durations)
          if (!(stream >> percent) || percent < kMinDurationPercent || percent > kMaxDurationPercent)
            return settings; // keep Dorico's defaults
        settings.doricoDurationPercents = durations;
        int lead = kDoricoDefaultSwitchLeadTicks;
        if (stream >> lead && lead >= 0 && lead <= kMaxSwitchLeadTicks)
          settings.doricoSwitchLeadTicks = lead;
        DoricoDynamics d;
        if (stream >> d.midpoint >> d.lowerPower >> d.upperPower >> d.minLevel >> d.maxLevel >>
                d.accent >> d.marcato >> d.tenuto >> d.ghost &&
            d.midpoint > 0 && d.midpoint < 1 && d.lowerPower > 0 && d.upperPower > 0 &&
            d.minLevel < d.maxLevel)
          settings.doricoDynamics = d;
      }
      return settings;
    }
    while (stream >> std::quoted(id))
      if (!id.empty()) settings.lockedChairIds.insert(id);
    return settings;
  }
};

} // namespace fiddle
