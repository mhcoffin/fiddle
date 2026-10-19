#pragma once
#include "midi_event.pb.h"
#include <algorithm>
#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace fiddle {

// Decoder schema 1: independent CC/velocity-key dimensions and explicit base
// length buckets. This is compiled from XML, never from a hand-edited lookup.
struct CompiledBridgeMap {
  struct Value {
    std::set<std::string> techniques;
    int length = -1;
    bool ccDynamics = true;
  };
  struct Dimension {
    bool key = false;
    int number = -1, reset = -1;
    std::map<int, Value> values;
  };
  std::vector<Dimension> dimensions;
  int musicalLow = 21, musicalHigh = 114;
};

struct BridgeMapDocument {
  static constexpr int schema = 1;
  juce::String sourceXml, entityId, name, contentHash;
  int revision = 0;
  int musicalLow = 21, musicalHigh = 114;
  std::shared_ptr<const CompiledBridgeMap> compiled;

  static std::shared_ptr<const BridgeMapDocument> compile(
      const juce::String &xml, juce::String &error, int low = 21, int high = 114);
  juce::var toJson() const;
};

struct BridgeMapProject {
  using Endpoint = std::pair<int, int>; // port 0..15, channel 1..16
  std::map<juce::String, std::shared_ptr<const BridgeMapDocument>> documents;
  std::map<Endpoint, juce::String> assignments;
  juce::String serialize() const;
  static bool parse(const juce::String &, BridgeMapProject &, juce::String &error);
};

// Each channel owns its state; commands never escape to hosted instruments.
// No state is assumed until received from Dorico. A matching export is a
// user-attested assignment check, not automatic access to Dorico's project.
class BridgeDecoderState {
public:
  explicit BridgeDecoderState(std::shared_ptr<const CompiledBridgeMap> map)
      : map_(std::move(map)), values_(map_->dimensions.size(), -1) {}
  bool verified = false;
  void reset() { std::fill(values_.begin(), values_.end(), -1); }
  bool consumesCC(int number) const {
    for (const auto &d : map_->dimensions)
      if (!d.key && d.number == number) return true;
    return false;
  }
  bool consume(const MidiEvent &event) {
    for (size_t i = 0; i < values_.size(); ++i) {
      const auto &d = map_->dimensions[i];
      int value = -1;
      if (!d.key && event.has_cc() && int(event.cc().controller_number()) == d.number)
        value = int(event.cc().controller_value());
      else if (d.key && event.has_note_on() && int(event.note_on().note_number()) == d.number) {
        if (event.note_on().velocity() == 0) return true;
        value = int(event.note_on().velocity());
      } else if (d.key && event.has_note_off() && int(event.note_off().note_number()) == d.number)
        return true;
      else continue;
      values_[i] = d.values.count(value) ? value : -1;
      return true;
    }
    return false;
  }
  bool annotate(Note &note) const {
    if (!verified || std::find(values_.begin(), values_.end(), -1) != values_.end()) return false;
    if (int(note.note_number()) < map_->musicalLow || int(note.note_number()) > map_->musicalHigh) return false;
    for (size_t i = 0; i < values_.size(); ++i) {
      const auto &d = map_->dimensions[i];
      const auto &v = d.values.at(values_[i]);
      const auto key = std::string(d.key ? "KS" : "CC") + std::to_string(d.number);
      (*note.mutable_notation_dimensions())[key] = float(values_[i]);
      if (v.length >= 0) {
        (*note.mutable_notation_dimensions())["dorico_length_category"] = float(v.length);
        note.set_dynamics_mode(v.ccDynamics ? Note::CC : Note::VELOCITY);
      }
      for (const auto &id : v.techniques)
        (*note.mutable_notation_techniques())[key + ":" + id] = id;
    }
    return true;
  }
private:
  std::shared_ptr<const CompiledBridgeMap> map_;
  std::vector<int> values_;
};
} // namespace fiddle
