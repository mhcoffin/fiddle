#include "../BridgeReachability.h"
#include "../TechniqueIdentity.h"
#include "../ExpressionMapParser.h"
#include <iostream>
using namespace fiddle;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; } } while(false)
TechniqueCombination entry(std::string name, std::set<std::string> ids, bool addon = false) {
  TechniqueCombination c; c.name = std::move(name); c.techniqueIDs = std::move(ids); c.isAddOn = addon; return c;
}
}
int main(int argc, char **argv) {
  // Optional read-only audit of explicitly supplied files. These are test
  // assignments, not discovery of the user's actual running project.
  if (argc >= 3) {
    ExpressionMap bridge;
    if (!bridge.loadFromDoricolib(juce::File(argv[1]))) return 2;
    std::vector<BridgeReachability::Assignment> assignments;
    for (int i = 2; i < argc; ++i) {
      const auto maps = parseAllExpressionMaps(juce::File(argv[i]));
      if (maps.empty()) { std::cerr << "No parsed maps: " << argv[i] << "\n"; return 2; }
      for (const auto &map : maps) assignments.push_back({"fixture", "File audit (not project assignments)",
          juce::String(i) + ":" + map.entityID, juce::File(argv[i]).getFileName(), map});
    }
    std::cout << juce::JSON::toString(BridgeReachability::audit(bridge.getDimensions(), assignments), true);
    return 0;
  }
  ExpressionMap::Dimension articulation;
  articulation.name = "Articulation"; articulation.ccNumber = 102;
  for (int n = 0; n < 5; ++n) {
    articulation.defaultValues.push_back(n);
    articulation.techniques[n] = "Natural";
    articulation.techniques[5 + n] = "Staccato";
    articulation.techniques[10 + n] = "Legato";
  }
  ExpressionMap::Dimension mute;
  mute.name = "Mute"; mute.ccNumber = 103; mute.defaultValues = {0};
  mute.techniques = {{0, "Natural"}, {1, "Muted"}};
  ExpressionMap::Dimension custom;
  custom.name = "Custom"; custom.ccNumber = 104; custom.defaultValues = {0};
  custom.techniques = {{0, "Natural"}, {1, "Lyrical"}};
  std::vector<ExpressionMap::Dimension> decoder{articulation, mute, custom};
  ExpressionMapData map;
  map.entityID = "fixture"; map.name = "Fixture";
  map.combinations = {
    entry("Natural", {"pt.natural"}),
    entry("Short staccato", {"pt.staccato"}),
    entry("Long staccato", {"pt.staccato"}),
    entry("Shadowed staccato", {"pt.staccato"}),
    entry("Impossible pair", {"pt.staccato", "pt.legato"}),
    entry("Missing custom identity", {"pt.user.lyrical"}),
    entry("Mute add-on", {"pt.muted"}, true),
    entry("Unsupported condition", {"pt.legato"}),
  };
  map.combinations[1].conditionString = "NoteLength < kMedium";
  map.combinations[1].condition = parseConditionString(map.combinations[1].conditionString);
  map.combinations[2].conditionString = "NoteLength >= kMedium";
  map.combinations[2].condition = parseConditionString(map.combinations[2].conditionString);
  map.combinations[7].conditionString = "Velocity > 90";
  BridgeReachability::Assignment a{"chair.1", "Violin 1", "layer.1", "First layer", map};
  auto b = a; b.layerId = "layer.2"; b.layerName = "Second layer";
  b.map.entityID = "other"; b.map.combinations = {entry("Harmonic", {"pt.harmonic"})};
  auto unassigned = a; unassigned.chairId = "chair.2"; unassigned.assigned = false;
  const auto result = BridgeReachability::audit(decoder, {a, b, unassigned});
  const auto chair = result["chairs"][0];
  const auto rows = chair["layers"][0]["rows"];
  CHECK(bool(result["decoderAvailable"]));
  CHECK(result["chairs"].size() == 2);
  CHECK(chair["layers"].size() == 2);
  CHECK(chair["profile"].toString() == "builtin-universal");
  CHECK(chair["required"].getArray()->contains(juce::String("pt.harmonic")));
  CHECK(rows.size() == 8);
  CHECK(rows[0]["status"].toString() == "witness"); // Natural via reset/fallback
  CHECK(rows[0]["witness"]["techniques"].size() == 0);
  CHECK(rows[1]["status"].toString() == "witness");
  CHECK(int(rows[1]["witness"]["length"]) < 3);
  CHECK(rows[2]["status"].toString() == "witness");
  CHECK(int(rows[2]["witness"]["length"]) >= 3);
  CHECK(rows[3]["status"].toString() == "selection");
  CHECK(rows[4]["status"].toString() == "combination");
  CHECK(rows[5]["status"].toString() == "missing");
  CHECK(rows[5]["missing"][0].toString() == "pt.user.lyrical");
  CHECK(rows[6]["status"].toString() == "witness");
  CHECK(rows[6]["witness"]["cc"].size() == 3);
  CHECK(rows[7]["status"].toString() == "uncertain");
  CHECK(!bool(result["chairs"][1]["layers"][0]["assigned"]));

  // Witnesses replay through the production resolver with the shared legacy
  // normalization and runtime default suppression, not an audit-only matcher.
  for (const auto &row : *rows.getArray()) {
    if (row["status"].toString() != "witness") continue;
    std::set<std::string> decoded;
    for (const auto &cc : *row["witness"]["cc"].getArray()) {
      for (const auto &dim : decoder) if (dim.ccNumber == int(cc["cc"])) {
        const int value = cc["value"];
        if (std::find(dim.defaultValues.begin(), dim.defaultValues.end(), value) == dim.defaultValues.end())
          decoded.insert(techniqueIdFromDisplay(dim.techniques.at(value).toStdString()));
      }
    }
    const auto match = map.resolveMatch(decoded, static_cast<LengthCategory>(int(row["witness"]["length"])));
    const auto *target = &map.combinations[int(row["index"])];
    CHECK(match.base == target || std::find(match.addOns.begin(), match.addOns.end(), target) != match.addOns.end());
  }
  // Never label unsupported/ambiguous decoder layouts as proven gaps.
  decoder.push_back(mute);
  const auto unsupported = BridgeReachability::audit(decoder, {a});
  CHECK(!bool(unsupported["decoderAvailable"]));
  CHECK(unsupported["chairs"][0]["layers"][0]["rows"][1]["status"].toString() == "uncertain");
  CHECK(BridgeReachability::audit({}, {a})["chairs"][0]["layers"][0]["rows"][0]["status"].toString() == "uncertain");
  const auto cancelled = BridgeReachability::audit(decoder, {a}, [] { return true; });
  CHECK(bool(cancelled["cancelled"]));
  CHECK(cancelled["chairs"].size() == 0);
  CHECK(techniqueIdFromDisplay("Lyrical") == "pt.lyrical");
  CHECK(techniqueIdFromDisplay("pt.user.Lyrical") == "pt.user.Lyrical");
  // The runtime's permissive condition parser must not yield a false success.
  a.map.combinations = {entry("Unrecognized length", {"pt.staccato"})};
  a.map.combinations[0].conditionString = "NoteLength > kUnknown";
  a.map.combinations[0].condition = parseConditionString(a.map.combinations[0].conditionString);
  CHECK(BridgeReachability::audit({articulation, mute}, {a})["chairs"][0]["layers"][0]["rows"][0]["status"].toString() == "uncertain");
  // An exponential state space must terminate with uncertainty, not a claim
  // that the later duplicate is universally unreachable.
  std::vector<ExpressionMap::Dimension> manyAxes;
  for (int cc = 102; cc <= 119; ++cc) {
    ExpressionMap::Dimension dim;
    dim.name = juce::String(cc); dim.ccNumber = cc;
    dim.defaultValues = {0}; dim.techniques = {{0, "Natural"}, {5, "A"}};
    manyAxes.push_back(dim);
  }
  a.map.combinations = {entry("First", {"pt.a"}), entry("Duplicate", {"pt.a"})};
  CHECK(BridgeReachability::audit(manyAxes, {a})["chairs"][0]["layers"][0]["rows"][1]["status"].toString() == "uncertain");
  std::cout << (failures ? "FAIL" : "PASS") << ": bridge reachability (" << failures << " failures)\n";
  return failures ? 1 : 0;
}
