#include "TechniqueCatalog.h"
#include <algorithm>

namespace fiddle {
namespace {
using Strings = std::set<juce::String>;
juce::String child(const juce::XmlElement &e, const char *name) {
  if (auto *c = e.getChildByName(name)) return c->getAllSubText().trim();
  return {};
}
Strings ids(const juce::String &s) {
  Strings result;
  for (auto value : juce::StringArray::fromTokens(s, ",+", "")) {
    value = value.trim();
    if (value.isNotEmpty()) result.insert(value);
  }
  return result;
}
juce::var strings(const Strings &s) {
  juce::Array<juce::var> result;
  for (const auto &v : s) result.add(v);
  return result;
}
juce::var object(std::initializer_list<std::pair<const char *, juce::var>> fields) {
  auto *o = new juce::DynamicObject();
  for (const auto &[name, value] : fields) o->setProperty(name, value);
  return juce::var(o);
}
struct Technique {
  Strings names;
  juce::Array<juce::var> definitions, appearances, maps;
  std::map<juce::String, int> definitionIndex, appearanceIndex;
  bool declared = false;
};
void appendRecord(juce::Array<juce::var> &records, std::map<juce::String, int> &index, juce::var value) {
  const auto source = value["source"];
  value.getDynamicObject()->removeProperty("source");
  const auto key = juce::JSON::toString(value, true);
  const auto found = index.find(key);
  if (found != index.end()) {
    auto *sources = records[found->second]["sources"].getArray();
    if (!sources->contains(source)) sources->add(source);
  } else {
    index[key] = records.size();
    value.getDynamicObject()->setProperty("sources", juce::Array<juce::var>{source});
    records.add(value);
  }
}
class Builder {
public:
  const TechniqueCatalog::Request &request;
  TechniqueCatalog::Cancel cancelled;
  TechniqueCatalog::Progress progress;
  std::map<juce::String, Technique> techniques;
  juce::Array<juce::var> sources, maps, groups, issues, roots;
  std::map<juce::String, int> groupIndex;
  std::set<juce::String> visited;
  int files = 0;
  bool stopped() const { return cancelled && cancelled(); }

  void issue(const juce::String &path, const juce::String &message) {
    issues.add(object({{"path", path}, {"message", message}}));
  }
  void visit(juce::XmlElement &xml, int source, const juce::String &kind) {
    const auto tag = xml.getTagName();
    const auto id = child(xml, "entityID");
    if (tag == "PlayingTechniqueDefinition" && id.isNotEmpty()) {
      auto &t = techniques[id];
      const auto name = child(xml, "name");
      if (name.isNotEmpty()) t.names.insert(name);
      auto definition = object({{"source", source}, {"name", name},
          {"articulationType", child(xml, "articulationType")},
          {"groupType", child(xml, "groupType")},
          {"parent", child(xml, "parentEntityID")},
          {"inheritanceMask", child(xml, "inheritanceMask")},
          {"alias", child(xml, "aliasForPlayingTechniqueID")},
          {"fallback", child(xml, "fallbackPlayingTechniqueID")},
          {"groups", strings(ids(child(xml, "mutualExclusionGroups")))}});
      appendRecord(t.definitions, t.definitionIndex, definition);
      for (const auto *key : {"alias", "fallback", "parent"}) {
        const auto target = definition[key].toString();
        if (target.isNotEmpty()) techniques[target];
      }
      return;
    }
    if (tag == "PlayingTechniqueAppearanceDefinition" && id.isNotEmpty()) {
      const auto target = child(xml, "playingTechniqueEntityID");
      if (target.isEmpty()) return;
      auto &t = techniques[target];
      appendRecord(t.appearances, t.appearanceIndex, object({{"source", source}, {"id", id},
          {"name", child(xml, "name")}, {"text", child(xml, "text")},
          {"type", child(xml, "playingTechniqueAppearanceType")},
          {"alwaysHasDuration", child(xml, "alwaysHasDuration")},
          {"continuation", child(xml, "continuationText")},
          {"durationLine", xml.getChildByName("durationLineData") ? child(*xml.getChildByName("durationLineData"), "lineStyleEntityID") : juce::String{}},
          {"transitionLine", xml.getChildByName("transitionLineData") ? child(*xml.getChildByName("transitionLineData"), "lineStyleEntityID") : juce::String{}}}));
      return;
    }
    if (tag == "ExpressionMapDefinition" && id.isNotEmpty()) {
      Strings references, declared;
      // Count switch techniques, not mere membership in an exclusion group.
      for (const auto *container : {"playingTechniqueCombinations", "techniqueAddOns"})
        if (auto *switches = xml.getChildByName(container))
          for (auto *sw : switches->getChildIterator()) {
            const auto values = ids(child(*sw, "techniqueIDs"));
            references.insert(values.begin(), values.end());
            if (auto *actions = sw->getChildByName("switchOnActions"))
              for (auto *action : actions->getChildIterator()) {
                const auto type = child(*action, "type");
                const auto cc = child(*action, "param1").getIntValue();
                if ((type == "kControlChange" || type == "kCC") && cc >= 102 && cc <= 119)
                  declared.insert(values.begin(), values.end());
              }
          }
      juce::Array<juce::var> mapGroups, uses;
      if (auto *megs = xml.getChildByName("mutualExclusionGroups"))
        for (auto *meg : megs->getChildIterator()) {
          const auto group = object({{"id", child(*meg, "groupID")}, {"name", child(*meg, "name")},
              {"techniques", strings(ids(child(*meg, "techniqueIDs")))}});
          const auto key = juce::JSON::toString(group, true);
          auto [found, inserted] = groupIndex.emplace(key, groups.size());
          if (inserted) groups.add(group);
          mapGroups.add(found->second);
        }
      const auto path = sources[source]["path"].toString();
      for (const auto &use : request.usage)
        if (use.mapId == id && (use.sourcePath.isEmpty() || use.sourcePath == path))
          uses.add(object({{"scope", use.scope}, {"label", use.label}, {"exactSource", use.sourcePath.isNotEmpty()}}));
      const int index = maps.size();
      maps.add(object({{"id", id}, {"name", child(xml, "name")}, {"creator", child(xml, "creator")},
          {"version", child(xml, "version")}, {"source", source}, {"techniqueCount", int(references.size())},
          {"groups", mapGroups}, {"usage", uses}, {"autoMutualExclusion", child(xml, "autoMutualExclusion")}}));
      for (const auto &ref : references) techniques[ref].maps.add(index);
      if (kind == "bridge" && child(xml, "name") == "Fiddle")
        for (const auto &ref : declared) techniques[ref].declared = true;
      return;
    }
    for (auto *c : xml.getChildIterator()) {
      if (stopped()) return;
      visit(*c, source, kind);
    }
  }
  void add(const TechniqueCatalog::Source &source) {
    if (stopped()) return;
    const auto path = source.path.getFullPathName();
    // The same file can be requested by both discovery and a Fiddle assignment.
    // Preserve bridge separately: it is the map loaded by this app, not proof
    // that the same version is installed in a particular Dorico project.
    const auto key = path + (source.kind == "bridge" ? "#bridge" : "") + (source.xml.isNotEmpty() ? "#snapshot" : "");
    if (!visited.insert(key).second) return;
    if (source.xml.isEmpty() && source.path.getSize() > 64 * 1024 * 1024) {
      issue(path, "Skipped file larger than 64 MiB"); return;
    }
    juce::XmlDocument doc(source.xml.isNotEmpty() ? source.xml : source.path.loadFileAsString());
    auto xml = doc.getDocumentElement();
    ++files;
    if (progress) progress(files);
    if (!xml) { issue(path, "Could not parse library XML: " + doc.getLastParseError()); return; }
    if (!xml->hasTagName("kScoreLibrary") && !xml->hasTagName("kScoreLibraryAndOptions") && !xml->hasTagName("ExpressionMapDefinition")) {
      issue(path, "Not a Dorico score library or expression map"); return;
    }
    const int index = sources.size();
    sources.add(object({{"path", path}, {"kind", source.kind}, {"snapshot", source.xml.isNotEmpty()}}));
    visit(*xml, index, source.kind);
  }
  juce::var finish() {
    juce::Array<juce::var> rows, unresolvedUsage;
    int declaredCount = 0, missingCount = 0, variantCount = 0;
    for (auto &[id, t] : techniques) {
      // Keep all definitions and source provenance. Differences are candidates
      // for review, not an attempt to emulate Dorico's override precedence.
      Strings signatures, localSignatures;
      bool localDefinition = false;
      for (const auto &def : t.definitions) {
        auto value = def.clone();
        value.getDynamicObject()->removeProperty("sources");
        const auto signature = juce::JSON::toString(value, true);
        signatures.insert(signature);
        for (const auto &source : *def["sources"].getArray()) {
          const auto kind = sources[int(source)]["kind"].toString();
          if (kind != "reference" && kind != "additional") { localDefinition = true; localSignatures.insert(signature); }
        }
      }
      const bool variants = signatures.size() > 1;
      const auto definitionState = t.definitions.isEmpty() ? "missing" : localDefinition ? "local" : "reference";
      if (t.declared) ++declaredCount;
      if (t.definitions.isEmpty()) ++missingCount;
      if (variants) ++variantCount;
      rows.add(object({{"id", id}, {"names", strings(t.names)}, {"definitions", t.definitions},
          {"appearances", t.appearances}, {"maps", t.maps}, {"declared", t.declared},
          {"definitionState", definitionState}, {"variants", variants}, {"localVariants", localSignatures.size() > 1}}));
    }
    for (const auto &use : request.usage) {
      const bool found = std::any_of(maps.begin(), maps.end(), [&](const juce::var &map) {
        return map["id"].toString() == use.mapId && (use.sourcePath.isEmpty() || sources[int(map["source"])]["path"].toString() == use.sourcePath);
      });
      if (!found) unresolvedUsage.add(object({{"mapId", use.mapId}, {"label", use.label}, {"scope", use.scope}}));
    }
    const auto reachability = BridgeReachability::audit(request.decoder, request.assignments, cancelled);
    return object({{"techniques", rows}, {"maps", maps}, {"sources", sources}, {"groups", groups}, {"roots", roots},
        {"reachability", reachability},
        {"issues", issues}, {"unresolvedUsage", unresolvedUsage}, {"files", files},
        {"declaredCount", declaredCount}, {"missingCount", missingCount}, {"variantCount", variantCount},
        {"includeReference", request.includeReference}, {"scannedAt", juce::Time::getCurrentTime().toISO8601(true)},
        {"cancelled", stopped()}});
  }
};
} // namespace

juce::var TechniqueCatalog::scan(const Request &request, Cancel cancelled, Progress progress) {
  Builder b{request, std::move(cancelled), std::move(progress)};
  for (const auto &source : request.sources) {
    if (b.stopped()) break;
    const bool exists = source.xml.isNotEmpty() || source.path.exists();
    b.roots.add(object({{"path", source.path.getFullPathName()}, {"kind", source.kind}, {"exists", exists}}));
    if (!exists) { b.issue(source.path.getFullPathName(), "Source not found"); continue; }
    if (source.xml.isEmpty() && source.path.isDirectory()) {
      // Do not follow symlinks out of the chosen tree, or scan archives/presets.
      for (const auto &entry : juce::RangedDirectoryIterator(source.path, true, "*.doricolib", juce::File::findFiles,
               juce::File::FollowSymlinks::no)) {
        if (b.stopped()) break;
        b.add({entry.getFile(), source.kind, {}});
      }
    } else b.add(source);
  }
  auto report = b.finish();
  if (!b.stopped()) report.getDynamicObject()->setProperty("repairPlan", BridgeRepairPlan::build(
      request.decoder, request.reservedControllers, request.assignments, report, {}, b.cancelled));
  report.getDynamicObject()->setProperty("cancelled", b.stopped());
  return report;
}

bool TechniqueCatalogScan::start(TechniqueCatalog::Request request) {
  if (isThreadRunning()) return false;
  request_ = std::move(request);
  previewOnly_ = false;
  previewBase_ = juce::var{};
  files_.store(0);
  { const juce::ScopedLock lock(lock_); result_ = juce::var{}; }
  if (startThread(juce::Thread::Priority::low)) return true;
  const juce::ScopedLock lock(lock_);
  result_ = object({{"error", "Could not start catalog scan"}});
  return false;
}
bool TechniqueCatalogScan::previewRepairs(BridgeRepairPlan::Options options) {
  if (isThreadRunning()) return false;
  {
    const juce::ScopedLock lock(lock_);
    if (!result_["techniques"].isArray() || bool(result_["cancelled"])) return false;
    previewBase_ = result_;
  }
  repairOptions_ = std::move(options);
  previewOnly_ = true;
  if (startThread(juce::Thread::Priority::low)) return true;
  previewOnly_ = false;
  return false;
}
void TechniqueCatalogScan::run() {
  const auto cancelled = [this] { return threadShouldExit(); };
  juce::var result;
  if (previewOnly_) {
    // Copy just the root; large catalog arrays stay immutable and shared.
    auto *root = new juce::DynamicObject();
    const auto &properties = previewBase_.getDynamicObject()->getProperties();
    for (int i = 0; i < properties.size(); ++i) root->setProperty(properties.getName(i), properties.getValueAt(i));
    result = juce::var(root);
    auto plan = BridgeRepairPlan::build(request_.decoder, request_.reservedControllers,
        request_.assignments, previewBase_, repairOptions_, cancelled);
    if (cancelled()) root->setProperty("repairError", "Preview cancelled; showing the previous plan.");
    else { root->setProperty("repairPlan", plan); root->removeProperty("repairError"); }
  } else result = TechniqueCatalog::scan(request_, cancelled, [this](int files) { files_.store(files); });
  const juce::ScopedLock lock(lock_);
  result_ = std::move(result);
}
juce::var TechniqueCatalogScan::state() const {
  const juce::ScopedLock lock(lock_);
  return object({{"running", isThreadRunning()}, {"previewing", previewOnly_ && isThreadRunning()},
      {"files", files_.load()}, {"report", result_}});
}
} // namespace fiddle
