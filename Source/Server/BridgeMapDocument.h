#pragma once
#include "DoricoPlaybackOptions.h"
#include "midi_event.pb.h"
#include <algorithm>
#include <array>
#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace fiddle {

// Decoder schemas compiled from XML, never from a hand-edited lookup.
//   1: independent CC/velocity-key dimensions; every Off writes the reset.
//   2: as 1, but each Off names the technique it ends (Off = On + 64), and
//      Init carries a revision beacon on two reserved keyswitch pitches.
struct CompiledBridgeMap {
  static constexpr int typedOffOffset = 64;
  static constexpr std::array<int, 2> beaconPitches{126, 127};
  struct Value {
    std::set<std::string> techniques;
    int length = -1;
    bool ccDynamics = true;
  };
  struct Dimension {
    bool key = false;
    int number = -1, reset = -1;
    std::map<int, Value> values;  // reset and On values
    std::map<int, int> typedOffs; // schema 2: Off value -> the On value it ends
  };
  int schema = 1;
  std::vector<Dimension> dimensions;
  int musicalLow = 21, musicalHigh = 114;
  std::array<int, 2> beacon{-1, -1}; // expected beacon velocities (schema 2)
  // Dorico's playback options are pinned (see kPinnedPlaybackDimension).
  bool pinnedPlayback = false;
};

struct BridgeMapDocument {
  static constexpr int latestSchema = 2;
  juce::String sourceXml, entityId, name, contentHash;
  // Identity of everything Dorico or the decoder act on, independent of XML
  // formatting, row order and display text. Used to compare Dorico exports.
  juce::String semanticHash;
  int schema = 1;
  int revision = 0;
  int musicalLow = 21, musicalHigh = 114;
  std::shared_ptr<const CompiledBridgeMap> compiled;

  static std::shared_ptr<const BridgeMapDocument> compile(
      const juce::String &xml, juce::String &error, int low = 21, int high = 114);
  // Beacon velocities (1..127 each) derived from the Dorico map identity.
  static std::array<int, 2> beaconFor(const juce::String &entityId, int revision);
  // Rewrite schema-2 beacon actions to match the map's entityID/version.
  // Returns false when the XML has no beacon (schema 1); never adds one.
  static bool stampBeacon(juce::XmlElement &expressionMapDefinition);
  juce::var toJson() const;
};

struct BridgeMapProject {
  using Endpoint = std::pair<int, int>; // port 0..15, channel 1..16
  std::map<juce::String, std::shared_ptr<const BridgeMapDocument>> documents;
  std::map<Endpoint, juce::String> assignments;
  // Section key (BridgeSections.h) -> pinned revision. Template installation
  // derives assignments from chairs; a branch keeps its pinned revisions.
  std::map<juce::String, juce::String> sections;
  juce::String serialize() const;
  static bool parse(const juce::String &, BridgeMapProject &, juce::String &error);
};

// Each channel owns its state; commands never escape to hosted instruments.
// No base state is assumed until received from Dorico. Notes play once the
// user attests a matching export, or the schema-2 Init beacon matches.
class BridgeDecoderState {
public:
  enum class Withheld { None, Unverified, BeaconMismatch, BaseUnknown, InvalidCommand, OutsideRange, Count };
  enum class Beacon { Unsupported, Absent, Matched, Mismatched };
  static constexpr double offDebounceMs = 30.0;
  struct Diagnostics {
    Beacon beacon = Beacon::Unsupported;
    std::array<int, 2> receivedBeacon{-1, -1}, expectedBeacon{-1, -1};
    bool attested = false;
    uint64_t invalidCommands = 0, ignoredOffs = 0, debouncedOffs = 0;
    std::array<uint64_t, size_t(Withheld::Count)> withheld{};
    Withheld lastWithheld = Withheld::None;
  };

  explicit BridgeDecoderState(std::shared_ptr<const CompiledBridgeMap> map)
      : map_(std::move(map)), values_(map_->dimensions.size(), unknown),
        lastOn_(map_->dimensions.size()) {
    diagnostics_.expectedBeacon = map_->beacon;
    diagnostics_.beacon = map_->schema >= 2 ? Beacon::Absent : Beacon::Unsupported;
  }

  bool attested = false;
  double sampleRate = 44100.0;

  // Clears received protocol state (transport stop, verification change).
  // Cumulative counters survive until the assignment itself is replaced.
  void reset() {
    std::fill(values_.begin(), values_.end(), unknown);
    std::fill(lastOn_.begin(), lastOn_.end(), LastOn{});
    diagnostics_.receivedBeacon = {-1, -1};
    if (map_->schema >= 2) diagnostics_.beacon = Beacon::Absent;
  }
  bool consumesCC(int number) const {
    for (const auto &d : map_->dimensions)
      if (!d.key && d.number == number) return true;
    return false;
  }
  bool playable() const {
    return diagnostics_.beacon != Beacon::Mismatched && (attested || diagnostics_.beacon == Beacon::Matched);
  }

  bool consume(const MidiEvent &event) {
    if (map_->schema >= 2 && consumeBeacon(event)) return true;
    for (size_t i = 0; i < values_.size(); ++i) {
      const auto &d = map_->dimensions[i];
      int value = -1;
      if (!d.key && event.has_cc() && int(event.cc().controller_number()) == d.number)
        value = int(event.cc().controller_value());
      else if (d.key && event.has_note_on() && int(event.note_on().note_number()) == d.number) {
        if (event.note_on().velocity() == 0) return true; // physical release
        value = int(event.note_on().velocity());
      } else if (d.key && event.has_note_off() && int(event.note_off().note_number()) == d.number)
        return true;
      else continue;
      apply(i, value, position(event));
      return true;
    }
    return false;
  }

  Withheld annotate(Note &note) {
    const auto reason = check(note);
    if (reason != Withheld::None) {
      ++diagnostics_.withheld[size_t(reason)];
      diagnostics_.lastWithheld = reason;
      return reason;
    }
    for (size_t i = 0; i < values_.size(); ++i) {
      const auto &d = map_->dimensions[i];
      // Never-received add-ons are unspecified: Init writes exactly that.
      const int current = values_[i] == unknown ? d.reset : values_[i];
      const auto &v = d.values.at(current);
      const auto key = std::string(d.key ? "KS" : "CC") + std::to_string(d.number);
      (*note.mutable_notation_dimensions())[key] = float(current);
      if (v.length >= 0) {
        (*note.mutable_notation_dimensions())["dorico_length_category"] = float(v.length);
        note.set_dynamics_mode(v.ccDynamics ? Note::CC : Note::VELOCITY);
      }
      for (const auto &id : v.techniques)
        (*note.mutable_notation_techniques())[key + ":" + id] = id;
    }
    if (map_->pinnedPlayback)
      (*note.mutable_notation_dimensions())[kPinnedPlaybackDimension] = 1.0f;
    diagnostics_.lastWithheld = Withheld::None;
    return Withheld::None;
  }

  Diagnostics diagnostics() const {
    auto result = diagnostics_;
    result.attested = attested;
    return result;
  }
  static const char *describe(Withheld reason) {
    switch (reason) {
    case Withheld::None: return "";
    case Withheld::Unverified: return "map not verified (no matching beacon or attested export)";
    case Withheld::BeaconMismatch: return "Dorico is playing a different bridge revision";
    case Withheld::BaseUnknown: return "base articulation not received (start playback so Dorico sends Init)";
    case Withheld::InvalidCommand: return "an unknown command value invalidated a dimension";
    case Withheld::OutsideRange: return "note outside the bridge's musical range";
    case Withheld::Count: break;
    }
    return "";
  }

private:
  static constexpr int unknown = -1, invalid = -2;
  struct LastOn { int value = -1; uint64_t position = 0; };

  static uint64_t position(const MidiEvent &event) {
    return event.has_host_sample_position() ? event.host_sample_position() : event.timestamp_samples();
  }
  bool withinDebounce(size_t i, int on, uint64_t at) const {
    const auto &last = lastOn_[i];
    const auto window = uint64_t(offDebounceMs * std::max(1.0, sampleRate) / 1000.0);
    return last.value == on && at >= last.position && at - last.position < window;
  }
  void apply(size_t i, int value, uint64_t at) {
    const auto &d = map_->dimensions[i];
    if (const auto off = d.typedOffs.find(value); off != d.typedOffs.end()) {
      // A typed Off ends only the technique it names. Dorico can emit the
      // previous combination's Off after pre-rolling the next On; that Off
      // must not clear the successor or a just-restated technique.
      if (values_[i] != off->second) ++diagnostics_.ignoredOffs;
      else if (withinDebounce(i, off->second, at)) ++diagnostics_.debouncedOffs;
      else values_[i] = d.reset;
      return;
    }
    if (!d.values.count(value)) {
      values_[i] = invalid; ++diagnostics_.invalidCommands; return;
    }
    if (value == d.reset && i != 0 && map_->schema < 2 && values_[i] >= 0 && values_[i] != d.reset
        && withinDebounce(i, values_[i], at)) {
      // Schema 1 cannot tell which technique an Off ends; still refuse to
      // clear a technique restated moments before.
      ++diagnostics_.debouncedOffs;
      return;
    }
    values_[i] = value;
    if (value != d.reset) lastOn_[i] = {value, at};
  }
  bool consumeBeacon(const MidiEvent &event) {
    const auto &pitches = CompiledBridgeMap::beaconPitches;
    const int pitch = event.has_note_on() ? int(event.note_on().note_number())
        : event.has_note_off() ? int(event.note_off().note_number()) : -1;
    const auto slot = std::find(pitches.begin(), pitches.end(), pitch);
    if (slot == pitches.end()) return false;
    if (!event.has_note_on() || event.note_on().velocity() == 0) return true;
    auto &received = diagnostics_.receivedBeacon;
    received[size_t(slot - pitches.begin())] = int(event.note_on().velocity());
    if (received[0] >= 0 && received[1] >= 0)
      diagnostics_.beacon = received == map_->beacon ? Beacon::Matched : Beacon::Mismatched;
    return true;
  }
  Withheld check(const Note &note) const {
    if (int(note.note_number()) < map_->musicalLow || int(note.note_number()) > map_->musicalHigh)
      return Withheld::OutsideRange;
    if (diagnostics_.beacon == Beacon::Mismatched) return Withheld::BeaconMismatch;
    if (!playable()) return Withheld::Unverified;
    if (values_[0] == invalid) return Withheld::InvalidCommand;
    if (values_[0] == unknown) return Withheld::BaseUnknown;
    if (std::find(values_.begin(), values_.end(), invalid) != values_.end()) return Withheld::InvalidCommand;
    return Withheld::None;
  }

  std::shared_ptr<const CompiledBridgeMap> map_;
  std::vector<int> values_;
  std::vector<LastOn> lastOn_;
  Diagnostics diagnostics_;
};
} // namespace fiddle
