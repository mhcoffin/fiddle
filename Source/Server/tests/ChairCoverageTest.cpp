#include "../ChairCoverage.h"
#include "../ExpressionMapParser.h"
#include <iostream>
using namespace fiddle;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; } } while(false)
TechniqueCombination entry(std::string name, std::set<std::string> ids, bool addon = false) {
  TechniqueCombination c; c.name = std::move(name); c.techniqueIDs = std::move(ids); c.isAddOn = addon; return c;
}
BridgeReachability::Assignment layer(std::string id, std::vector<TechniqueCombination> entries) {
  BridgeReachability::Assignment a;
  a.chairId = "chair"; a.chairName = "Violin 1"; a.layerId = id; a.layerName = id;
  a.map.name = "Test " + id; a.map.combinations = std::move(entries); return a;
}
juce::var run(std::vector<BridgeReachability::Assignment> layers, ChairCoverage::Limits limits = {}) {
  return ChairCoverage::analyze(layers, {}, limits)["chairs"][0];
}
bool hasIssue(const juce::var& chair, const char* code) {
  for (const auto& issue : *chair["issues"].getArray()) if (issue["code"].toString() == code) return true;
  return false;
}
juce::var witness(const juce::var& chair, const std::set<std::string>& ids) {
  for (const auto& w : *chair["witnesses"].getArray()) {
    std::set<std::string> found;
    for (const auto& id : *w["techniques"].getArray()) found.insert(id.toString().toStdString());
    if (found == ids) return w;
  }
  return {};
}
}
int main(int argc, char** argv) {
  // Explicit opt-in snapshots, one file = one layer in a test chair. This
  // neither discovers nor mutates the live project or installed Dorico maps.
  if (argc > 1) {
    std::vector<BridgeReachability::Assignment> layers;
    for (int i = 1; i < argc; ++i) {
      const juce::File file(argv[i]);
      auto maps = parseAllExpressionMaps(file);
      if (maps.empty()) return 2;
      for (auto& map : maps) {
        auto a = layer(std::to_string(i) + ":" + map.entityID, {});
        a.map = std::move(map); a.sourceXml = file.loadFileAsString(); a.sourcePath = file.getFullPathName();
        layers.push_back(std::move(a));
      }
    }
    std::cout << juce::JSON::toString(ChairCoverage::analyze(layers, {}, {}), true); return 0;
  }
  ChairCoverage::Limits large;
  large.witnessesPerChair = 1024; large.witnessBytes = 16 * 1024 * 1024; large.milliseconds = 60000;
  auto a = layer("a", {entry("Legato", {"pt.legato"})});
  auto b = layer("b", {entry("Mute", {"pt.muted"})});
  auto c = layer("c", {entry("Flautando", {"pt.flautando"})});
  auto baseline = run({a}, large);
  CHECK(int(baseline["estimatedStates"]) == 24); // duration dependencies: 3*2*2*2
  CHECK(baseline["vocabulary"].size() == 5);
  auto two = run({a, b}, large);
  CHECK(int(two["estimatedStates"]) == 48);
  CHECK(witness(two, {"pt.legato", "pt.muted"}).isObject());
  auto three = run({a, b, c}, large);
  CHECK(int(three["estimatedStates"]) == 96);
  CHECK(int(three["completedStates"]) == 96);
  CHECK(int(three["resolverCalls"]) == 96 * 3 * 5);
  auto joint = witness(three, {"pt.legato", "pt.muted", "pt.flautando"});
  CHECK(joint["interpretations"].size() == 15);
  CHECK(joint["interpretations"][0]["unmatched"].getArray()->contains(juce::String("pt.muted")));
  // The same unprojected set reaches every resolver, with five length cases.
  CHECK(joint["interpretations"][5]["unmatched"].getArray()->contains(juce::String("pt.legato")));
  auto duplicate = a; duplicate.layerId = "duplicate";
  CHECK(int(run({a, duplicate}, large)["estimatedStates"]) == 24);
  auto reordered = run({c, a, b}, large);
  CHECK(juce::JSON::toString(three) == juce::JSON::toString(reordered));

  auto addons = layer("addons", {entry("Legato", {"pt.legato"}), entry("Mute", {"pt.muted"}, true), entry("Color", {"pt.flautando"}, true)});
  auto addReport = run({addons}, large);
  CHECK(witness(addReport, {"pt.legato", "pt.muted", "pt.flautando"})["interpretations"][0]["addons"].size() == 2);
  auto compound = layer("conditions", {entry("Short", {"pt.legato"}), entry("Long", {"pt.legato"})});
  compound.map.combinations[0].conditionString = "NoteLength >= kVeryShort AND NoteLength < kLong";
  compound.map.combinations[1].conditionString = "NoteLength >= kLong";
  for (auto& row : compound.map.combinations) row.condition = parseConditionString(row.conditionString);
  auto conditions = run({compound}, large);
  CHECK(!hasIssue(conditions, "condition"));
  auto cw = witness(conditions, {"pt.legato"})["interpretations"];
  CHECK(cw[0]["outcome"].toString() == "no-match");
  CHECK(cw[1]["base"]["name"].toString() == "Short");
  CHECK(cw[4]["base"]["name"].toString() == "Long");
  addons.map.combinations[1].conditionString = "NoteLength < kShort";
  CHECK(hasIssue(run({addons}, large), "addon-condition"));
  compound.map.combinations[0].conditionString = "Velocity > 80";
  CHECK(hasIssue(run({compound}, large), "condition"));
  compound.map.combinations[0].conditionString = "NoteLength < nonsense";
  CHECK(hasIssue(run({compound}, large), "condition"));

  auto conflict = layer("bad", {entry("Impossible", {"pt.nonVibrato", "pt.wideVibrato"})});
  auto conflicted = run({conflict}, large);
  CHECK(hasIssue(conflicted, "mutex-conflict"));
  CHECK(!witness(conflicted, {"pt.nonVibrato", "pt.wideVibrato"}).isObject());
  CHECK(witness(conflicted, {}).isObject());
  CHECK(witness(conflicted, {"pt.nonVibrato"}).isObject());
  auto percussionConflict = layer("motor", {entry("Both", {"pt.motorOn", "pt.motorOff"})});
  CHECK(hasIssue(run({percussionConflict}, large), "mutex-conflict"));
  auto provisional = layer("provisional", {entry("Both rolls", {"pt.tremolo", "pt.roll"})});
  auto provisionalReport = run({provisional}, large);
  CHECK(!hasIssue(provisionalReport, "mutex-conflict"));
  CHECK(witness(provisionalReport, {"pt.tremolo", "pt.roll"}).isObject());
  CHECK(hasIssue(provisionalReport, "unreviewed-dimension"));
  auto natural = layer("natural", {entry("Natural", {"pt.natural"})});
  auto nr = run({natural}, large);
  CHECK(witness(nr, {}).isObject() && witness(nr, {"pt.natural"}).isObject());
  CHECK(witness(nr, {})["interpretations"][0]["outcome"].toString() == "fallback");
  auto missing = a; missing.assigned = false; missing.hasProcessors = true;
  auto mr = run({missing});
  CHECK(hasIssue(mr, "missing-map") && hasIssue(mr, "processors"));
  CHECK(int(mr["resolverCalls"]) == 0);
  CHECK(mr["installedCompatibility"].toString() == "unknown");
  auto custom = layer("custom", {entry("Custom", {"custom.lyrical"})});
  CHECK(hasIssue(run({custom}), "unknown-identity"));
  auto catalog = juce::JSON::parse(R"({"sources":[{"kind":"builtin"}],"techniques":[
    {"id":"pt.legato","definitions":[{"sources":[0],"alias":"pt.natural"}]}]})");
  auto known = ChairCoverage::analyze({a}, catalog, large)["chairs"][0];
  CHECK(hasIssue(known, "definition-review"));
  bool knownLegato = false;
  for (const auto& pt : *known["vocabulary"].getArray())
    if (pt["id"].toString() == "pt.legato") knownLegato = bool(pt["builtin"]);
  CHECK(knownLegato);
  a.map.combinations[0].switchOnActions.push_back({SwitchActionType::Unknown, 0, 0});
  CHECK(hasIssue(run({a}), "unknown-action"));
  a.map.combinations[0].switchOnActions.clear();

  auto capped = large; capped.statesPerChair = 3;
  auto partial = run({a, b, c}, capped);
  CHECK(partial["enumerationStatus"].toString() == "partial");
  CHECK(int(partial["completedStates"]) == 3);
  capped = large; capped.resolverCalls = 6;
  partial = run({a, b}, capped);
  CHECK(int(partial["completedStates"]) == 0 && int(partial["resolverCalls"]) == 6);
  CHECK(partial["witnesses"].size() == 0);
  CHECK(partial["stopReason"].toString() == "resolver-limit");
  capped = large; capped.resolverCalls = 120;
  CHECK(run({a}, capped)["enumerationStatus"].toString() == "complete"); // exact boundary
  auto otherChair = a; otherChair.chairId = "z-other";
  auto sharedBudget = ChairCoverage::analyze({a, otherChair}, {}, capped);
  CHECK(int(sharedBudget["resolverCalls"]) == 120);
  CHECK(sharedBudget["chairs"][1]["enumerationStatus"].toString() == "partial");
  capped = large; capped.witnessBytes = 0;
  auto boundedOutput = run({a}, capped);
  CHECK(boundedOutput["witnesses"].size() == 0);
  CHECK(boundedOutput["enumerationStatus"].toString() == "complete");
  auto cancelled = ChairCoverage::analyze({a}, {}, large, [] { return true; });
  CHECK(bool(cancelled["cancelled"]));
  CHECK(cancelled["chairs"][0]["stopReason"].toString() == "cancelled");
  capped = large; capped.milliseconds = 0;
  CHECK(run({a}, capped)["stopReason"].toString() == "time-limit");
  auto huge = a;
  for (int i = 0; i < 100; ++i) huge.map.combinations.push_back(entry("Unknown", {"custom." + std::to_string(i)}));
  capped = large; capped.statesPerChair = 2;
  auto hugeReport = run({huge}, capped);
  CHECK(double(hugeReport["estimatedStates"]) > 1e30);
  CHECK(int(hugeReport["completedStates"]) == 2);
  CHECK(hugeReport["enumerationStatus"].toString() == "partial");
  auto sampled = run({a, b, c});
  int richest = 0;
  for (const auto& w : *sampled["witnesses"].getArray()) richest = std::max(richest, w["techniques"].size());
  CHECK(richest == 6); // not merely the first 16 lexicographic requests
  std::cout << (failures ? "FAIL" : "PASS") << ": chair coverage (" << failures << " failures)\n";
  return failures ? 1 : 0;
}
