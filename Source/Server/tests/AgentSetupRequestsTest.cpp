// Request logic behind the MCP setup tools (AgentSetupRequests.h): Dorico
// instrument search, kit chair names, and merging a library.save request into
// a stored library.
#include "../AgentSetupRequests.h"

#include <iostream>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition)                                                             \
      ++passed;                                                                \
    else {                                                                     \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__                     \
                << "]: " #condition << std::endl;                              \
    }                                                                          \
  } while (false)

using namespace fiddle;

BrowsableInstrument instrument(const char *name, const char *entity, const char *family,
                               bool isDefault = true) {
  BrowsableInstrument result;
  result.name = name;
  result.entityID = entity;
  result.family = family;
  result.isDefault = isDefault;
  return result;
}

juce::var object(std::initializer_list<std::pair<const char *, juce::var>> properties) {
  auto *result = new juce::DynamicObject();
  for (const auto &[name, value] : properties) result->setProperty(name, value);
  return juce::var(result);
}

juce::var array(std::initializer_list<juce::var> values) {
  juce::Array<juce::var> result;
  for (const auto &value : values) result.add(value);
  return juce::var(result);
}

agent::LibraryEditChecks checks() {
  agent::LibraryEditChecks result;
  result.instrumentExists = [](const std::string &id) { return id == "instrument.strings.violin" || id == "instrument.wind.flutes.flute"; };
  result.instrumentPluginAvailable = [](int uid) { return uid == 11 || uid == 22; };
  result.mapAvailable = [](LibraryMapKind kind, const std::string &id, const juce::String &path) {
    if (kind == LibraryMapKind::percussion) return id == "perc.kit";
    if (path.isNotEmpty()) return path == "/maps/new.doricolib" && id == "map.new";
    return id == "map.known";
  };
  result.percussionMapInstruments = [](const std::string &id, const juce::String &) {
    return id == "perc.kit" ? std::optional<std::vector<std::string>>({"snare", "kick"}) : std::nullopt;
  };
  result.patchUsageCount = [](const std::string &id) { return id == "used" ? 2 : 0; };
  return result;
}

std::function<std::string()> ids() {
  auto next = std::make_shared<int>(0);
  return [next] { return "id-" + std::to_string(++*next); };
}

LibraryCatalogSnapshot stored() {
  LibraryCatalogSnapshot library;
  library.id = "lib";
  library.name = "Strings";
  library.vendor = "Vendor";
  library.exists = true;
  LibraryPatchRow violin;
  violin.id = "violin";
  violin.libraryId = "lib";
  violin.name = "Violin";
  violin.pluginUid = 11;
  violin.pluginState = {1, 2, 3};
  violin.expressionMapId = "map.known";
  violin.expectedPresetName = "Violins";
  violin.setupComplete = true;
  LibraryPatchRow used = violin;
  used.id = "used";
  used.name = "Used";
  library.patches = {violin, used};
  return library;
}

void testSearch() {
  std::vector<BrowsableInstrument> instruments{
      instrument("Violin", "instrument.strings.violin", "strings"),
      instrument("Violin (Baroque)", "instrument.strings.violin.baroque", "strings", false),
      instrument("Viola", "instrument.strings.viola", "strings"),
      instrument("Electric Violin", "instrument.strings.violin.electric", "strings"),
      instrument("Flute", "instrument.wind.flutes.flute", "wind"),
  };
  auto results = agent::searchInstruments(instruments, "violin", "", false, 10);
  CHECK(results.size() == 2);
  CHECK(results[0]->name == "Violin");
  CHECK(results[1]->name == "Electric Violin");
  results = agent::searchInstruments(instruments, "violin", "", true, 10);
  CHECK(results.size() == 3);
  CHECK(results[1]->name == "Violin (Baroque)"); // a prefix match outranks a later word
  results = agent::searchInstruments(instruments, "flutes flute", "", false, 10);
  CHECK(results.size() == 1 && results[0]->name == "Flute");
  results = agent::searchInstruments(instruments, "", "wind", false, 10);
  CHECK(results.size() == 1 && results[0]->name == "Flute");
  CHECK(agent::searchInstruments(instruments, "", "", true, 2).size() == 2);
  CHECK(agent::suggestedRole(instruments[0]) == "section");
  CHECK(agent::suggestedRole(instruments[4]) == "solo");
}

void testMatching() {
  CHECK(agent::matchesSearch("Hi-hat", "hi"));
  CHECK(!agent::matchesSearch("China (Opera) Gongs", "hi"));
  CHECK(agent::matchesSearch("VSL Hi-Hat", "hihat"));
  CHECK(agent::matchesSearch("Violoncello instrument.strings.violoncello", "cello"));
  CHECK(agent::matchesSearch("AC Syn Brass — Horn 1", "horn brass"));
  CHECK(!agent::matchesSearch("AC Syn Brass — Horn 1", "horn flute"));
  CHECK(agent::matchesSearch("anything", ""));
  CHECK(agent::matchesSearch("instrument.wind.flutes.flute", "flutes"));
  CHECK(!agent::matchesSearch("Trumpet", "rum")); // short words must start a word
}

void testCoversInstrument() {
  const std::vector<std::string> listed{"instrument.band.unpitched.tomtom.high.alias.tom-high",
                                        "instrument.drumset.snare"};
  CHECK(agent::coversInstrument(listed, "instrument.band.unpitched.tomtom.high"));
  CHECK(agent::coversInstrument(listed, "instrument.band.unpitched.tomtom.high.alias.other"));
  CHECK(agent::coversInstrument(listed, "instrument.drumset.snare"));
  CHECK(!agent::coversInstrument(listed, "instrument.band.unpitched.tomtom.low"));
  CHECK(!agent::coversInstrument(listed, "instrument.drumset"));
}

void testKitNames() {
  const auto names = agent::kitChairNames({"Snare", "Wood Block", "Wood Block", "Kick"});
  CHECK(names[0] == "Snare");
  CHECK(names[1] == "Wood Block 1");
  CHECK(names[2] == "Wood Block 2");
  CHECK(names[3] == "Kick");
}

void testCreateLibrary() {
  const auto params = object({
      {"name", "Winds"}, {"vendor", "V"}, {"pluginUid", 22},
      {"patches", array({
          object({{"name", "Flute"}, {"instrumentEntityId", "instrument.wind.flutes.flute"},
                  {"expressionMapId", "map.new"}, {"expressionMapPath", "/maps/new.doricolib"},
                  {"expectedPresetName", "Flute Legato"}}),
          object({{"name", "Flute plain"}, {"pluginUid", 11}}),
      })},
  });
  const auto edit = agent::mergeLibraryEdit(std::nullopt, params, checks(), ids());
  CHECK(edit.ok);
  CHECK(edit.created);
  CHECK(edit.target.id == "id-1");
  CHECK(edit.target.name == "Winds");
  CHECK(edit.target.patches.size() == 2);
  CHECK(edit.target.patches[0].pluginUid == 22);
  CHECK(!edit.target.patches[0].setupComplete); // waits for the user's preset capture
  CHECK(edit.target.patches[1].pluginUid == 11);
  CHECK(edit.target.patches[1].setupComplete); // no expected preset: nothing to capture
  CHECK(edit.target.patches[1].expressionMapId.empty()); // no map is fine
  CHECK(edit.target.patches[1].position == 1);
  CHECK(edit.target.patches[1].libraryId == "id-1");
  CHECK(edit.imports.size() == 1 && edit.imports[0].entityId == "map.new");
  CHECK(edit.addedPatchIds.size() == 2);

  CHECK(!agent::mergeLibraryEdit(std::nullopt, object({{"name", "X"}}), checks(), ids()).ok);
  CHECK(!agent::mergeLibraryEdit(std::nullopt,
      object({{"patches", array({object({{"name", "A"}, {"pluginUid", 11}})})}}), checks(), ids()).ok);
  // Without a player for the patch or the library.
  CHECK(!agent::mergeLibraryEdit(std::nullopt,
      object({{"name", "X"}, {"patches", array({object({{"name", "A"}})})}}), checks(), ids()).ok);
  // Not an installed instrument plug-in.
  CHECK(!agent::mergeLibraryEdit(std::nullopt,
      object({{"name", "X"}, {"patches", array({object({{"name", "A"}, {"pluginUid", 99}})})}}), checks(), ids()).ok);
  // A map neither in Fiddle nor given a file.
  const auto missingMap = agent::mergeLibraryEdit(std::nullopt,
      object({{"name", "X"}, {"pluginUid", 11},
              {"patches", array({object({{"name", "A"}, {"expressionMapId", "map.other"}})})}}),
      checks(), ids());
  CHECK(!missingMap.ok && missingMap.error.contains("not in Fiddle"));
  // An unknown instrument.
  CHECK(!agent::mergeLibraryEdit(std::nullopt,
      object({{"name", "X"}, {"pluginUid", 11},
              {"patches", array({object({{"name", "A"}, {"instrumentEntityId", "instrument.nope"}})})}}),
      checks(), ids()).ok);
  // An ID that doesn't exist yet is not a way to create a library.
  CHECK(!agent::mergeLibraryEdit(std::nullopt, object({{"libraryId", "nope"}, {"name", "X"}}), checks(), ids()).ok);
}

void testUpdateLibrary() {
  // Partial edits keep what they don't mention, including captured state.
  auto edit = agent::mergeLibraryEdit(stored(),
      object({{"libraryId", "lib"}, {"variant", "Pro"},
              {"patches", array({object({{"id", "violin"}, {"family", "strings"}})})}}),
      checks(), ids());
  CHECK(edit.ok);
  CHECK(!edit.created);
  CHECK(edit.target.name == "Strings" && edit.target.vendor == "Vendor" && edit.target.variant == "Pro");
  CHECK(edit.target.patches[0].family == "strings");
  CHECK(edit.target.patches[0].pluginState.size() == 3);
  CHECK(edit.target.patches[0].setupComplete);
  CHECK(edit.target.patches[0].expressionMapId == "map.known");

  // A new expected preset needs the user again; an empty string clears a map.
  edit = agent::mergeLibraryEdit(stored(),
      object({{"libraryId", "lib"},
              {"patches", array({object({{"id", "violin"}, {"expectedPresetName", "Violins Pro"},
                                         {"expressionMapId", ""}})})}}),
      checks(), ids());
  CHECK(edit.ok);
  CHECK(!edit.target.patches[0].setupComplete);
  CHECK(edit.target.patches[0].pluginState.size() == 3); // kept until the user captures again
  CHECK(edit.target.patches[0].expressionMapId.empty());

  // A new player drops the old player's state.
  edit = agent::mergeLibraryEdit(stored(),
      object({{"libraryId", "lib"}, {"patches", array({object({{"id", "violin"}, {"pluginUid", 22}})})}}),
      checks(), ids());
  CHECK(edit.ok);
  CHECK(edit.target.patches[0].pluginUid == 22);
  CHECK(edit.target.patches[0].pluginState.empty());
  CHECK(!edit.target.patches[0].setupComplete);

  // Removing and adding in one step; new patches go last.
  edit = agent::mergeLibraryEdit(stored(),
      object({{"libraryId", "lib"}, {"removePatchIds", array({"violin"})},
              {"patches", array({object({{"name", "Viola"}, {"pluginUid", 11}})})}}),
      checks(), ids());
  CHECK(edit.ok);
  CHECK(edit.target.patches.size() == 2);
  CHECK(edit.target.patches[0].id == "used" && edit.target.patches[0].position == 0);
  CHECK(edit.target.patches[1].name == "Viola" && edit.target.patches[1].position == 1);

  // Patches that layers use can't be removed.
  edit = agent::mergeLibraryEdit(stored(),
      object({{"libraryId", "lib"}, {"removePatchIds", array({"used"})}}), checks(), ids());
  CHECK(!edit.ok && edit.error.contains("layers use"));
  CHECK(!agent::mergeLibraryEdit(stored(),
      object({{"libraryId", "lib"}, {"removePatchIds", array({"nope"})}}), checks(), ids()).ok);
  CHECK(!agent::mergeLibraryEdit(stored(),
      object({{"libraryId", "lib"}, {"patches", array({object({{"id", "nope"}})})}}), checks(), ids()).ok);
  CHECK(!agent::mergeLibraryEdit(stored(),
      object({{"libraryId", "lib"}, {"patches", array({object({{"id", "violin"}}), object({{"id", "violin"}})})}}),
      checks(), ids()).ok);
}

void testPercussion() {
  auto edit = agent::mergeLibraryEdit(stored(),
      object({{"libraryId", "lib"},
              {"patches", array({object({{"id", "violin"}, {"percussionMapId", "perc.kit"},
                                         {"percussionPlayAs", object({{"instrument.drums.snare", "snare"}})}})})}}),
      checks(), ids());
  CHECK(edit.ok);
  CHECK(edit.target.patches[0].percussionMapId == "perc.kit");
  CHECK(edit.target.patches[0].percussionPlayAs.at("instrument.drums.snare") == "snare");
  // Play as must name one of the map's instruments.
  edit = agent::mergeLibraryEdit(stored(),
      object({{"libraryId", "lib"},
              {"patches", array({object({{"id", "violin"}, {"percussionMapId", "perc.kit"},
                                         {"percussionPlayAs", object({{"instrument.drums.snare", "cowbell"}})}})})}}),
      checks(), ids());
  CHECK(!edit.ok && edit.error.contains("cowbell"));
  // Play as without a percussion map.
  CHECK(!agent::mergeLibraryEdit(stored(),
      object({{"libraryId", "lib"},
              {"patches", array({object({{"id", "violin"}, {"percussionPlayAs", object({{"a", "snare"}})}})})}}),
      checks(), ids()).ok);
  // A path without the map's ID.
  CHECK(!agent::mergeLibraryEdit(stored(),
      object({{"libraryId", "lib"},
              {"patches", array({object({{"id", "violin"}, {"expressionMapPath", "/maps/new.doricolib"}})})}}),
      checks(), ids()).ok);
}

} // namespace

int main() {
  testSearch();
  testMatching();
  testCoversInstrument();
  testKitNames();
  testCreateLibrary();
  testUpdateLibrary();
  testPercussion();
  std::cout << passed << " passed, " << failed << " failed" << std::endl;
  return failed == 0 ? 0 : 1;
}
