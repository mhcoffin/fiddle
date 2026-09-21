#include "../BridgeRepairPlan.h"
#include "../TechniqueCatalog.h"
#include "../ExpressionMapParser.h"
#include <iostream>
using namespace fiddle;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; } } while(false)
juce::var object(std::initializer_list<std::pair<const char *, juce::var>> fields) {
  auto *o = new juce::DynamicObject();
  for (const auto &[name, value] : fields) o->setProperty(name, value);
  return juce::var(o);
}
TechniqueCombination entry(const std::string &name, std::set<std::string> ids) {
  TechniqueCombination e; e.name = name; e.techniqueIDs = std::move(ids); return e;
}
juce::var candidate(const juce::var &plan, const juce::String &id) {
  for (const auto &c : *plan["candidates"].getArray()) if (c["id"].toString() == id) return c;
  return {};
}
}
int main(int argc, char **argv) {
  // Optional file-only smoke audit. These are explicit test assignments, not
  // claims about the running project; no vendor files or live state are changed.
  if (argc > 1) {
    TechniqueCatalog::Request request;
    BridgeRepairPlan::Options options;
    for (int i = 1; i + 1 < argc; i += 2) {
      const juce::String flag(argv[i]), value(argv[i + 1]);
      if (flag == "--add") { options.additions.insert(value.toStdString()); continue; }
      const juce::File file(value);
      if (flag == "--bridge") {
        ExpressionMap bridge;
        if (!bridge.loadFromDoricolib(file)) return 2;
        request.decoder = bridge.getDimensions();
        request.reservedControllers = bridge.getUsedReservedControllers();
        request.sources.push_back({file, "bridge", {}});
      } else if (flag == "--map") {
        const auto maps = parseAllExpressionMaps(file);
        if (maps.empty()) return 2;
        for (const auto &map : maps) request.assignments.push_back({"test-chair", "Explicit file audit",
            juce::String(i) + map.entityID, file.getFileName(), map});
      } else request.sources.push_back({file, flag.fromFirstOccurrenceOf("--", false, false), {}});
    }
    const auto report = TechniqueCatalog::scan(request);
    const auto plan = BridgeRepairPlan::build(request.decoder, request.reservedControllers, request.assignments, report, options);
    std::cout << juce::JSON::toString(plan, true);
    return 0;
  }
  ExpressionMap::Dimension dim;
  dim.name = "Articulation"; dim.ccNumber = 102; dim.defaultValues = {0};
  dim.techniques = {{0, "Natural"}, {5, "SulTasto"}, {10, "Staccato+Tenuto"}};
  dim.sourceTechniqueIDs = {{0, {"pt.natural"}}, {5, {"pt.sulTasto"}}, {10, {"pt.staccato", "pt.tenuto"}}};
  ExpressionMap::Dimension vibrato;
  vibrato.name = "Vibrato"; vibrato.ccNumber = 103; vibrato.defaultValues = {64};
  vibrato.techniques = {{64, "Vibrato"}, {0, "NonVibrato"}};
  vibrato.sourceTechniqueIDs = {{64, {"pt.vibrato"}}, {0, {"pt.nonVibrato"}}};
  ExpressionMapData map;
  map.name = "One map";
  map.combinations = {entry("Natural", {"pt.natural"}), entry("Canonical", {"pt.sulTasto"}),
      entry("Legacy", {"pt.sultasto"}), entry("Compound", {"pt.staccato", "pt.tenuto"}),
      entry("New", {"pt.user.long"}), entry("Vibrato", {"pt.vibrato"}), entry("Alias", {"pt.alias"})};
  std::vector<BridgeReachability::Assignment> assignments{{"chair", "Strings", "one", "VSL", map}};
  auto makeDefinition = [](const char *id, const char *state, bool variants, const char *alias = "") {
    return object({{"id", id}, {"definitionState", state}, {"localVariants", variants},
        {"appearances", juce::Array<juce::var>{}}, {"definitions", juce::Array<juce::var>{object({
          {"articulationType", "kAttribute"}, {"alias", alias}, {"sources", juce::Array<juce::var>{0}}})}}});
  };
  auto catalog = object({{"sources", juce::Array<juce::var>{object({{"kind", "installed"}})}},
      {"techniques", juce::Array<juce::var>{makeDefinition("pt.user.long", "local", false),
          makeDefinition("pt.alias", "local", false, "pt.user.long")}}});
  const std::vector<ExpressionMap::Dimension> decoder{dim, vibrato};
  const auto base = BridgeRepairPlan::build(decoder, {102, 103, 104}, assignments, catalog, {});
  CHECK(base["kind"].toString() == "fiddle-bridge-repair-plan");
  CHECK(!bool(base["installable"]));
  CHECK(int(base["gained"]) == 2);
  CHECK(int(base["regressed"]) == 1);
  CHECK(bool(candidate(base, "pt.user.long")["eligible"]));
  CHECK(!bool(candidate(base, "pt.alias")["eligible"]));
  CHECK(!bool(candidate(base, "pt.vibrato")["eligible"]));
  CHECK(int(base["freeControllers"][0]) == 105);
  CHECK(int(base["allocated"]) == 0);
  CHECK(base["planId"] == BridgeRepairPlan::build(decoder, {102, 103, 104}, assignments, catalog, {})["planId"]);
  BridgeRepairPlan::Options options;
  options.additions = {"pt.user.long", "pt.alias", "pt.unknown"};
  const auto added = BridgeRepairPlan::build(decoder, {102, 103, 104}, assignments, catalog, options);
  CHECK(int(added["allocated"]) == 1);
  CHECK(int(candidate(added, "pt.user.long")["cc"]) == 105);
  CHECK(added["selectedAdditions"].size() == 1);
  CHECK(int(added["gained"]) == 3);
  CHECK(added["planId"] != base["planId"]);
  CHECK(decoder[0].techniques.at(5) == "SulTasto");
  options.preserveIdentities = false; options.additions.clear();
  const auto unchanged = BridgeRepairPlan::build(decoder, {}, assignments, catalog, options);
  CHECK(int(unchanged["gained"]) == 0 && int(unchanged["regressed"]) == 0);
  CHECK(!bool(candidate(unchanged, "pt.sulTasto")["eligible"]));
  std::set<int> full;
  for (int cc = 102; cc <= 119; ++cc) full.insert(cc);
  options = {}; options.additions = {"pt.user.long"};
  const auto overflow = BridgeRepairPlan::build(decoder, full, assignments, catalog, options);
  CHECK(bool(overflow["capacityExceeded"]));
  CHECK(int(overflow["allocated"]) == 0);
  auto reference = catalog.clone();
  reference["techniques"][0].getDynamicObject()->setProperty("definitionState", "reference");
  CHECK(!bool(candidate(BridgeRepairPlan::build(decoder, {}, assignments, reference, options), "pt.user.long")["eligible"]));
  auto conflict = catalog.clone();
  conflict["techniques"][0].getDynamicObject()->setProperty("localVariants", true);
  CHECK(!bool(candidate(BridgeRepairPlan::build(decoder, {}, assignments, conflict, options), "pt.user.long")["eligible"]));
  auto ambiguous = decoder;
  ambiguous[0].sourceIdentityAmbiguous = true;
  const auto collision = BridgeRepairPlan::build(ambiguous, {}, assignments, catalog, {});
  CHECK(bool(collision["canonicalPreview"]));
  CHECK(collision["blockedIdentityStates"].size() == 2);
  CHECK(int(collision["gained"]) == 0);
  CHECK(int(collision["regressed"]) == 0);
  CHECK(bool(BridgeRepairPlan::build(decoder, {}, assignments, catalog, {}, [] { return true; })["cancelled"]));
  auto second = assignments.front(); second.layerId = "two"; second.layerName = "Other library";
  second.map.combinations = {entry("Legacy", {"pt.sultasto"})}; assignments.push_back(second);
  CHECK(int(BridgeRepairPlan::build(decoder, {}, assignments, catalog, {})["regressed"]) == 2);
  std::cout << (failures ? "FAIL" : "PASS") << ": bridge repair plan (" << failures << " failures)\n";
  return failures ? 1 : 0;
}
