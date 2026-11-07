#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

namespace fiddle {

/// What Fiddle last installed as Dorico's playback template: one line per
/// chair (its identity, MIDI destination and pinned bridge map). Dorico's
/// template is global, so the record lives in the app data folder rather than
/// in a project; checking out another version compares against the same
/// record. Chair Manager uses the comparison to say which chairs need the
/// template reinstalled.
struct PlaybackTemplateRecord {
  using Lines = std::map<juce::String, juce::String>; // chair ID -> line

  bool known = false; // false until an install has been recorded
  Lines chairs;

  struct Status {
    bool known = false;
    bool dirty = true;
    std::vector<juce::String> added, changed;
    int removed = 0;

    juce::var toJson() const {
      auto *o = new juce::DynamicObject();
      o->setProperty("known", known);
      o->setProperty("dirty", dirty);
      juce::Array<juce::var> addedIds, changedIds;
      for (const auto &id : added) addedIds.add(id);
      for (const auto &id : changed) changedIds.add(id);
      o->setProperty("newChairIds", addedIds);
      o->setProperty("changedChairIds", changedIds);
      o->setProperty("removedCount", removed);
      return juce::var(o);
    }
  };

  /// Compare the current chairs with the installed ones. Without a record
  /// nothing is known per chair, and the template counts as out of date.
  Status compare(const Lines &current) const {
    Status status;
    status.known = known;
    if (!known) return status;
    for (const auto &[id, line] : current) {
      const auto installed = chairs.find(id);
      if (installed == chairs.end()) status.added.push_back(id);
      else if (installed->second != line) status.changed.push_back(id);
    }
    for (const auto &[id, line] : chairs)
      if (current.find(id) == current.end()) ++status.removed;
    status.dirty = !status.added.empty() || !status.changed.empty() || status.removed > 0;
    return status;
  }

  juce::var toJson() const {
    auto *lines = new juce::DynamicObject();
    for (const auto &[id, line] : chairs) lines->setProperty(id, line);
    auto *o = new juce::DynamicObject();
    o->setProperty("schema", 1);
    o->setProperty("chairs", juce::var(lines));
    return juce::var(o);
  }

  static PlaybackTemplateRecord fromJson(const juce::var &json) {
    PlaybackTemplateRecord record;
    if (!json.isObject() || int(json["schema"]) != 1) return record;
    if (auto *lines = json["chairs"].getDynamicObject()) {
      for (const auto &property : lines->getProperties())
        record.chairs[property.name.toString()] = property.value.toString();
      record.known = true;
    }
    return record;
  }

  static PlaybackTemplateRecord load(const juce::File &file) {
    return file.existsAsFile() ? fromJson(juce::JSON::parse(file)) : PlaybackTemplateRecord{};
  }

  bool save(const juce::File &file) const {
    return file.replaceWithText(juce::JSON::toString(toJson()));
  }
};

} // namespace fiddle
