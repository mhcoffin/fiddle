// What the user is told when a Dorico project connects (ProjectRestoreReport.h).
#include "../ProjectRestoreReport.h"

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

  const auto kept = report(RestoreOutcome::notSwitched, {}, "plug-ins are still loading");
  CHECK(kept["action"] == "retry");
  CHECK(kept["message"].toString().contains("plug-ins are still loading"));
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

} // namespace

int main() {
  testOutcomes();
  testSavedEdits();
  testBoundToCurrent();
  std::cout << passed << " passed, " << failed << " failed" << std::endl;
  return failed == 0 ? 0 : 1;
}
