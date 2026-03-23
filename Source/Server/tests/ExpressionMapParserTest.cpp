#include "../ExpressionMapParser.h"
#include "../ExpressionMapData.h"

#include <iostream>
#include <juce_core/juce_core.h>
#include <string>

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

int gPass = 0, gFail = 0;

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (expr) {                                                                \
      ++gPass;                                                                 \
    } else {                                                                   \
      ++gFail;                                                                 \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #expr      \
                << std::endl;                                                  \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    if ((a) == (b)) {                                                          \
      ++gPass;                                                                 \
    } else {                                                                   \
      ++gFail;                                                                 \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__                     \
                << "]: " #a " == " #b " (got " << (a) << " vs " << (b) << ")"  \
                << std::endl;                                                  \
    }                                                                          \
  } while (0)

using namespace fiddle;

// ---------------------------------------------------------------------------
// Helper: find a combination by name
// ---------------------------------------------------------------------------
static const TechniqueCombination *findByName(const ExpressionMapData &data,
                                              const std::string &name) {
  for (auto &c : data.combinations) {
    if (c.name == name)
      return &c;
  }
  return nullptr;
}

/// Find a combination whose techniqueIDs match exactly.
static const TechniqueCombination *
findByTechniques(const ExpressionMapData &data,
                 const std::set<std::string> &techs) {
  auto matches = data.findExact(techs);
  return matches.empty() ? nullptr : matches[0];
}

/// Check if any action of the given type exists in switchOnActions.
static bool hasActionType(const TechniqueCombination &tc,
                          SwitchActionType type) {
  for (auto &a : tc.switchOnActions) {
    if (a.type == type)
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void testParseSucceeds(const juce::File &dir) {
  std::cout << "--- testParseSucceeds ---" << std::endl;
  auto files = dir.findChildFiles(juce::File::findFiles, false, "*.doricolib");
  CHECK(files.size() > 0);

  for (auto &f : files) {
    ExpressionMapData data;
    bool ok = parseExpressionMap(f, data);
    if (!ok) {
      ++gFail;
      std::cerr << "FAIL: could not parse " << f.getFileName() << std::endl;
    } else {
      ++gPass;
      std::cout << "  OK: " << f.getFileName() << " -> " << data.name << " ("
                << data.combinations.size() << " combinations)" << std::endl;
    }
  }
}

static void testPiccoloFlute(const juce::File &dir) {
  std::cout << "--- testPiccoloFlute (VSYSW 01) ---" << std::endl;
  auto file = dir.getChildFile("VSYSW 01 Piccolo Flute sus.doricolib");
  if (!file.existsAsFile()) {
    std::cout << "  SKIP: file not found" << std::endl;
    return;
  }

  ExpressionMapData data;
  CHECK(parseExpressionMap(file, data));
  CHECK_EQ(data.name, std::string("VSYSW 01 Piccolo Flute sus"));
  CHECK(!data.combinations.empty());

  // Should have a "Natural [Long]" entry with pt.natural
  auto *natural = findByTechniques(data, {"pt.natural"});
  if (natural) {
    ++gPass;
    // VSL uses kNoteVelocity actions
    CHECK(hasActionType(*natural, SwitchActionType::NoteVelocity));
    CHECK_EQ(natural->conditionString.empty(),
             false); // has NoteLength condition
  } else {
    // Some entries with pt.natural might have condition strings
    // Try finding by name
    auto *natByName = findByName(data, "Natural [Long]");
    if (natByName) {
      ++gPass;
      CHECK(hasActionType(*natByName, SwitchActionType::NoteVelocity));
    } else {
      ++gFail;
      std::cerr << "FAIL: no Natural entry found" << std::endl;
    }
  }
}

static void testHollywoodBrass(const juce::File &dir) {
  std::cout << "--- testHollywoodBrass (EWQL) ---" << std::endl;
  auto file =
      dir.getChildFile("EWQL - Hollywood Brass - Expression Maps.doricolib");
  if (!file.existsAsFile()) {
    std::cout << "  SKIP: file not found" << std::endl;
    return;
  }

  ExpressionMapData data;
  CHECK(parseExpressionMap(file, data));
  CHECK(!data.combinations.empty());

  // EWQL uses kKeySwitch actions — verify at least one exists
  bool hasKeySwitch = false;
  for (auto &c : data.combinations) {
    if (hasActionType(c, SwitchActionType::KeySwitch)) {
      hasKeySwitch = true;
      break;
    }
  }
  CHECK(hasKeySwitch);

  std::cout << "  kKeySwitch actions found: " << hasKeySwitch << std::endl;
}

static void testIconicaSections(const juce::File &dir) {
  std::cout << "--- testIconicaSections ---" << std::endl;
  auto file = dir.getChildFile("Iconica Sections Strings.doricolib");
  if (!file.existsAsFile()) {
    std::cout << "  SKIP: file not found" << std::endl;
    return;
  }

  ExpressionMapData data;
  CHECK(parseExpressionMap(file, data));

  // Iconica uses kChannelSwitch — verify
  bool hasChannelSwitch = false;
  for (auto &c : data.combinations) {
    if (hasActionType(c, SwitchActionType::ChannelSwitch)) {
      hasChannelSwitch = true;
      break;
    }
  }
  CHECK(hasChannelSwitch);

  std::cout << "  kChannelSwitch actions found: " << hasChannelSwitch
            << std::endl;
}

static void testBerlinWoodwinds(const juce::File &dir) {
  std::cout << "--- testBerlinWoodwinds ---" << std::endl;
  auto file = dir.getChildFile("Berlin Woodwinds SINE English Horn.doricolib");
  if (!file.existsAsFile()) {
    std::cout << "  SKIP: file not found" << std::endl;
    return;
  }

  ExpressionMapData data;
  CHECK(parseExpressionMap(file, data));

  // Check for kCC actions
  bool hasCC = false;
  for (auto &c : data.combinations) {
    if (hasActionType(c, SwitchActionType::CC) ||
        hasActionType(c, SwitchActionType::ControlChange)) {
      hasCC = true;
      break;
    }
  }
  CHECK(hasCC);

  std::cout << "  kCC/kControlChange actions found: " << hasCC << std::endl;
}

static void testVSLDimensionStrings(const juce::File &dir) {
  std::cout << "--- testVSLDimensionStrings ---" << std::endl;
  auto file = dir.getChildFile(
      "VSL Dimension Strings- Violins simplified with NL.doricolib");
  if (!file.existsAsFile()) {
    std::cout << "  SKIP: file not found" << std::endl;
    return;
  }

  ExpressionMapData data;
  CHECK(parseExpressionMap(file, data));

  // Check comma-separated technique IDs are parsed correctly
  // Look for an entry with multiple techniques
  bool foundMultiTechnique = false;
  for (auto &c : data.combinations) {
    if (c.techniqueIDs.size() > 1) {
      foundMultiTechnique = true;
      std::cout << "  Multi-technique: " << c.name << " (";
      for (auto &t : c.techniqueIDs)
        std::cout << t << " ";
      std::cout << ")" << std::endl;
      break;
    }
  }
  CHECK(foundMultiTechnique);
}

static void testBWWAClarinet(const juce::File &dir) {
  std::cout << "--- testBWWAClarinet ---" << std::endl;
  auto file = dir.getChildFile("BWWA Eb Clarinet.doricolib");
  if (!file.existsAsFile()) {
    std::cout << "  SKIP: file not found" << std::endl;
    return;
  }

  ExpressionMapData data;
  CHECK(parseExpressionMap(file, data));
  CHECK(!data.combinations.empty());

  std::cout << "  " << data.combinations.size()
            << " combinations, creator: " << data.creator << std::endl;
}

static void testTechniqueIDParsing() {
  std::cout << "--- testTechniqueIDParsing (inline XML) ---" << std::endl;

  // Test with "+" delimiter
  juce::String xmlPlus = R"(<?xml version="1.0" encoding="utf-8"?>
<kScoreLibrary>
  <expressionMapDefinitions>
    <entities array="true">
      <ExpressionMapDefinition>
        <name>Test Plus</name>
        <entityID>test.plus</entityID>
        <playingTechniqueCombinations array="true">
          <playingTechniqueCombination>
            <name>Legato Marcato</name>
            <baseSwitchID>1</baseSwitchID>
            <techniqueIDs>pt.legato+pt.marcato</techniqueIDs>
            <switchOnActions array="true">
              <switchOnAction>
                <type>kKeySwitch</type>
                <param1>24</param1>
                <param2>127</param2>
              </switchOnAction>
            </switchOnActions>
            <switchOffActions array="true"/>
            <ticksBefore>10</ticksBefore>
            <millisecondsBefore>50</millisecondsBefore>
            <velocityFactor>0.800000</velocityFactor>
            <lengthFactor>1.200000</lengthFactor>
            <monophonic>true</monophonic>
            <conditionString>NoteLength &gt; kShort</conditionString>
            <velocityRange>10,120</velocityRange>
            <pitchRange>24,96</pitchRange>
            <transpose>-12</transpose>
          </playingTechniqueCombination>
        </playingTechniqueCombinations>
      </ExpressionMapDefinition>
    </entities>
  </expressionMapDefinitions>
</kScoreLibrary>)";

  ExpressionMapData dataPlus;
  CHECK(parseExpressionMapXml(xmlPlus, dataPlus));
  CHECK_EQ(dataPlus.name, std::string("Test Plus"));
  CHECK_EQ((int)dataPlus.combinations.size(), 1);

  auto &c = dataPlus.combinations[0];
  CHECK_EQ(c.name, std::string("Legato Marcato"));
  CHECK_EQ((int)c.techniqueIDs.size(), 2);
  CHECK(c.techniqueIDs.count("pt.legato") == 1);
  CHECK(c.techniqueIDs.count("pt.marcato") == 1);
  CHECK_EQ((int)c.switchOnActions.size(), 1);
  CHECK_EQ((int)c.switchOnActions[0].type, (int)SwitchActionType::KeySwitch);
  CHECK_EQ(c.switchOnActions[0].param1, 24);
  CHECK_EQ(c.switchOnActions[0].param2, 127);
  CHECK_EQ(c.ticksBefore, 10);
  CHECK_EQ(c.millisecondsBefore, 50);
  CHECK(std::abs(c.velocityFactor - 0.8f) < 0.01f);
  CHECK(std::abs(c.lengthFactor - 1.2f) < 0.01f);
  CHECK_EQ(c.monophonic, true);
  CHECK_EQ(c.conditionString, std::string("NoteLength > kShort"));
  CHECK_EQ(c.velocityMin, 10);
  CHECK_EQ(c.velocityMax, 120);
  CHECK_EQ(c.pitchMin, 24);
  CHECK_EQ(c.pitchMax, 96);
  CHECK_EQ(c.transpose, -12);

  // Test with ", " delimiter
  juce::String xmlComma = R"(<?xml version="1.0" encoding="utf-8"?>
<kScoreLibrary>
  <expressionMapDefinitions>
    <entities array="true">
      <ExpressionMapDefinition>
        <name>Test Comma</name>
        <entityID>test.comma</entityID>
        <playingTechniqueCombinations array="true">
          <playingTechniqueCombination>
            <name>Multi Tech</name>
            <baseSwitchID>1</baseSwitchID>
            <techniqueIDs>pt.detache, pt.legato, pt.staccato</techniqueIDs>
            <switchOnActions array="true">
              <switchOnAction>
                <type>kCC</type>
                <param1>102</param1>
                <param2>5</param2>
              </switchOnAction>
              <switchOnAction>
                <type>kKeySwitch</type>
                <param1>36</param1>
                <param2>100</param2>
              </switchOnAction>
            </switchOnActions>
            <switchOffActions array="true"/>
          </playingTechniqueCombination>
        </playingTechniqueCombinations>
      </ExpressionMapDefinition>
    </entities>
  </expressionMapDefinitions>
</kScoreLibrary>)";

  ExpressionMapData dataComma;
  CHECK(parseExpressionMapXml(xmlComma, dataComma));
  CHECK_EQ((int)dataComma.combinations.size(), 1);

  auto &c2 = dataComma.combinations[0];
  CHECK_EQ((int)c2.techniqueIDs.size(), 3);
  CHECK(c2.techniqueIDs.count("pt.detache") == 1);
  CHECK(c2.techniqueIDs.count("pt.legato") == 1);
  CHECK(c2.techniqueIDs.count("pt.staccato") == 1);
  CHECK_EQ((int)c2.switchOnActions.size(), 2);
  CHECK_EQ((int)c2.switchOnActions[0].type, (int)SwitchActionType::CC);
  CHECK_EQ((int)c2.switchOnActions[1].type, (int)SwitchActionType::KeySwitch);
}

static void testFindBestMatch() {
  std::cout << "--- testFindBestMatch ---" << std::endl;

  ExpressionMapData data;
  data.combinations.push_back(TechniqueCombination{
      .name = "Natural", .techniqueIDs = {"pt.natural"}, .baseSwitchID = 1});
  data.combinations.push_back(TechniqueCombination{
      .name = "Legato", .techniqueIDs = {"pt.legato"}, .baseSwitchID = 2});
  data.combinations.push_back(
      TechniqueCombination{.name = "Legato+Marcato",
                           .techniqueIDs = {"pt.legato", "pt.marcato"},
                           .baseSwitchID = 3});

  // Exact match
  auto exact = data.findExact({"pt.legato", "pt.marcato"});
  CHECK_EQ((int)exact.size(), 1);
  CHECK_EQ(exact[0]->name, std::string("Legato+Marcato"));

  // Best match: looking for {legato, marcato, staccato} → Legato+Marcato (2/3)
  auto *best = data.findBestMatch({"pt.legato", "pt.marcato", "pt.staccato"});
  CHECK(best != nullptr);
  CHECK_EQ(best->name, std::string("Legato+Marcato"));

  // No match at all
  auto *none = data.findBestMatch({"pt.pizzicato"});
  CHECK(none == nullptr);
}

static void testActionTypeCoverage(const juce::File &dir) {
  std::cout << "--- testActionTypeCoverage ---" << std::endl;

  auto files = dir.findChildFiles(juce::File::findFiles, false, "*.doricolib");

  std::map<SwitchActionType, int> typeCounts;
  for (auto &f : files) {
    ExpressionMapData data;
    if (!parseExpressionMap(f, data))
      continue;
    for (auto &c : data.combinations) {
      for (auto &a : c.switchOnActions)
        typeCounts[a.type]++;
    }
  }

  // Verify we see the expected types
  CHECK(typeCounts[SwitchActionType::KeySwitch] > 0);
  CHECK(typeCounts[SwitchActionType::NoteVelocity] > 0);
  CHECK(typeCounts[SwitchActionType::CC] > 0 ||
        typeCounts[SwitchActionType::ControlChange] > 0);

  for (auto &[type, count] : typeCounts) {
    const char *name;
    switch (type) {
    case SwitchActionType::KeySwitch:
      name = "KeySwitch";
      break;
    case SwitchActionType::NoteVelocity:
      name = "NoteVelocity";
      break;
    case SwitchActionType::CC:
      name = "CC";
      break;
    case SwitchActionType::ControlChange:
      name = "ControlChange";
      break;
    case SwitchActionType::ChannelSwitch:
      name = "ChannelSwitch";
      break;
    case SwitchActionType::ProgramChange:
      name = "ProgramChange";
      break;
    default:
      name = "Unknown";
      break;
    }
    std::cout << "  " << name << ": " << count << std::endl;
  }
}

// ---------------------------------------------------------------------------
// Test: playbackOptionsOverrides → timingOptions
// ---------------------------------------------------------------------------

static void testVSLDualityTimingOptions(const juce::File &examplesDir) {
  std::cout << "--- testVSLDualityTimingOptions ---" << std::endl;
  auto file = examplesDir.getChildFile(
      "VSL SY Duality Strings - Violins, Violas.doricolib");
  if (!file.existsAsFile()) {
    std::cout << "  SKIP: VSL Duality file not found" << std::endl;
    return;
  }
  ExpressionMapData data;
  CHECK(parseExpressionMap(file, data));

  CHECK_EQ(data.timingOptions.noteDurationPercent, 85);
  CHECK_EQ(data.timingOptions.staccatoDurationPercent, 80);
  CHECK_EQ(data.timingOptions.staccatissimoDurationPercent, 80);
  CHECK_EQ(data.timingOptions.legatoDurationPercent, 101);
}

// ---------------------------------------------------------------------------
// Forward declaration for annotator tests
// ---------------------------------------------------------------------------
extern void runAnnotatorTests();

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  // Default to project's example-expression-maps directory
  juce::File examplesDir;
  if (argc > 1) {
    examplesDir = juce::File(argv[1]);
  } else {
    examplesDir = juce::File::getCurrentWorkingDirectory().getChildFile(
        "example-expression-maps");
  }

  if (!examplesDir.isDirectory()) {
    std::cerr << "ERROR: examples directory not found: "
              << examplesDir.getFullPathName() << std::endl;
    std::cerr << "Usage: ExpressionMapTest [path/to/example-expression-maps]"
              << std::endl;
    return 1;
  }

  std::cout << "Examples dir: " << examplesDir.getFullPathName() << std::endl;
  std::cout << std::endl;

  // Run parser tests
  testTechniqueIDParsing();
  testFindBestMatch();
  testParseSucceeds(examplesDir);
  testActionTypeCoverage(examplesDir);
  testPiccoloFlute(examplesDir);
  testHollywoodBrass(examplesDir);
  testIconicaSections(examplesDir);
  testBerlinWoodwinds(examplesDir);
  testVSLDimensionStrings(examplesDir);
  testBWWAClarinet(examplesDir);
  testVSLDualityTimingOptions(examplesDir);

  // Run annotator tests
  runAnnotatorTests();

  // Summary
  std::cout << std::endl;
  std::cout << "============================" << std::endl;
  std::cout << "PASS: " << gPass << "  FAIL: " << gFail << std::endl;
  std::cout << "============================" << std::endl;

  return gFail > 0 ? 1 : 0;
}

