#pragma once

#include "midi_event.pb.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace fiddle {

// Opt-in decoder for the measured Strings F r6 experiment, NOT a replacement
// for the bundled legacy bridge. Parse/validate before MIDI starts. Profiles
// are immutable; each port/channel owns separate state in NoteStreamTracker.
class ExperimentalBridgeProfile {
public:
  struct Value {
    std::set<std::string> techniques;
    int length = -1;
  };
  struct Dimension {
    std::string name;
    int cc = -1, pitch = -1;
    std::map<int, Value> values;
  };
  const std::vector<Dimension> &dimensions() const { return dimensions_; }

  static std::shared_ptr<const ExperimentalBridgeProfile>
  parse(const juce::var &json, juce::String &error) {
    error.clear();
    try {
      require(json["schema_version"].isInt() && int(json["schema_version"]) == 4,
              "Expected decoder schema 4");
      require(json["profile_id"].toString() == "fiddle.experimental.strings.v1"
                  && json["variant"].toString() == "high-vibrato"
                  && integer(json["map_revision"], 6, 6) == 6
                  && integer(json["base_cc"], 102, 102) == 102,
              "Only the experimental Strings F r6 profile is supported");
      auto result = std::make_shared<ExperimentalBridgeProfile>();
      const auto *range = json["musical_range"].getArray();
      require(range && range->size() == 2, "Missing musical range");
      const int low = integer((*range)[0], 0, 127);
      const int high = integer((*range)[1], low, 127);
      std::set<std::string> names;
      std::set<int> pitches, controllers;
      auto add = [&](Dimension dimension) {
        require(!dimension.name.empty() && names.insert(dimension.name).second,
                "Empty or duplicate dimension name");
        require(!dimension.values.empty(), "Empty dimension");
        if (dimension.pitch >= 0) {
          require(dimension.pitch < low || dimension.pitch > high,
                  "Keyswitch overlaps declared musical range");
          require(pitches.insert(dimension.pitch).second, "Shared keyswitch pitch");
        } else {
          require(controllers.insert(dimension.cc).second, "Shared controller");
        }
        result->dimensions_.push_back(std::move(dimension));
      };
      const auto *tables = json["tables"].getDynamicObject();
      require(tables != nullptr, "Missing controller tables");
      for (const auto &entry : tables->getProperties()) {
        Dimension d;
        d.cc = numberKey(entry.name.toString(), 102, 119);
        d.name = entry.value["name"].toString().toStdString();
        const auto *values = entry.value["values"].getDynamicObject();
        require(values != nullptr, "Missing controller values");
        for (const auto &v : values->getProperties()) {
          Value value;
          const auto *techniques = v.value["techniques"].getArray();
          require(techniques != nullptr, "Missing technique set");
          for (const auto &t : *techniques) value.techniques.insert(technique(t));
          if (d.cc == 102) {
            const juce::StringArray lengths{"kVeryShort", "kShort", "kMedium", "kLong", "kVeryLong"};
            value.length = lengths.indexOf(v.value["length"].toString());
            require(value.length >= 0, "Missing/invalid base length");
          } else {
            require(v.value["length"].isVoid(), "Length outside base controller");
          }
          d.values.emplace(numberKey(v.name.toString(), 0, 127), std::move(value));
        }
        add(std::move(d));
      }
      require(controllers.count(102) != 0, "Missing base controller");
      const auto *switches = json["key_switches"].getArray();
      require(switches != nullptr, "Missing binary keyswitches");
      for (const auto &s : *switches) {
        Dimension d;
        d.name = s["axis_id"].toString().toStdString();
        d.pitch = integer(s["on_note"], 0, 127);
        require(integer(s["off_note"], 0, 127) == d.pitch,
                "This profile requires one pitch per dimension");
        const int on = integer(s["on_velocity"], 1, 127);
        const int off = integer(s["off_velocity"], 1, 127);
        require(on != off, "Ambiguous binary velocity");
        d.values.emplace(on, Value{{technique(s["technique"])}, -1});
        d.values.emplace(off, Value{});
        add(std::move(d));
      }
      const auto *scalars = json["key_dimensions"].getArray();
      require(scalars != nullptr, "Missing scalar dimensions");
      for (const auto &s : *scalars) {
        Dimension d;
        d.name = s["axis_id"].toString().toStdString();
        d.pitch = integer(s["note"], 0, 127);
        const auto *values = s["values"].getDynamicObject();
        require(values != nullptr, "Missing scalar values");
        for (const auto &v : values->getProperties()) {
          Value value;
          if (!v.value.isVoid()) value.techniques.insert(technique(v.value));
          d.values.emplace(numberKey(v.name.toString(), 1, 127), std::move(value));
        }
        add(std::move(d));
      }
      require(result->dimensions_.size() == 8, "Expected eight r6 dimensions");
      return result;
    } catch (const std::exception &e) {
      error = e.what();
      return {};
    }
  }

private:
  static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
  }
  static int integer(const juce::var &v, int low, int high) {
    require(v.isInt() || v.isInt64(), "Expected integer");
    const auto n = static_cast<juce::int64>(v);
    require(n >= low && n <= high, "Integer out of range");
    return int(n);
  }
  static int numberKey(const juce::String &s, int low, int high) {
    require(s.isNotEmpty() && s.containsOnly("0123456789"), "Invalid numeric key");
    const int n = s.getIntValue();
    require(s == juce::String(n) && n >= low && n <= high, "Numeric key out of range");
    return n;
  }
  static std::string technique(const juce::var &v) {
    require(v.isString() && v.toString().startsWith("pt.") && v.toString().length() > 3,
            "Expected canonical playback technique ID");
    return v.toString().toStdString();
  }
  std::vector<Dimension> dimensions_;
};

class ExperimentalBridgeState {
public:
  explicit ExperimentalBridgeState(std::shared_ptr<const ExperimentalBridgeProfile> profile)
      : profile_(std::move(profile)), values_(profile_->dimensions().size(), -1) {}

  void reset() { std::fill(values_.begin(), values_.end(), -1); }
  bool consumesCC(int cc) const {
    for (const auto &d : profile_->dimensions()) if (d.cc == cc) return true;
    return false;
  }
  // Return true for every message on a reserved conduit, including releases
  // and unknown velocities. Unknown values invalidate state, never sound notes.
  bool consume(const MidiEvent &event) {
    for (size_t i = 0; i < values_.size(); ++i) {
      const auto &d = profile_->dimensions()[i];
      int value = -1;
      if (d.cc >= 0 && event.has_cc() && int(event.cc().controller_number()) == d.cc)
        value = int(event.cc().controller_value());
      else if (d.pitch >= 0 && event.has_note_on() && int(event.note_on().note_number()) == d.pitch) {
        if (event.note_on().velocity() == 0) return true;
        value = int(event.note_on().velocity());
      } else if (d.pitch >= 0 && event.has_note_off() && int(event.note_off().note_number()) == d.pitch)
        return true;
      else continue;
      values_[i] = d.values.count(value) ? value : -1;
      if (values_[i] < 0) ++invalidCommands;
      return true;
    }
    return false;
  }
  bool annotate(Note &note) const {
    if (std::find(values_.begin(), values_.end(), -1) != values_.end()) return false;
    for (size_t i = 0; i < values_.size(); ++i) {
      const auto &d = profile_->dimensions()[i];
      const auto &v = d.values.at(values_[i]);
      (*note.mutable_notation_dimensions())[d.name] = float(values_[i]);
      if (v.length >= 0)
        (*note.mutable_notation_dimensions())["dorico_length_category"] = float(v.length);
      // Store each exact ID separately: legacy display conversion must not
      // collapse a compound base switch into one guessed technique name.
      for (const auto &id : v.techniques)
        (*note.mutable_notation_techniques())[d.name + ":" + id] = id;
    }
    note.set_dynamics_mode(Note::CC); // r6 bases explicitly use CC1 dynamics.
    return true;
  }
  uint64_t invalidCommands = 0;
  uint64_t rejectedNotes = 0;
private:
  std::shared_ptr<const ExperimentalBridgeProfile> profile_;
  std::vector<int> values_;
};
} // namespace fiddle
