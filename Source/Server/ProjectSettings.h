#pragma once

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>

namespace fiddle {

struct ProjectSettings {
  int playbackDelayMs = 1000;
  std::set<std::string> lockedChairIds;
  bool operator==(const ProjectSettings &other) const {
    return playbackDelayMs == other.playbackDelayMs &&
           lockedChairIds == other.lockedChairIds;
  }

  std::string serialize() const {
    std::ostringstream stream;
    stream << playbackDelayMs << '\n';
    for (const auto &id : lockedChairIds)
      stream << std::quoted(id) << '\n';
    return stream.str();
  }
  static ProjectSettings deserialize(const std::string &text) {
    ProjectSettings settings;
    std::istringstream stream(text);
    int delay = 1000;
    if (!(stream >> delay))
      return settings;
    settings.playbackDelayMs = std::clamp(delay, 0, 5000);
    std::string id;
    while (stream >> std::quoted(id))
      if (!id.empty()) settings.lockedChairIds.insert(id);
    return settings;
  }
};

} // namespace fiddle
