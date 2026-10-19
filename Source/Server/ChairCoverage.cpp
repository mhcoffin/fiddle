#include "ChairCoverage.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace fiddle {
namespace {
using IDs = std::set<std::string>;
using Assignment = BridgeReachability::Assignment;
juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
  auto* result = new juce::DynamicObject();
  for (const auto& [key, value] : fields) result->setProperty(key, value);
  return juce::var(result);
}
juce::var strings(const IDs& values) {
  juce::Array<juce::var> out;
  for (const auto& value : values) out.add(juce::String(value));
  return out;
}
struct Rule { const char* name; IDs members; };
// Only the 20 firm enums from docs/dorico-built-in-technique-groups.md.
// Other families remain independent flags (an upper bound), never inferred
// from vendor MEGs. This revision is a sizing model, not a host contract.
const std::vector<Rule> rules{
  {"Connection", {"pt.legato", "pt.nonLegato"}},
  {"Shortening", {"pt.staccato", "pt.staccatissimo"}},
  {"Accent class", {"pt.accent", "pt.marcato"}},
  {"Vibrato", {"pt.nonVibrato", "pt.conVibrato", "pt.vibrato", "pt.wideVibrato"}},
  {"Trill interval", {"pt.trill", "pt.trill.half", "pt.trill.whole"}},
  {"Arpeggio direction", {"pt.arpeggioUp", "pt.arpeggioDown"}},
  {"Bow direction", {"pt.upbow", "pt.downbow"}},
  {"Steady dynamic designation", {"pt.ppp", "pt.pp", "pt.p", "pt.mp", "pt.mf", "pt.f", "pt.ff", "pt.fff"}},
  {"Mouth opening", {"pt.mouthClosed", "pt.mouthSlightlyOpen", "pt.mouthOpen", "pt.mouthWideOpen"}},
  {"Strum direction", {"pt.strumDown", "pt.strumUp"}},
  {"Openness", {"pt.closed", "pt.halfOpen", "pt.open2"}},
  {"End opening", {"pt.closedEnd", "pt.openEnd"}},
  {"Hole opening", {"pt.closedHole", "pt.openHole"}},
  {"Hand", {"pt.left", "pt.right"}},
  {"Motion direction", {"pt.down", "pt.up"}},
  {"Push / pull", {"pt.pull", "pt.push"}},
  {"Scratch direction", {"pt.scratchIn", "pt.scratchOut"}},
  {"Snare mechanism", {"pt.snareOff", "pt.snareOn"}},
  {"Throwoff mechanism", {"pt.throwoffOff", "pt.throwoffOn"}},
  {"Motor", {"pt.motorOff", "pt.motorOn"}},
};
const Rule* ruleFor(const std::string& id) {
  for (const auto& rule : rules) if (rule.members.count(id)) return &rule;
  return nullptr;
}
const IDs timingIDs{"pt.staccatissimo", "pt.staccato", "pt.legato", "pt.tenuto", "pt.marcato"};
const IDs independentIDs{
  "pt.alternativeFingering", "pt.chorus", "pt.cuivre", "pt.delay", "pt.distortion", "pt.flange",
  "pt.flautando", "pt.fouette", "pt.ghostNote", "pt.growl", "pt.lhPizzicato", "pt.mouthPursed",
  "pt.multiphonic", "pt.nasalVoice", "pt.overdrive", "pt.overpressure", "pt.palmMuting", "pt.pedal",
  "pt.phaser", "pt.soft", "pt.sostenutoPedal", "pt.stopped", "pt.sustainPedal", "pt.tenuto", "pt.unaCorda"
};
struct Definition { bool builtin = false; bool variants = false; bool alias = false; bool fallback = false; };
std::map<std::string, Definition> definitions(const juce::var& catalog) {
  std::map<std::string, Definition> result;
  if (auto* rows = catalog["techniques"].getArray()) for (const auto& row : *rows) {
    auto& d = result[row["id"].toString().toStdString()];
    d.variants = bool(row["localVariants"]);
    if (auto* defs = row["definitions"].getArray()) for (const auto& def : *defs) {
      bool local = false;
      if (auto* sources = def["sources"].getArray()) for (const auto& index : *sources) {
        const auto kind = catalog["sources"][int(index)]["kind"].toString();
        d.builtin |= kind == "builtin";
        local |= kind == "builtin" || kind == "installed" || kind == "assigned";
      }
      if (local) {
        d.alias |= def["alias"].toString().isNotEmpty();
        d.fallback |= def["fallback"].toString().isNotEmpty();
      }
    }
  }
  return result;
}
juce::var interpretation(const Assignment& layer, const IDs& request, int length) {
  const auto match = layer.map.resolveMatch(request, static_cast<LengthCategory>(length));
  IDs covered, unmatched, introduced;
  juce::Array<juce::var> addons;
  auto selected = [&](const TechniqueCombination* row) {
    covered.insert(row->techniqueIDs.begin(), row->techniqueIDs.end());
    return object({{"index", int(row - layer.map.combinations.data())}, {"name", juce::String(row->name)}});
  };
  juce::var base;
  if (match.base) base = selected(match.base);
  for (auto* addon : match.addOns) addons.add(selected(addon));
  std::set_difference(request.begin(), request.end(), covered.begin(), covered.end(), std::inserter(unmatched, unmatched.end()));
  std::set_difference(covered.begin(), covered.end(), request.begin(), request.end(), std::inserter(introduced, introduced.end()));
  const auto outcome = !match.base ? "no-match" : !introduced.empty() ? "fallback" : !unmatched.empty() ? "partial" : "covered";
  return object({{"layerId", layer.layerId}, {"layerName", layer.layerName}, {"length", length},
      {"outcome", outcome}, {"base", base}, {"addons", addons},
      {"unmatched", strings(unmatched)}, {"introduced", strings(introduced)}});
}
} // namespace

juce::var ChairCoverage::analyze(const std::vector<Assignment>& assignments,
    const juce::var& catalog, const Limits& limits, std::function<bool()> cancelled) {
  const auto start = juce::Time::getMillisecondCounterHiRes();
  const auto defs = definitions(catalog);
  std::map<juce::String, std::vector<const Assignment*>> grouped;
  for (const auto& layer : assignments) grouped[layer.chairId].push_back(&layer);
  juce::Array<juce::var> chairs;
  int calls = 0, storedBytes = 0;
  const auto stopReason = [&]() -> juce::String {
    if (cancelled && cancelled()) return "cancelled";
    if (juce::Time::getMillisecondCounterHiRes() - start >= limits.milliseconds) return "time-limit";
    if (calls >= std::max(0, limits.resolverCalls)) return "resolver-limit";
    return {};
  };
  for (auto& [chairId, layers] : grouped) {
    std::sort(layers.begin(), layers.end(), [](auto* a, auto* b) { return a->layerId < b->layerId; });
    std::map<std::string, juce::Array<juce::var>> vocabulary;
    juce::Array<juce::var> issues, layerRows, dimensionRows, witnesses;
    int issueCount = 0;
    auto issue = [&](const char* code, const juce::String& message, const Assignment* layer = nullptr,
                     int entry = -1, const IDs& ids = {}) {
      ++issueCount;
      if (issues.size() < 200) issues.add(object({{"code", code}, {"message", message},
          {"layerId", layer ? layer->layerId : juce::String{}}, {"entry", entry}, {"techniques", strings(ids)}}));
    };
    auto require = [&](const std::string& id, const Assignment& layer, int index, const char* reason) {
      vocabulary[id].add(object({{"layerId", layer.layerId}, {"layerName", layer.layerName},
          {"mapName", juce::String(layer.map.name)}, {"entry", index},
          {"entryName", index >= 0 ? juce::String(layer.map.combinations[size_t(index)].name) : juce::String{}}, {"reason", reason}}));
    };
    for (auto* layer : layers) {
      layerRows.add(object({{"id", layer->layerId}, {"name", layer->layerName}, {"assigned", layer->assigned},
          {"mapId", juce::String(layer->map.entityID)}, {"mapName", juce::String(layer->map.name)},
          {"sourcePath", layer->sourcePath}, {"hasSavedXml", layer->sourceXml.isNotEmpty()},
          // A diagnostic fingerprint, not a future package identity/security hash.
          {"snapshotFingerprint", layer->sourceXml.isNotEmpty() ? juce::String::toHexString(layer->sourceXml.hashCode64()) : juce::String{}}}));
      if (layer->hasProcessors) issue("processors", "Lua processor dependencies are undeclared; coverage is unknown.", layer);
      if (!layer->assigned) { issue("missing-map", "No expression map snapshot; this layer is not an empty requirement.", layer); continue; }
      if (layer->sourceXml.isEmpty()) issue("missing-xml", "Parsed map available, but no saved XML provenance/capability audit.", layer);
      if (layer->map.combinations.empty()) issue("empty-map", "Assigned map has no parsed switches.", layer);
      for (const auto& id : timingIDs) require(id, *layer, -1, "Fiddle duration processing");
      for (size_t i = 0; i < layer->map.combinations.size(); ++i) {
        const auto& row = layer->map.combinations[i];
        for (const auto& id : row.techniqueIDs) require(id, *layer, int(i), row.isAddOn ? "Library add-on" : "Library base switch");
        for (const auto& rule : rules) {
          IDs overlap;
          std::set_intersection(row.techniqueIDs.begin(), row.techniqueIDs.end(), rule.members.begin(), rule.members.end(), std::inserter(overlap, overlap.end()));
          if (overlap.size() > 1) issue("mutex-conflict", "Entry requires multiple values of " + juce::String(rule.name) + ": " + juce::String(row.name), layer, int(i), overlap);
        }
        const auto parsed = parseConditionString(row.conditionString);
        if (!parsed.valid || (!row.conditionString.empty() && parsed.empty()))
          issue("condition", "Unsupported condition: " + juce::String(row.conditionString), layer, int(i));
        if (row.isAddOn && !row.conditionString.empty())
          issue("addon-condition", "Production matching does not evaluate add-on conditions.", layer, int(i));
        for (const auto* actions : {&row.switchOnActions, &row.switchOffActions})
          if (std::any_of(actions->begin(), actions->end(), [](const auto& action) { return action.type == SwitchActionType::Unknown; }))
            issue("unknown-action", "An unknown MIDI action is ignored by playback.", layer, int(i));
      }
    }
    // No implicit aliases or counterpart/reset expansion until these rules have
    // been reviewed. The report explicitly refuses installation-readiness.
    issue("contract-review", "Sizing domain only: cancellation/default closure, raw XML capabilities, host mutex/alias behavior and wire encoding still need review.");
    std::map<std::string, IDs> dimensions;
    juce::Array<juce::var> vocabularyRows;
    for (const auto& [id, reasons] : vocabulary) {
      const auto found = defs.find(id);
      const auto def = found == defs.end() ? Definition{} : found->second;
      const auto* rule = ruleFor(id);
      const auto key = rule ? std::string(rule->name) : id;
      dimensions[key].insert(id);
      vocabularyRows.add(object({{"id", juce::String(id)}, {"builtin", def.builtin}, {"dimension", juce::String(key)}, {"reasons", reasons}}));
      if (!def.builtin) issue("unknown-identity", "Not identified in the scanned built-in catalog; custom or missing definition.", nullptr, -1, {id});
      if (def.variants || def.alias || def.fallback) issue("definition-review", "Definition variants, aliases or fallbacks need review; exact identity retained.", nullptr, -1, {id});
      if (!rule && !independentIDs.count(id)) issue("unreviewed-dimension", "Kept as an independent flag for conservative sizing, not an approved host dimension.", nullptr, -1, {id});
    }
    std::vector<IDs> axes;
    double estimate = 1;
    for (const auto& [name, values] : dimensions) {
      axes.push_back(values);
      estimate *= double(values.size() + 1);
      dimensionRows.add(object({{"name", juce::String(name)}, {"values", strings(values)}, {"stateCount", int(values.size() + 1)},
          {"kind", ruleFor(*values.begin()) ? "enum" : "flag"}}));
    }
    // Mixed-radix iteration: unspecified first, then exact values. Never
    // materialize the product or project requests down to a layer vocabulary.
    std::vector<size_t> positions(axes.size(), 0);
    int visited = 0, completed = 0, chairCalls = 0;
    std::map<juce::String, std::map<juce::String, int>> outcomes;
    juce::String stopped;
    bool exhausted = false;
    while (!exhausted) {
      stopped = stopReason();
      if (stopped.isNotEmpty()) break;
      if (visited >= std::max(0, limits.statesPerChair)) { stopped = "state-limit"; break; }
      IDs request;
      for (size_t d = 0; d < axes.size(); ++d)
        if (positions[d]) request.insert(*std::next(axes[d].begin(), positions[d] - 1));
      ++visited;
      juce::Array<juce::var> interpretations;
      // Preserve the initial requests and also retain the richest joint
      // requests encountered, rather than showing only a lexicographic prefix.
      int replacement = -1;
      if (limits.witnessesPerChair > 1 && witnesses.size() >= limits.witnessesPerChair) {
        for (int i = limits.witnessesPerChair / 2; i < witnesses.size(); ++i)
          if (witnesses[i]["techniques"].size() < int(request.size()) &&
              (replacement < 0 || witnesses[i]["techniques"].size() < witnesses[replacement]["techniques"].size())) replacement = i;
      }
      const bool keep = (witnesses.size() < std::max(0, limits.witnessesPerChair) || replacement >= 0) && storedBytes < limits.witnessBytes;
      bool completeState = true;
      for (auto* layer : layers) {
        if (!layer->assigned) continue;
        for (int length = 0; length < 5; ++length) {
          stopped = stopReason();
          if (stopped.isNotEmpty()) { completeState = false; break; }
          auto result = interpretation(*layer, request, length);
          ++calls; ++chairCalls;
          ++outcomes[layer->layerId][result["outcome"].toString()];
          if (keep) interpretations.add(result);
        }
        if (!completeState) break;
      }
      if (!completeState) break; // no half-evaluated witness advertised as complete
      ++completed;
      if (keep) {
        auto witness = object({{"techniques", strings(request)}, {"interpretations", interpretations}});
        const auto bytes = int(juce::JSON::toString(witness, true).getNumBytesAsUTF8());
        const int oldBytes = replacement < 0 ? 0 : int(juce::JSON::toString(witnesses[replacement], true).getNumBytesAsUTF8());
        if (bytes <= limits.witnessBytes - storedBytes + oldBytes) {
          if (replacement < 0) witnesses.add(witness); else witnesses.set(replacement, witness);
          storedBytes += bytes - oldBytes;
        }
      }
      size_t d = axes.size();
      while (d > 0) {
        --d;
        if (++positions[d] <= axes[d].size()) break;
        positions[d] = 0;
      }
      exhausted = axes.empty() || (d == 0 && positions[0] == 0);
    }
    for (auto& row : layerRows) {
      const auto& counts = outcomes[row["id"].toString()];
      auto* o = new juce::DynamicObject();
      for (const auto& [outcome, count] : counts) o->setProperty(outcome, count);
      row.getDynamicObject()->setProperty("outcomes", juce::var(o));
    }
    chairs.add(object({{"id", chairId}, {"name", layers.front()->chairName},
        {"vocabulary", vocabularyRows}, {"dimensions", dimensionRows}, {"layers", layerRows},
        {"estimatedStates", std::isfinite(estimate) ? juce::var(estimate) : juce::var("overflow")},
        {"visitedStates", visited}, {"completedStates", completed}, {"resolverCalls", chairCalls},
        {"enumerationStatus", exhausted ? "complete" : "partial"}, {"stopReason", stopped},
        {"issues", issues}, {"issueCount", issueCount}, {"witnesses", witnesses},
        {"installedCompatibility", "unknown"}, {"readiness", "needs-review"}}));
  }
  return object({{"modelRevision", "chair-sizing-v1"}, {"chairs", chairs}, {"resolverCalls", calls},
      {"cancelled", cancelled && cancelled()},
      {"limits", object({{"statesPerChair", limits.statesPerChair}, {"resolverCalls", limits.resolverCalls},
          {"milliseconds", limits.milliseconds}, {"witnessesPerChair", limits.witnessesPerChair}})}});
}
} // namespace fiddle
