#include "../TechniqueCatalog.h"
#include <iostream>

using namespace fiddle;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; } } while(false)
juce::var technique(const juce::var &report, const juce::String &id) {
  for (const auto &t : *report["techniques"].getArray()) if (t["id"].toString() == id) return t;
  return {};
}
juce::String definition(const juce::String &type = "kAttribute") {
  return "<PlayingTechniqueDefinition><entityID>pt.user.lyrical</entityID><name>Lyrical</name>"
      "<articulationType>" + type + "</articulationType><fallbackPlayingTechniqueID>pt.natural</fallbackPlayingTechniqueID>"
      "<mutualExclusionGroups>g.two, g.one</mutualExclusionGroups></PlayingTechniqueDefinition>";
}
juce::String map(const juce::String &id, const juce::String &name) {
  return "<ExpressionMapDefinition><entityID>" + id + "</entityID><name>" + name + "</name><creator>Fixture</creator>"
      "<playingTechniqueCombinations><playingTechniqueCombination><techniqueIDs>pt.user.lyrical, pt.missing</techniqueIDs>"
      "<switchOnActions><switchOnAction><type>kControlChange</type><param1>102</param1><param2>5</param2></switchOnAction></switchOnActions>"
      "</playingTechniqueCombination></playingTechniqueCombinations><mutualExclusionGroups><mutualExclusionGroup><groupID>g.test</groupID>"
      "<name>Test</name><techniqueIDs>pt.user.lyrical, pt.onlyGroup</techniqueIDs></mutualExclusionGroup></mutualExclusionGroups></ExpressionMapDefinition>";
}
TechniqueCatalog::Source fixture(const juce::String &file, const juce::String &xml, const juce::String &kind = "installed") {
  return {juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(file), kind, "<kScoreLibrary>" + xml + "</kScoreLibrary>"};
}
}

int main(int argc, char **argv) {
  // Opt-in read-only audit of real files. Never invoked by CTest; vendor data
  // stays outside this repository. Useful for profiling large distributions.
  if (argc > 1) {
    TechniqueCatalog::Request request;
    const bool json = juce::String(argv[1]) == "--json";
    for (int i = json ? 2 : 1; i + 1 < argc; i += 2)
      request.sources.push_back({juce::File(argv[i + 1]), juce::String(argv[i]).fromFirstOccurrenceOf("--", false, false), {}});
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto report = TechniqueCatalog::scan(request);
    if (json) { std::cout << juce::JSON::toString(report, true); return 0; }
    std::cout << "files=" << int(report["files"]) << " techniques=" << report["techniques"].size()
              << " maps=" << report["maps"].size() << " declared=" << int(report["declaredCount"])
              << " missing=" << int(report["missingCount"]) << " variants=" << int(report["variantCount"])
              << " issues=" << report["issues"].size() << " elapsedMs=" << (juce::Time::getMillisecondCounterHiRes() - start)
              << " reportBytes=" << juce::JSON::toString(report, true).getNumBytesAsUTF8() << "\n";
    for (int i = 0; i < juce::jmin(5, report["issues"].size()); ++i)
      std::cout << juce::JSON::toString(report["issues"][i], true) << "\n";
    return 0;
  }
  TechniqueCatalog::Request request;
  const auto appearance = "<PlayingTechniqueAppearanceDefinition><entityID>pta.user.lyrical</entityID><name>Lyrical line</name>"
      "<text>lyrical</text><playingTechniqueEntityID>pt.user.lyrical</playingTechniqueEntityID><alwaysHasDuration>true</alwaysHasDuration>"
      "<durationLineData><lineStyleEntityID>line.hook</lineStyleEntityID></durationLineData></PlayingTechniqueAppearanceDefinition>";
  request.sources = {
      fixture("tech-defs.xml", definition() + appearance),
      fixture("vsl-map.doricolib", map("map.vsl", "Violin")),
      fixture("ac-map.doricolib", map("map.ac", "Violin")),
      fixture("copied-defs.xml", definition()),
      fixture("reference.xml", definition("kDirection"), "reference"),
      fixture("bridge.doricolib", map("map.fiddle", "Fiddle"), "bridge")};
  request.usage = {{"map.vsl", "Violin layer", "project", request.sources[1].path.getFullPathName()},
                   {"map.gone", "Missing patch", "library", {}}};
  const auto report = TechniqueCatalog::scan(request);
  const auto row = technique(report, "pt.user.lyrical");
  CHECK(row["definitions"].size() == 2); // identical copies deduplicated, provenance retained
  CHECK(row["definitions"][0]["sources"].size() == 2);
  CHECK(bool(row["variants"]));
  CHECK(!bool(row["localVariants"])); // optional reference cannot create a local conflict
  CHECK(bool(row["declared"]));
  CHECK(row["appearances"].size() == 1);
  CHECK(row["appearances"][0]["durationLine"].toString() == "line.hook");
  CHECK(row["definitions"][0]["fallback"].toString() == "pt.natural");
  CHECK(technique(report, "pt.natural")["definitionState"].toString() == "missing");
  CHECK(technique(report, "pt.missing")["definitionState"].toString() == "missing");
  CHECK(technique(report, "pt.onlyGroup").isVoid()); // exclusions are not switches
  CHECK(report["maps"].size() == 3); // same display name never merges alternative maps
  CHECK(report["maps"][0]["usage"].size() == 1);
  CHECK(report["maps"][1]["usage"].size() == 0);
  CHECK(report["unresolvedUsage"].size() == 1);
  CHECK(report["groups"].size() == 1); // repeated map groups normalized
  CHECK(report["issues"].size() == 0);

  request.sources.push_back(fixture("override.xml", definition("kDirection")));
  request.sources.push_back(fixture("same-name.xml", "<PlayingTechniqueDefinition><entityID>pt.other</entityID><name>Lyrical</name></PlayingTechniqueDefinition>"));
  request.sources.push_back(fixture("reference-only.xml", "<PlayingTechniqueDefinition><entityID>pt.reference</entityID></PlayingTechniqueDefinition>", "reference"));
  request.sources.push_back(fixture("bad.xml", "<broken>"));
  request.sources.push_back(request.sources[0]);
  const auto expanded = TechniqueCatalog::scan(request);
  CHECK(bool(technique(expanded, "pt.user.lyrical")["localVariants"]));
  CHECK(!technique(expanded, "pt.other").isVoid());
  CHECK(technique(expanded, "pt.reference")["definitionState"].toString() == "reference");
  CHECK(expanded["issues"].size() == 1);
  CHECK(int(expanded["files"]) == 10); // repeated source scanned once
  CHECK(bool(TechniqueCatalog::scan(request, [] { return true; })["cancelled"]));
  CHECK(TechniqueCatalog::scan(request, [] { return true; })["techniques"].size() == 0);

  // Source identity, not just map ID, determines which alternative is assigned.
  request.sources.push_back(fixture("duplicate-map-id.xml", map("map.vsl", "Different version"), "reference"));
  const auto alternatives = TechniqueCatalog::scan(request);
  const auto lastMap = alternatives["maps"][alternatives["maps"].size() - 1];
  CHECK(lastMap["usage"].size() == 0);
  TechniqueCatalog::Request wrapped;
  auto wrapper = fixture("wrapped.xml", "");
  wrapper.xml = "<kScoreLibraryAndOptions><kScoreLibrary>" + definition() + map("wrapped", "Wrapped")
      .replace("pt.user.lyrical, pt.missing", "pt.user.lyrical+pt.missing") + "</kScoreLibrary></kScoreLibraryAndOptions>";
  wrapped.sources.push_back(wrapper);
  const auto wrappedReport = TechniqueCatalog::scan(wrapped);
  CHECK(wrappedReport["maps"].size() == 1);
  CHECK(!technique(wrappedReport, "pt.user.lyrical").isVoid());
  CHECK(!technique(wrappedReport, "pt.missing").isVoid());
  CHECK(technique(wrappedReport, "pt.user.lyrical+pt.missing").isVoid());
  // Replanning uses the same immutable assignment/source snapshot; it must
  // not restart directory discovery or silently refresh vendor files.
  TechniqueCatalogScan worker;
  CHECK(worker.start(wrapped));
  for (int i = 0; i < 500 && worker.running(); ++i) juce::Thread::sleep(10);
  CHECK(!worker.running());
  const auto snapshot = worker.state()["report"];
  CHECK(snapshot["repairPlan"].isObject());
  CHECK(snapshot["chairCoverage"].isObject());
  BridgeRepairPlan::Options options; options.preserveIdentities = false;
  CHECK(worker.previewRepairs(options));
  for (int i = 0; i < 500 && worker.running(); ++i) juce::Thread::sleep(10);
  CHECK(!worker.running());
  const auto preview = worker.state()["report"];
  CHECK(preview["files"] == snapshot["files"]);
  CHECK(preview["scannedAt"] == snapshot["scannedAt"]);
  CHECK(preview["chairCoverage"] == snapshot["chairCoverage"]);
  CHECK(!bool(preview["repairPlan"]["preserveIdentities"]));
  std::cout << (failures ? "FAIL" : "PASS") << ": technique catalog fixtures (" << failures << " failures)\n";
  return failures ? 1 : 0;
}
