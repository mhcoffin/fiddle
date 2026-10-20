#pragma once
#include "BridgeMapDocument.h"
#include "NoteStreamTracker.h"
#include "ProjectSettingsAction.h"

namespace fiddle {
// Message-thread authoring/session verification; actual decoder state is owned
// by NoteStreamTracker under its MIDI mutex. No file or compiler work in audio.
class BridgeMapCommands {
public:
  BridgeMapCommands(MixerModel &mixer, NoteStreamTracker &tracker, UndoManager &undo,
                    std::function<void()> changed, std::function<bool()> busy)
      : mixer_(mixer), tracker_(tracker), undo_(undo), changed_(std::move(changed)), busy_(std::move(busy)) {
    mixer_.onBridgeMapsChanged = [this](const std::string &json) { restore(json); };
    restore(mixer_.projectSettings().bridgeMapsJson);
  }
  ~BridgeMapCommands() { mixer_.onBridgeMapsChanged = {}; }

  void restore(const std::string &json) {
    BridgeMapProject parsed;
    juce::String error;
    if (!BridgeMapProject::parse(juce::String(json), parsed, error)) {
      error_ = "Bridge restore rejected: " + error;
      mixer_.allNotesOff(); tracker_.rejectBridgeMaps(); return;
    }
    project_ = std::move(parsed); verified_.clear(); error_.clear();
    mixer_.allNotesOff(); tracker_.setBridgeMaps(project_);
  }
  void invalidateVerification() { restore(mixer_.projectSettings().bridgeMapsJson); }
  bool publish(const juce::String &source, const juce::String &parent = {}, int low = 21, int high = 114) {
    if (!canChange()) return false;
    auto xml = source;
    if (parent.isNotEmpty()) {
      const auto found = project_.documents.find(parent);
      if (found == project_.documents.end()) return fail("Select the revision to edit first");
      auto root = juce::parseXML(xml);
      auto *defs = root ? root->getChildByName("expressionMapDefinitions") : nullptr;
      auto *entities = defs ? defs->getChildByName("entities") : nullptr;
      auto *map = entities ? entities->getFirstChildElement() : nullptr;
      if (!map || !map->getChildByName("entityID") || !map->getChildByName("version")) return fail("Invalid expression-map XML");
      auto *identity = map->getChildByName("entityID"), *version = map->getChildByName("version");
      identity->deleteAllChildElements(); identity->addTextElement("xmap.user.Fiddle.Bridge." + juce::Uuid().toString().removeCharacters("-"));
      version->deleteAllChildElements(); version->addTextElement(juce::String(found->second->revision + 1));
      // Give the revision its own Dorico mutex identities as well, so importing
      // edited grouping cannot reuse an older revision's group identity.
      if (auto *groups = map->getChildByName("mutualExclusionGroups"))
        for (auto *group : groups->getChildIterator())
          if (auto *id = group->getChildByName("groupID")) {
            id->deleteAllChildElements();
            id->addTextElement("ptmg.user.Fiddle.Bridge." + juce::Uuid().toString().removeCharacters("-"));
          }
      BridgeMapDocument::stampBeacon(*map);
      xml = root->toString();
    } else {
      // An edited schema-2 map may carry a stale beacon. Restamp only then, so
      // an already-consistent file is published byte for byte.
      juce::String probe;
      if (!BridgeMapDocument::compile(xml, probe, low, high) && probe.contains("Revision beacon does not match"))
        if (auto root = juce::parseXML(xml)) {
          auto *defs = root->getChildByName("expressionMapDefinitions");
          auto *entities = defs ? defs->getChildByName("entities") : nullptr;
          if (auto *map = entities ? entities->getFirstChildElement() : nullptr; map && BridgeMapDocument::stampBeacon(*map))
            xml = root->toString();
        }
    }
    juce::String error;
    const auto doc = BridgeMapDocument::compile(xml, error, low, high);
    if (!doc) return fail(error);
    for (const auto &[hash, existing] : project_.documents)
      if (existing->entityId == doc->entityId && hash != doc->contentHash)
        return fail("That Dorico map ID already has different content. Publish as a new revision from the selected map.");
    auto after = project_; after.documents[doc->contentHash] = doc;
    return commit(after, "Publish bridge map revision");
  }
  bool importBundle(const juce::String &json) {
    if (!canChange()) return false;
    if (json.trim().isEmpty()) return fail("The bridge bundle is empty");
    BridgeMapProject imported; juce::String error;
    if (!BridgeMapProject::parse(json, imported, error)) return fail(error);
    // Replace only by explicit project-bundle import, recorded in Undo/history.
    return commit(imported, "Import bridge project bundle");
  }
  bool assign(int port, int channel, const juce::String &hash) {
    if (!canChange()) return false;
    if (port < 0 || port >= 16 || channel < 1 || channel > 16 || (port == 0 && channel == 1))
      return fail("Choose port 1–16 and channel 1–16; port 1/channel 1 is reserved for the metronome");
    auto after = project_;
    if (hash.isEmpty()) after.assignments.erase({port, channel});
    else {
      if (!after.documents.count(hash)) return fail("Unknown bridge revision");
      after.assignments[{port, channel}] = hash;
    }
    return commit(after, "Assign bridge revision");
  }
  bool verify(int port, int channel, const juce::String &exportedXml) {
    if (!canChange()) return false;
    // A failed comparison immediately revokes any prior attestation.
    const auto it = project_.assignments.find({port, channel});
    if (it == project_.assignments.end()) return fail("Assign a bridge revision first");
    verified_.erase({port, channel}); tracker_.unverifyBridge(port, channel);
    const auto expected = project_.documents.at(it->second);
    juce::String error;
    const auto actual = BridgeMapDocument::compile(exportedXml, error, expected->musicalLow, expected->musicalHigh);
    // Compare meaning, not formatting: Dorico may reorder rows or rewrite text.
    if (!actual || actual->semanticHash != expected->semanticHash)
      return fail("Dorico export does not match the assigned bridge revision. " + error);
    verified_.insert({port, channel}); tracker_.verifyBridge(port, channel);
    error_.clear(); return true;
  }
  juce::String exportXml(const juce::String &hash) const {
    const auto it = project_.documents.find(hash);
    return it == project_.documents.end() ? juce::String{} : it->second->sourceXml;
  }
  juce::String exportBundle() const { return project_.serialize(); }
  juce::var status() const {
    auto *o = new juce::DynamicObject(); juce::Array<juce::var> docs, assignments;
    for (const auto &[hash, doc] : project_.documents) {
      auto *d = new juce::DynamicObject(); d->setProperty("hash", hash); d->setProperty("name", doc->name);
      d->setProperty("entityId", doc->entityId); d->setProperty("revision", doc->revision);
      d->setProperty("dimensions", int(doc->compiled->dimensions.size())); d->setProperty("sourceXml", doc->sourceXml);
      d->setProperty("low", doc->musicalLow); d->setProperty("high", doc->musicalHigh);
      d->setProperty("schema", doc->schema); docs.add(juce::var(d));
    }
    for (const auto &[ep, hash] : project_.assignments) {
      auto *a = new juce::DynamicObject(); a->setProperty("port", ep.first); a->setProperty("channel", ep.second);
      a->setProperty("hash", hash); a->setProperty("verified", verified_.count(ep) > 0);
      if (const auto live = tracker_.bridgeDiagnostics(ep.first, ep.second)) {
        static const char *beacons[] = {"unsupported", "absent", "matched", "mismatched"};
        a->setProperty("beacon", beacons[int(live->beacon)]);
        a->setProperty("receivedBeacon", juce::Array<juce::var>{live->receivedBeacon[0], live->receivedBeacon[1]});
        a->setProperty("expectedBeacon", juce::Array<juce::var>{live->expectedBeacon[0], live->expectedBeacon[1]});
        a->setProperty("lastWithheld", juce::String(BridgeDecoderState::describe(live->lastWithheld)));
        juce::int64 withheld = 0;
        for (const auto count : live->withheld) withheld += juce::int64(count);
        a->setProperty("withheldNotes", withheld);
        a->setProperty("invalidCommands", juce::int64(live->invalidCommands));
        a->setProperty("ignoredOffs", juce::int64(live->ignoredOffs));
        a->setProperty("debouncedOffs", juce::int64(live->debouncedOffs));
      }
      assignments.add(juce::var(a));
    }
    o->setProperty("documents", docs); o->setProperty("assignments", assignments);
    o->setProperty("error", error_); o->setProperty("busy", busy_() || tracker_.hasActiveNotes());
    return juce::var(o);
  }
  void reportError(const juce::String &error) { error_ = error; }
private:
  bool canChange() {
    if (busy_() || tracker_.hasActiveNotes()) return fail("Stop playback and release auditioned notes before changing or verifying bridge maps");
    return true;
  }
  bool fail(const juce::String &error) { error_ = error; return false; }
  bool commit(const BridgeMapProject &project, const juce::String &label) {
    const auto json = project.serialize();
    if (project.documents.size() > 128 || json.getNumBytesAsUTF8() > 16 * 1024 * 1024)
      return fail("Bridge project exceeds the supported size (128 revisions / 16 MiB)");
    auto settings = mixer_.projectSettings(); settings.bridgeMapsJson = json.toStdString();
    undo_.perform(std::make_unique<SetProjectSettingsAction>(mixer_, std::move(settings), label));
    error_.clear(); if (changed_) changed_(); return true;
  }
  MixerModel &mixer_; NoteStreamTracker &tracker_; UndoManager &undo_;
  std::function<void()> changed_; std::function<bool()> busy_;
  BridgeMapProject project_;
  std::set<BridgeMapProject::Endpoint> verified_;
  juce::String error_;
};
} // namespace fiddle
