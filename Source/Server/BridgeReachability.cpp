#include "BridgeReachability.h"
#include "TechniqueIdentity.h"
#include <algorithm>
#include <map>

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
struct Option { int cc, value, length; IDs ids; };
bool understoodCondition(const TechniqueCombination &entry) {
  if (entry.conditionString.empty()) return true;
  if (entry.condition.empty()) return false;
  const auto text = juce::String(entry.conditionString).removeCharacters(" \t\r\n");
  for (const auto *op : {"<", "<=", ">", ">=", "=="})
    for (const auto *threshold : {"kVeryShort", "kShort", "kMedium", "kLong", "kVeryLong"})
      if (text == juce::String("NoteLength") + op + threshold) return true;
  // The permissive production parser may assign a default threshold to an
  // unknown expression. Do not turn that into a positive coverage claim.
  return false;
}
struct Decoder {
  std::vector<std::vector<Option>> dimensions;
  IDs vocabulary;
  bool supported = true;
  explicit Decoder(const std::vector<ExpressionMap::Dimension> &input, bool canonical) {
    std::set<int> controllers;
    std::set<juce::String> names;
    bool hasLength = false;
    for (const auto &dim : input) {
      // Duplicate names/controllers overwrite runtime state. Do not pretend
      // independent axes model that case. The current shipped map is unique.
      if (dim.ccNumber < 102 || dim.ccNumber > 119 ||
          !controllers.insert(dim.ccNumber).second || !names.insert(dim.name).second)
        supported = false;
      if (dim.ccNumber == 102) hasLength = true;
      std::vector<Option> options;
      std::set<std::pair<IDs, int>> seen;
      if (canonical && dim.sourceIdentityAmbiguous) supported = false;
      std::set<int> values(dim.defaultValues.begin(), dim.defaultValues.end());
      for (const auto &[value, name] : dim.techniques) values.insert(value);
      // Only declared/default states, not invented CC values. An explicit
      // default is decoded as absent, exactly as collectInputTechniqueIds does.
      for (int value : values) {
        if (value < 0 || value > 127) { supported = false; continue; }
        IDs ids;
        const auto it = dim.techniques.find(value);
        if (it != dim.techniques.end() &&
            std::find(dim.defaultValues.begin(), dim.defaultValues.end(), value) == dim.defaultValues.end()) {
          if (canonical) {
            const auto source = dim.sourceTechniqueIDs.find(value);
            if (source == dim.sourceTechniqueIDs.end()) supported = false;
            else ids = source->second;
          } else {
            const auto id = techniqueIdFromDisplay(it->second.toStdString());
            if (!id.empty()) ids.insert(id);
          }
        }
        const int length = dim.ccNumber == 102 ? value % 5 : -1;
        vocabulary.insert(ids.begin(), ids.end());
        if (seen.insert({ids, length}).second) options.push_back({dim.ccNumber, value, length, ids});
      }
      if (options.empty()) supported = false;
      dimensions.push_back(std::move(options));
    }
    // Without CC102 the runtime uses duration as a fallback; this first audit
    // deliberately models only the existing compound CC102 protocol.
    supported = supported && hasLength && !dimensions.empty();
  }
};

juce::var auditMap(const Decoder &decoder, const ExpressionMapData &map,
                   const std::function<bool()> &cancelled) {
  juce::Array<juce::var> rows;
  IDs required;
  int mapBudget = 100000;
  const auto stopped = [&] { return cancelled && cancelled(); };
  for (size_t index = 0; index < map.combinations.size() && !stopped(); ++index) {
    const auto &target = map.combinations[index];
    required.insert(target.techniqueIDs.begin(), target.techniqueIDs.end());
    auto row = object({{"index", int(index)}, {"name", juce::String(target.name)},
        {"addOn", target.isAddOn}, {"techniques", strings(target.techniqueIDs)},
        {"condition", juce::String(target.conditionString)}});
    auto setResult = [&](const char *status, const juce::String &reason) {
      row.getDynamicObject()->setProperty("status", status);
      row.getDynamicObject()->setProperty("reason", reason);
    };
    IDs missing;
    for (const auto &id : target.techniqueIDs)
      if (id != "pt.natural" && !decoder.vocabulary.count(id)) missing.insert(id);
    if (!decoder.supported) {
      setResult("uncertain", "No supported decoder snapshot (requires the current unique-dimension CC102 protocol).");
    } else if (!missing.empty()) {
      setResult("missing", "Required identities cannot be produced by the current decoder. A fallback selecting a switch does not convey the missing intent.");
      row.getDynamicObject()->setProperty("missing", strings(missing));
    } else if (!understoodCondition(target)) {
      setResult("uncertain", "This condition is not understood by the current resolver; selection would not prove its meaning is preserved.");
    } else {
      // Exercise canonical requests: the base's own PT set, or an add-on's
      // PT set with each base. This is NOT exhaustive proof over all supersets.
      std::set<IDs> requests;
      auto addRequest = [&](IDs ids) {
        requests.insert(ids);
        // Natural commonly means the absence of another articulation. Try
        // both explicit Natural (if encoded) and the reset/default state.
        if (ids.erase("pt.natural")) requests.insert(std::move(ids));
      };
      if (!target.isAddOn) addRequest(target.techniqueIDs);
      else for (const auto &base : map.combinations) if (!base.isAddOn) {
        auto ids = base.techniqueIDs;
        ids.insert(target.techniqueIDs.begin(), target.techniqueIDs.end());
        addRequest(std::move(ids));
      }
      bool representable = false, found = false, limited = false;
      int rowBudget = 4096;
      juce::var witness;
      juce::String alternative;
      for (const auto &desired : requests) {
        if (found || stopped()) break;
        bool possible = true;
        for (const auto &id : desired) if (!decoder.vocabulary.count(id)) possible = false;
        if (!possible) continue;
        std::vector<std::vector<Option>> choices;
        for (const auto &axis : decoder.dimensions) {
          std::vector<Option> compatible;
          for (const auto &option : axis)
            if (std::includes(desired.begin(), desired.end(), option.ids.begin(), option.ids.end())) compatible.push_back(option);
          if (compatible.empty()) possible = false;
          choices.push_back(std::move(compatible));
        }
        if (!possible) continue;
        std::vector<Option> chosen;
        std::function<void(size_t, IDs, int)> search = [&](size_t axis, IDs actual, int length) {
          if (found || limited || stopped()) return;
          if (--rowBudget < 0 || --mapBudget < 0) { limited = true; return; }
          if (axis == choices.size()) {
            if (actual != desired) return;
            representable = true;
            const auto match = map.resolveMatch(actual, static_cast<LengthCategory>(length));
            const bool selected = target.isAddOn
                ? std::find(match.addOns.begin(), match.addOns.end(), &target) != match.addOns.end()
                : match.base == &target;
            if (!selected) { alternative = match.base ? juce::String(match.base->name) : "(no base switch)"; return; }
            // A parsed condition on an add-on is not evaluated by the runtime
            // resolver. Leave it uncertain rather than advertise coverage.
            if (target.isAddOn && !target.conditionString.empty()) return;
            found = true;
            juce::Array<juce::var> cc;
            for (const auto &option : chosen) cc.add(object({{"cc", option.cc}, {"value", option.value}}));
            witness = object({{"cc", cc}, {"techniques", strings(actual)}, {"length", length},
                {"base", match.base ? juce::String(match.base->name) : juce::String{}}});
            return;
          }
          for (const auto &option : choices[axis]) {
            auto next = actual;
            next.insert(option.ids.begin(), option.ids.end());
            chosen.push_back(option);
            search(axis + 1, std::move(next), option.length >= 0 ? option.length : length);
            chosen.pop_back();
            if (found || limited || stopped()) break;
          }
        };
        search(0, {}, 4);
        if (limited) break;
      }
      if (found) {
        setResult("witness", "A declared/default CC state decodes to this request and the actual resolver selects this entry.");
        row.getDynamicObject()->setProperty("witness", witness);
      } else if (limited || stopped()) {
        setResult("uncertain", "Search budget reached or scan cancelled. No claim of unreachability is made.");
      } else if (target.isAddOn) {
        setResult("uncertain", "No witness with the tested base-plus-add-on requests. Other supersets, base availability, and add-on conditions need review.");
      } else if (!representable) {
        setResult("combination", "No declared/default decoder state expresses this exact request. Individual identities may share one controller, or another dimension may force an extra identity.");
      } else {
        setResult("selection", "The request is expressible, but no tested length selects this entry. Check conditions and competing entries; other supersets have not been exhausted.");
        row.getDynamicObject()->setProperty("alternative", alternative);
      }
    }
    rows.add(row);
  }
  return object({{"rows", rows}, {"required", strings(required)}});
}
} // namespace

juce::var BridgeReachability::audit(const std::vector<ExpressionMap::Dimension> &dimensions,
                                  const std::vector<Assignment> &assignments,
                                  std::function<bool()> cancelled, bool canonicalPreview) {
  const Decoder decoder(dimensions, canonicalPreview);
  juce::Array<juce::var> chairs;
  std::map<juce::String, int> chairIndex;
  std::map<juce::String, IDs> requirements;
  for (const auto &assignment : assignments) {
    if (cancelled && cancelled()) break;
    auto [it, inserted] = chairIndex.emplace(assignment.chairId, chairs.size());
    if (inserted) chairs.add(object({{"id", assignment.chairId}, {"name", assignment.chairName},
        {"profile", "builtin-universal"}, {"layers", juce::Array<juce::var>{}}}));
    auto layer = object({{"id", assignment.layerId}, {"name", assignment.layerName},
        {"assigned", assignment.assigned}, {"mapId", juce::String(assignment.map.entityID)},
        {"mapName", juce::String(assignment.map.name)}});
    if (assignment.assigned) {
      auto result = auditMap(decoder, assignment.map, cancelled);
      layer.getDynamicObject()->setProperty("rows", result["rows"]);
      for (const auto &id : *result["required"].getArray()) requirements[assignment.chairId].insert(id.toString().toStdString());
    }
    chairs.getReference(it->second)["layers"].getArray()->add(layer);
  }
  for (auto &chair : chairs)
    chair.getDynamicObject()->setProperty("required", strings(requirements[chair["id"].toString()]));
  return object({{"schemaVersion", 1}, {"chairs", chairs}, {"decoderAvailable", decoder.supported},
      {"canonicalPreview", canonicalPreview},
      {"vocabulary", strings(decoder.vocabulary)}, {"cancelled", cancelled && cancelled()}});
}
} // namespace fiddle
