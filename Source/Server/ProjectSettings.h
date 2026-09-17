#pragma once

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
  bool operator==(const ProjectSettings &other) const {
    return playbackDelayMs == other.playbackDelayMs &&
           lockedChairIds == other.lockedChairIds && chairLevels == other.chairLevels;
  }

  std::string serialize() const {
    std::ostringstream stream;
    stream << "FIDDLE_SETTINGS_V2\n" << std::setprecision(17)
           << playbackDelayMs << '\n' << lockedChairIds.size() << '\n';
    for (const auto &id : lockedChairIds)
      stream << std::quoted(id) << '\n';
    stream << chairLevels.size() << '\n';
    for (const auto &[id, level] : chairLevels) {
      stream << std::quoted(id) << ' ' << level.targetDb << ' ' << level.weights.size() << '\n';
      for (const auto &[strip, weight] : level.weights)
        stream << std::quoted(strip) << ' ' << weight << '\n';
    }
    return stream.str();
  }
  static ProjectSettings deserialize(const std::string &text) {
    ProjectSettings settings;
    std::istringstream stream(text);
    const bool version2 = text.rfind("FIDDLE_SETTINGS_V2\n", 0) == 0;
    if (version2) { std::string header; std::getline(stream, header); }
    int delay = 1000;
    if (!(stream >> delay))
      return settings;
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
      return settings;
    }
    while (stream >> std::quoted(id))
      if (!id.empty()) settings.lockedChairIds.insert(id);
    return settings;
  }
};

} // namespace fiddle
