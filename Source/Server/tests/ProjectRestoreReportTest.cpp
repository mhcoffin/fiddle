// What the user is told when a Dorico project connects (ProjectRestoreReport.h).
#include "../ProjectEnvironment.h"
#include "../ProjectRestoreReport.h"

#include <algorithm>
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

juce::var report(RestoreOutcome outcome, juce::String savedEdits = {}, juce::String error = {}) {
  RestoreReportInput in;
  in.outcome = outcome;
  in.savedBranchName = "Main";
  in.branchName = "Strings test";
  in.savedEditsBranch = savedEdits;
  in.error = error;
  return buildRestoreReport(in);
}

void testOutcomes() {
  const auto exact = report(RestoreOutcome::exact);
  CHECK(exact["kind"] == "exact");
  CHECK(exact["severity"] == "info");
  CHECK(exact["action"].toString().isEmpty());
  CHECK(exact["message"].toString().contains("\"Strings test\""));

  // Anything that may not sound as saved is a warning.
  for (const auto outcome : {RestoreOutcome::newerOnBranch, RestoreOutcome::byBranchName,
                             RestoreOutcome::notFound, RestoreOutcome::notSwitched})
    CHECK(report(outcome)["severity"] == "warning");

  // A project whose setup isn't here names it and offers to use the current one.
  const auto missing = report(RestoreOutcome::notFound);
  CHECK(missing["action"] == "useCurrentSetup");
  CHECK(missing["message"].toString().contains("\"Main\""));
  CHECK(missing["message"].toString().contains("\"Strings test\""));
  CHECK(missing["message"].toString().endsWith("You can:"));
  // Three choices: use the current setup, pick another, or keep the link.
  const auto *choices = missing["options"].getArray();
  CHECK(choices != nullptr && choices->size() == 3);
  if (choices && choices->size() == 3) {
    CHECK((*choices)[0]["label"] == "Use current setup");
    CHECK((*choices)[0]["text"].toString().contains("\"Strings test\""));
    CHECK((*choices)[1]["label"] == "Choose another branch or version in Fiddle");
    CHECK((*choices)[1]["text"] == "the project uses that one instead.");
    CHECK((*choices)[2]["label"] == "Leave it");
    CHECK((*choices)[2]["text"].toString().contains("backup"));
  }

  const auto kept = report(RestoreOutcome::notSwitched, {}, "plug-ins are still loading");
  CHECK(kept["action"] == "retry");
  CHECK(kept["message"].toString().contains("plug-ins are still loading"));
  const auto *keptChoices = kept["options"].getArray();
  CHECK(keptChoices != nullptr && keptChoices->size() == 3);
  if (keptChoices && !keptChoices->isEmpty()) {
    CHECK((*keptChoices)[0]["label"] == "Load project setup");
    // Saving or undoing edits is advice only when the edits were the problem.
    CHECK(!(*keptChoices)[0]["text"].toString().contains("saved or undone"));
  }
  RestoreReportInput edits;
  edits.outcome = RestoreOutcome::notSwitched;
  edits.retryAfterEdits = true;
  CHECK((*buildRestoreReport(edits)["options"].getArray())[0]["text"].toString().contains("saved or undone"));

  // The log line carries the choices too.
  const auto line = restoreReportText(missing);
  CHECK(line.contains("You can: Use current setup: the project uses"));
  CHECK(line.contains("Leave it: if the original setup"));

  // Reports that need no choice list none.
  CHECK(report(RestoreOutcome::exact)["options"].getArray()->isEmpty());
  CHECK(!report(RestoreOutcome::newerOnBranch)["message"].toString().contains("You can:"));
}

void testSavedEdits() {
  CHECK(!report(RestoreOutcome::exact)["message"].toString().contains("unsaved"));
  CHECK(!static_cast<bool>(report(RestoreOutcome::exact)["savedEdits"]));
  CHECK(static_cast<bool>(report(RestoreOutcome::exact, "Main (2)")["savedEdits"]));
  const auto saved = report(RestoreOutcome::exact, "Main (2)");
  CHECK(saved["message"].toString().contains("unsaved Fiddle changes were saved first"));
  CHECK(saved["message"].toString().contains("\"Main (2)\""));
}

void testBoundToCurrent() {
  const auto bound = boundToCurrentReport("Strings test");
  CHECK(bound["severity"] == "info");
  CHECK(bound["message"].toString().contains("Save the project in Dorico"));
}

void testEnvironmentChanges() {
  ProjectEnvironment saved;
  saved.appVersion = "1.0.51";
  saved.sampleRate = 48000.0;
  // Realistic millisecond file times (past 32 bits) and a negative VST3 ID.
  saved.plugins[1] = {"Synchron Player", "VSL", "1.2.0", 1763600000000, "/VST3/Synchron Player.vst3", true};
  saved.plugins[-2] = {"Pianoteq", "Modartt", "8.1", 1763600000001, "/VST3/Pianoteq 8.vst3", true};
  saved.plugins[3] = {"Gone Instrument", "Nobody", "1.0", 1763600000002, "/VST3/Gone.vst3", true};
  saved.plugins[4] = {"Big Reverb", "Acme", "2.0", 1763600000003, "/VST3/Big Reverb.vst3", false};
  saved.scripts["humanize"] = {"/scripts/humanize.lua", ProjectEnvironment::fingerprintOf("a")};
  saved.scripts["missing"] = {"/scripts/missing.lua", ProjectEnvironment::fingerprintOf("m")};

  // Nothing changed: nothing to say.
  CHECK(environmentChanges(saved, saved).empty());

  ProjectEnvironment now = saved;
  now.appVersion = "1.0.60";
  now.sampleRate = 44100.0;
  now.plugins[1].version = "1.3.0";        // updated
  now.plugins[-2].fileTime = 1763699999999; // same version, file replaced
  now.plugins.erase(3);                    // a missing instrument: the mixer reports it
  now.plugins.erase(4);                    // a missing effect: nothing else would say
  now.scripts["humanize"].fingerprint = ProjectEnvironment::fingerprintOf("b");
  now.scripts.erase("missing");            // missing: the mixer reports it
  const auto changes = environmentChanges(saved, now);
  CHECK(changes.size() == 6);
  const auto has = [&changes](const juce::String &text) {
    return std::any_of(changes.begin(), changes.end(), [&](const juce::String &c) { return c.contains(text); });
  };
  if (changes.size() == 6) {
    CHECK(changes[0].contains("Fiddle has been updated (1.0.51 to 1.0.60)"));
    CHECK(changes[1] == "The audio sample rate was 48 kHz when this project was saved and is 44.1 kHz now.");
  }
  CHECK(has("Synchron Player has been updated (1.2.0 to 1.3.0)."));
  CHECK(has("Pianoteq has been reinstalled or changed"));
  CHECK(has("The effect plug-in Big Reverb is no longer installed"));
  CHECK(!has("Gone Instrument"));
  CHECK(has("\"humanize\" has been edited"));
  // Unknown values (no audio device, an old record) aren't changes.
  ProjectEnvironment quiet = saved;
  quiet.sampleRate = 0.0;
  quiet.appVersion = {};
  CHECK(environmentChanges(saved, quiet).empty());

  // The record survives JSON; a foreign or missing one is ignored.
  const auto loaded = ProjectEnvironment::fromJson(juce::JSON::parse(juce::JSON::toString(saved.toJson())));
  CHECK(loaded.has_value());
  if (loaded) {
    CHECK(environmentChanges(saved, *loaded).empty());
    CHECK(loaded->plugins.at(1).manufacturer == "VSL");
    CHECK(loaded->plugins.at(-2).fileTime == 1763600000001);
    CHECK(loaded->plugins.at(1).instrument && !loaded->plugins.at(4).instrument);
    CHECK(loaded->plugins.at(4).path == "/VST3/Big Reverb.vst3");
    CHECK(loaded->scripts.at("humanize").filePath == "/scripts/humanize.lua");
  }
  CHECK(!ProjectEnvironment::fromJson(juce::var()).has_value());
  CHECK(!ProjectEnvironment::fromJson(juce::JSON::parse("{\"schema\": 2}")).has_value());

  // The restore report carries the changes, and the log line includes them.
  RestoreReportInput in;
  in.outcome = RestoreOutcome::exact;
  in.branchName = "Main";
  in.changes = changes;
  const auto built = buildRestoreReport(in);
  CHECK(built["changes"].size() == 6);
  CHECK(restoreReportText(built).contains("Synchron Player has been updated"));
  CHECK(report(RestoreOutcome::exact)["changes"].size() == 0);
}

} // namespace

int main() {
  testOutcomes();
  testSavedEdits();
  testBoundToCurrent();
  testEnvironmentChanges();
  std::cout << passed << " passed, " << failed << " failed" << std::endl;
  return failed == 0 ? 0 : 1;
}
