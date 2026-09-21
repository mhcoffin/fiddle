#include "BridgeRepairPlan.h"
#include "TechniqueIdentity.h"
#include <tuple>

namespace fiddle {
namespace {
using IDs = std::set<std::string>;
juce::var object(std::initializer_list<std::pair<const char *, juce::var>> fields) {
  auto *o = new juce::DynamicObject();
  for (const auto &[name, value] : fields) o->setProperty(name, value);
  return juce::var(o);
}
juce::var strings(const IDs &ids) {
  juce::Array<juce::var> result;
  for (const auto &id : ids) result.add(juce::String(id));
  return result;
}
IDs stringSet(const juce::var &values) {
  IDs result;
  if (auto *items = values.getArray()) for (const auto &id : *items) result.insert(id.toString().toStdString());
  return result;
}
bool isDefault(const ExpressionMap::Dimension &dim, int value) {
  return std::find(dim.defaultValues.begin(), dim.defaultValues.end(), value) != dim.defaultValues.end();
}
using Key = std::tuple<juce::String, juce::String, int>;
std::map<Key, juce::var> entries(const juce::var &audit) {
  std::map<Key, juce::var> result;
  if (auto *chairs = audit["chairs"].getArray()) for (const auto &chair : *chairs)
    for (const auto &layer : *chair["layers"].getArray())
      if (auto *rows = layer["rows"].getArray()) for (const auto &row : *rows) {
        auto value = row.clone();
        value.getDynamicObject()->setProperty("chairName", chair["name"]);
        value.getDynamicObject()->setProperty("layerName", layer["name"]);
        value.getDynamicObject()->setProperty("mapName", layer["mapName"]);
        result[{chair["id"].toString(), layer["id"].toString(), int(row["index"])}] = value;
      }
  return result;
}
juce::var summarize(const std::map<Key, juce::var> &rows) {
  auto result = object({{"total", int(rows.size())}, {"witness", 0}, {"missing", 0},
      {"combination", 0}, {"selection", 0}, {"uncertain", 0}});
  for (const auto &[key, row] : rows) {
    const juce::Identifier status(row["status"].toString());
    result.getDynamicObject()->setProperty(status, int(result.getProperty(status, 0)) + 1);
  }
  return result;
}
} // namespace

juce::var BridgeRepairPlan::build(const std::vector<ExpressionMap::Dimension> &decoder,
                                const std::set<int> &reservedControllers,
                                const std::vector<BridgeReachability::Assignment> &assignments,
                                const juce::var &catalog, const Options &options,
                                std::function<bool()> cancelled) {
  const auto stopped = [&] { return cancelled && cancelled(); };
  const auto before = BridgeReachability::audit(decoder, assignments, cancelled);
  auto proposed = decoder;
  auto used = reservedControllers;
  IDs sourceIDs, defaultIDs, ambiguousIDs;
  juce::Array<juce::var> identityChanges, blockedIdentityStates, warnings;
  bool metadataComplete = true;
  for (size_t dimension = 0; dimension < decoder.size(); ++dimension) {
    const auto &dim = decoder[dimension];
    used.insert(dim.ccNumber);
    for (const auto &[value, label] : dim.techniques) {
      const auto found = dim.sourceTechniqueIDs.find(value);
      if (found != dim.sourceTechniqueIDs.end()) {
        sourceIDs.insert(found->second.begin(), found->second.end());
        if (isDefault(dim, value)) defaultIDs.insert(found->second.begin(), found->second.end());
      }
      if (isDefault(dim, value)) continue;
      if (found == dim.sourceTechniqueIDs.end()) { metadataComplete = false; continue; }
      const IDs legacy{techniqueIdFromDisplay(label.toStdString())};
      if (dim.sourceIdentityAmbiguous && (dim.ambiguousSourceValues.empty() || dim.ambiguousSourceValues.count(value))) {
        ambiguousIDs.insert(found->second.begin(), found->second.end());
        blockedIdentityStates.add(object({{"cc", dim.ccNumber}, {"value", value}, {"sourceIDs", strings(found->second)},
            {"retained", strings(legacy)}, {"reason", "Multiple source identities/actions share this decoded state. Keep its current meaning until the collision is resolved explicitly."}}));
        // Preserve this exact legacy state in the proposal while allowing
        // unrelated unambiguous states to be repaired. Never choose an alias.
        proposed[dimension].sourceTechniqueIDs[value] = legacy;
        continue;
      }
      if (legacy != found->second)
        identityChanges.add(object({{"cc", dim.ccNumber}, {"value", value},
            {"before", strings(legacy)}, {"after", strings(found->second)}}));
    }
    proposed[dimension].sourceIdentityAmbiguous = false;
    proposed[dimension].ambiguousSourceValues.clear();
  }
  const bool canonical = options.preserveIdentities && metadataComplete;
  if (!metadataComplete) warnings.add("Exact source identities are missing. Identity repair cannot be previewed for this decoder snapshot.");
  if (!blockedIdentityStates.isEmpty()) warnings.add("Some source identities collide on the same CC state. Those states retain their legacy interpretation in the preview; review the collisions before map generation.");
  const auto identityAudit = canonical ? BridgeReachability::audit(proposed, assignments, cancelled, true) : before;
  const auto vocabulary = stringSet(identityAudit["vocabulary"]);
  std::map<std::string, juce::var> definitions;
  if (auto *items = catalog["techniques"].getArray())
    for (const auto &item : *items) definitions[item["id"].toString().toStdString()] = item;
  std::map<std::string, juce::Array<juce::var>> affected;
  for (const auto &assignment : assignments) if (assignment.assigned) {
    for (size_t i = 0; i < assignment.map.combinations.size(); ++i)
      for (const auto &id : assignment.map.combinations[i].techniqueIDs)
        if (id != "pt.natural" && !vocabulary.count(id)) affected[id].add(object({
            {"chair", assignment.chairName}, {"chairId", assignment.chairId},
            {"layer", assignment.layerName}, {"layerId", assignment.layerId},
            {"map", juce::String(assignment.map.name)}, {"entry", juce::String(assignment.map.combinations[i].name)},
            {"index", int(i)}}));
  }
  std::vector<int> free;
  for (int cc = 102; cc <= 119; ++cc) if (!used.count(cc)) free.push_back(cc);
  juce::Array<juce::var> candidates;
  IDs selected;
  for (const auto &[id, uses] : affected) {
    const auto definition = definitions.find(id);
    const auto row = definition != definitions.end() ? definition->second : juce::var{};
    juce::String reason;
    if (!bool(before["decoderAvailable"])) reason = "Unsupported current decoder; resolve that before allocating controllers.";
    else if (defaultIDs.count(id)) reason = "This identity shares a reset/default value. Its on/off semantics must be separated explicitly, not duplicated on another controller.";
    else if (ambiguousIDs.count(id)) reason = "This identity shares a CC state with another source identity. Resolve that collision before assigning a new encoding.";
    else if (sourceIDs.count(id) && !canonical) reason = "Already encoded under a different decoded identity. Preview exact-ID repair first; do not duplicate its switch.";
    else if (bool(row["localVariants"])) reason = "Local definitions differ. Choose the intended definition before generating a switch.";
    else if (row["definitionState"].toString() != "local") reason = "No local definition found. Identify/import the required Dorico Library Addition or export the project's definition first.";
    else {
      // Only local, explicit, unambiguous attribute/direction definitions are
      // mechanically allocatable. Never guess aliases or inherit semantics.
      bool usable = false;
      if (auto *defs = row["definitions"].getArray()) for (const auto &def : *defs) {
        bool local = false;
        if (auto *sources = def["sources"].getArray()) for (const auto &source : *sources) {
          const auto kind = catalog["sources"][int(source)]["kind"].toString();
          if (kind == "builtin" || kind == "installed") local = true;
        }
        if (!local) continue;
        const auto type = def["articulationType"].toString();
        if (def["alias"].toString().isNotEmpty() || def["parent"].toString().isNotEmpty())
          reason = "Alias/inherited definition needs review. The planner will not guess its musical meaning.";
        else if (type == "kAttribute" || type == "kDirection") usable = true;
      }
      if (!usable && reason.isEmpty()) reason = "No explicit installed attribute/direction definition; review the source definition.";
    }
    const bool eligible = reason.isEmpty();
    if (eligible && options.additions.count(id)) selected.insert(id);
    if (eligible) reason = "Propose an independent on/off controller: 127 on, 0 off. Review notation, cancellation and interactions before map generation.";
    candidates.add(object({{"id", juce::String(id)}, {"eligible", eligible}, {"selected", eligible && options.additions.count(id) != 0},
        {"reason", reason}, {"affected", uses}, {"appearanceCount", row["appearances"].size()}}));
  }
  const bool overflow = selected.size() > free.size();
  if (overflow) warnings.add("Selected additions exceed free independent controllers. No additions were allocated; choose fewer, or design shared dimensions/chair profiles. This is not proof that CC102–119 lacks total capacity.");
  size_t allocated = 0;
  if (!overflow) for (auto &candidate : candidates) {
    if (!bool(candidate["selected"])) continue;
    ExpressionMap::Dimension dim;
    dim.ccNumber = free[allocated++];
    dim.name = "Repair CC" + juce::String(dim.ccNumber);
    // Avoid dimension-name collisions with the existing decoder.
    while (std::any_of(proposed.begin(), proposed.end(), [&](const auto &old) { return old.name == dim.name; })) dim.name += "_";
    const auto id = candidate["id"].toString().toStdString();
    dim.defaultValues = {0};
    dim.techniques = {{0, "Natural"}, {127, juce::String(id)}};
    dim.sourceTechniqueIDs = {{0, {}}, {127, {id}}};
    proposed.push_back(std::move(dim));
    candidate.getDynamicObject()->setProperty("cc", free[allocated - 1]);
  }
  const auto after = allocated ? BridgeReachability::audit(proposed, assignments, cancelled, canonical) : identityAudit;
  const auto oldRows = entries(before), newRows = entries(after);
  juce::Array<juce::var> changes, remaining;
  int gained = 0, regressed = 0;
  for (const auto &[key, oldRow] : oldRows) {
    const auto found = newRows.find(key);
    if (found == newRows.end()) continue;
    const auto &next = found->second;
    if (oldRow["status"] != next["status"]) {
      const bool gain = next["status"].toString() == "witness";
      const bool regression = oldRow["status"].toString() == "witness" && !gain;
      gained += gain; regressed += regression;
      changes.add(object({{"chair", next["chairName"]}, {"layer", next["layerName"]},
          {"map", next["mapName"]}, {"entry", next["name"]}, {"index", next["index"]},
          {"before", oldRow["status"]}, {"after", next["status"]}, {"regression", regression},
          {"reason", next["reason"]}, {"witness", next["witness"]}}));
    }
    if (next["status"].toString() != "witness") remaining.add(next);
  }
  if (regressed) warnings.add("Some previously demonstrated requests no longer have a witness. Review these regressions; do not silently alias or replace IDs to hide them.");
  juce::Array<juce::var> axes, freeValues;
  for (int cc : free) freeValues.add(cc);
  for (const auto &dim : proposed) {
    juce::Array<juce::var> states;
    for (const auto &[value, display] : dim.techniques) {
      IDs ids;
      if (!isDefault(dim, value)) {
        if (canonical) {
          const auto found = dim.sourceTechniqueIDs.find(value);
          if (found != dim.sourceTechniqueIDs.end()) ids = found->second;
        } else ids.insert(techniqueIdFromDisplay(display.toStdString()));
      }
      states.add(object({{"value", value}, {"default", isDefault(dim, value)}, {"ids", strings(ids)}}));
    }
    axes.add(object({{"cc", dim.ccNumber}, {"name", dim.name}, {"states", states}}));
  }
  auto plan = object({{"schemaVersion", 1}, {"kind", "fiddle-bridge-repair-plan"}, {"installable", false},
      {"preserveIdentities", options.preserveIdentities}, {"canonicalPreview", canonical},
      {"metadataComplete", metadataComplete}, {"identityChanges", identityChanges},
      {"blockedIdentityStates", blockedIdentityStates},
      {"candidates", candidates}, {"selectedAdditions", strings(selected)}, {"freeControllers", freeValues},
      {"allocated", int(allocated)}, {"capacityExceeded", overflow}, {"warnings", warnings},
      {"before", summarize(oldRows)}, {"after", summarize(newRows)}, {"gained", gained}, {"regressed", regressed},
      {"changes", changes}, {"remaining", remaining}, {"proposedDecoder", axes}, {"chairs", before["chairs"]},
      {"cancelled", stopped()},
      {"scope", "Current project, all layers. One proposed universal profile; no per-layer bridge overrides."},
      {"nextStep", "Review this plan, then implement versioned map/decoder generation and explicit activation. This JSON is not a Dorico expression map or a loadable Fiddle profile. No live behavior has changed."}});
  plan.getDynamicObject()->setProperty("planId", juce::String::toHexString(juce::JSON::toString(plan, true).hashCode64()));
  return plan;
}
} // namespace fiddle
