#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
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
  /// Fingerprints of every template installed before this one (recorded
  /// since 2026-11-20), so a project's template counts as its own copy only
  /// when Fiddle really installed it.
  std::vector<juce::String> history;
  static constexpr int kHistoryLimit = 500;

  static juce::String fingerprint(const Lines &lines) {
    juce::String text;
    for (const auto &[id, line] : lines) text << id << '\n' << line << '\n';
    return juce::String::toHexString(text.hashCode64());
  }

  /// Whether Fiddle installed exactly this template, now or before.
  bool wasInstalled(const Lines &lines) const {
    if (known && lines == chairs) return true;
    return std::find(history.begin(), history.end(), fingerprint(lines)) != history.end();
  }

  /// Record a new install, keeping the template it replaces in the history.
  void recordInstall(const Lines &lines) {
    if (known && chairs != lines) {
      const auto previous = fingerprint(chairs);
      history.erase(std::remove(history.begin(), history.end(), previous), history.end());
      history.push_back(previous);
      if (int(history.size()) > kHistoryLimit) history.erase(history.begin());
    }
    chairs = lines;
    known = true;
  }

  /// A chair as the template writes it.
  struct Chair {
    juce::String id, instrumentEntityId, name, family;
    bool solo = true;
    int ordinal = 1, displayOrder = 0, flatIndex = -1;
  };

  /// One line per chair: its identity, MIDI destination and the bridge map
  /// pinned to it in bridgeMapsJson (a project's bridge-map settings).
  /// Draft/unassigned revisions don't change the template; pinned
  /// endpoint/revision changes do.
  static Lines linesFor(const std::vector<Chair> &chairs, const juce::String &bridgeMapsJson) {
    std::map<std::pair<int, int>, juce::String> maps;
    const auto bridgeJson = juce::JSON::parse(bridgeMapsJson);
    if (const auto *routes = bridgeJson["assignments"].getArray())
      for (const auto &route : *routes)
        maps[{int(route["port"]), int(route["channel"])}] = route["map"].toString();
    Lines lines;
    for (const auto &chair : chairs) {
      const auto map = maps.find({chair.flatIndex / 16, chair.flatIndex % 16 + 1});
      lines[chair.id] = chair.instrumentEntityId + "|" + chair.name + "|" + chair.family + "|" +
                        (chair.solo ? "S" : "T") + "|" + juce::String(chair.ordinal) + "|" +
                        juce::String(chair.displayOrder) + "|" + juce::String(chair.flatIndex) + "|" +
                        (map != maps.end() ? map->second : juce::String());
    }
    return lines;
  }

  struct Status {
    bool known = false;
    bool dirty = true;
    std::vector<juce::String> added, changed;
    int removed = 0;
    /// The chairs differ from the installed template, but the open Dorico
    /// project was saved with exactly these chairs, so it has its own
    /// matching copy and needs no reinstall (dirty is false).
    bool projectCopy = false;

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
      o->setProperty("projectCopy", projectCopy);
      return juce::var(o);
    }
  };

  /// Compare the current chairs with the installed ones. Without a record
  /// nothing is known per chair, and the template counts as out of date.
  /// projectTemplate is the template of the version the open Dorico project
  /// was saved with, when Fiddle restored exactly that version. When the
  /// chairs match it and Fiddle really installed that template at some point,
  /// the project has its own copy, so a difference from the installed
  /// template isn't something to reinstall for. A version saved before its
  /// chairs were installed never counts.
  Status compare(const Lines &current, const Lines *projectTemplate = nullptr) const {
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
    if (status.dirty && projectTemplate && *projectTemplate == current && wasInstalled(current)) {
      status = Status{};
      status.known = true;
      status.dirty = false;
      status.projectCopy = true;
    }
    return status;
  }

  juce::var toJson() const {
    auto *lines = new juce::DynamicObject();
    for (const auto &[id, line] : chairs) lines->setProperty(id, line);
    auto *o = new juce::DynamicObject();
    o->setProperty("schema", 1);
    o->setProperty("chairs", juce::var(lines));
    juce::Array<juce::var> earlier;
    for (const auto &fingerprint : history) earlier.add(fingerprint);
    o->setProperty("history", earlier);
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
    if (const auto *earlier = json["history"].getArray())
      for (const auto &fingerprint : *earlier) record.history.push_back(fingerprint.toString());
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
