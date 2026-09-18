#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <set>
#include <vector>

namespace fiddle {

// Message-thread state. The renderer uses resolved graph edges, never these
// strings. Sends tap the completed strip rack (after fader and post-fader FX).
struct AudioSend {
  juce::String id, busId;
  float gainDb = -12.0f;
  bool enabled = true;
  bool showOnStrip = false;
  bool sameAudioRoute(const AudioSend &other) const {
    return id == other.id && busId == other.busId &&
           gainDb == other.gainDb && enabled == other.enabled;
  }
  bool operator==(const AudioSend &other) const {
    return sameAudioRoute(other) && showOnStrip == other.showOnStrip;
  }
};

inline juce::var audioSendsToJson(const std::vector<AudioSend> &sends) {
  juce::Array<juce::var> result;
  for (const auto &send : sends) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("id", send.id);
    obj->setProperty("busId", send.busId);
    obj->setProperty("gainDb", send.gainDb);
    obj->setProperty("enabled", send.enabled);
    // Omit the default to preserve hashes of existing send configurations.
    if (send.showOnStrip) obj->setProperty("showOnStrip", true);
    result.add(juce::var(obj));
  }
  return result;
}

inline bool audioSendsFromJson(const juce::var &value,
                               std::vector<AudioSend> &result) {
  const auto *array = value.getArray();
  if (!array) return false;
  std::vector<AudioSend> parsed;
  std::set<juce::String> ids, destinations;
  for (const auto &item : *array) {
    if (!item.isObject() || !item["id"].isString() ||
        !item["busId"].isString() || !item["enabled"].isBool() ||
        !(item["gainDb"].isDouble() || item["gainDb"].isInt() || item["gainDb"].isInt64()))
      return false;
    AudioSend send{item["id"].toString(), item["busId"].toString(),
                   static_cast<float>(double(item["gainDb"])), bool(item["enabled"])};
    if (item.hasProperty("showOnStrip")) {
      if (!item["showOnStrip"].isBool()) return false;
      send.showOnStrip = bool(item["showOnStrip"]);
    }
    if (send.id.isEmpty() || send.busId.isEmpty() ||
        !std::isfinite(send.gainDb) || send.gainDb < -120 || send.gainDb > 12 ||
        !ids.insert(send.id).second || !destinations.insert(send.busId).second)
      return false;
    parsed.push_back(std::move(send));
  }
  result = std::move(parsed);
  return true;
}

} // namespace fiddle
