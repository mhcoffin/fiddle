#pragma once

// What Fiddle tells the user when a Dorico project connects and Fiddle looks
// for the setup the project was saved with (docs/dorico-project-identity-results.md).
// The main window shows it as a banner; fiddle_get_status reports it to agents.

#include <juce_core/juce_core.h>

#include <vector>

namespace fiddle {

enum class RestoreOutcome {
  exact,         // the version the project was saved with
  newerOnBranch, // that version is gone; the latest version of its branch
  byBranchName,  // a project saved before Fiddle recorded IDs, by branch name
  notFound,      // the project's setup isn't in this Fiddle database
  notSwitched    // Fiddle couldn't save unsaved edits (or load the version), so kept the current setup
};

struct RestoreReportInput {
  RestoreOutcome outcome = RestoreOutcome::exact;
  juce::String savedBranchName; // the branch name the project recorded
  juce::String branchName;      // the branch Fiddle uses now
  juce::String savedEditsBranch; // unsaved edits were saved here before switching
  juce::String error;
  bool retryAfterEdits = false; // notSwitched because unsaved edits couldn't be saved
  /// What may sound different since the project was saved
  /// (environmentChanges in ProjectEnvironment.h).
  std::vector<juce::String> changes;
};

/// {kind, severity (info or warning), title, message, action, savedEdits,
/// options, changes}. changes lists what may sound different since the
/// project was saved. options lists the user's choices ({label, text}) when the
/// project's own setup isn't loaded; the message then ends "You can:". action is
/// "useCurrentSetup" when the project still points at a setup Fiddle doesn't
/// have, "retry" when Fiddle didn't switch, and empty otherwise.
/// An exact restore's title and severity: a warning when the project may
/// not sound as saved, otherwise plain news.
inline constexpr const char *kExactTitle = "Dorico project connected";
inline constexpr const char *kMaySoundDifferentTitle =
    "This project may sound different than when it was saved";

inline juce::var buildRestoreReport(const RestoreReportInput &in) {
  const auto quoted = [](const juce::String &name) {
    return name.isEmpty() ? juce::String("an unnamed branch") : "branch \"" + name + "\"";
  };
  juce::String kind, severity = "warning", title, message, action;
  // The user's choices when the project's own setup isn't loaded, each a
  // {label, text} pair shown as a list under the message.
  juce::Array<juce::var> options;
  const auto option = [&options](const juce::String &label, const juce::String &text) {
    auto *item = new juce::DynamicObject();
    item->setProperty("label", label);
    item->setProperty("text", text);
    options.add(juce::var(item));
  };
  const auto chooseAnother = [&] {
    option("Choose another branch or version in Fiddle", "the project uses that one instead.");
  };
  const auto leaveIt = [&](const juce::String &when) {
    const juce::String keeps = "project keeps its link to its own setup, even when you save it in Dorico.";
    option("Leave it", when.isEmpty() ? "the " + keeps : when + "The " + keeps);
  };
  switch (in.outcome) {
  case RestoreOutcome::exact:
    kind = "exact";
    // The right setup, but things it depends on changed: it may not sound
    // the same, so this is a warning like the other "may sound different" cases.
    severity = in.changes.empty() ? "info" : "warning";
    title = in.changes.empty() ? kExactTitle : kMaySoundDifferentTitle;
    message = "Fiddle loaded the setup this project was saved with (" + quoted(in.branchName) + ").";
    break;
  case RestoreOutcome::newerOnBranch:
    kind = "newerOnBranch";
    title = "This project's saved Fiddle version is gone";
    message = "The Fiddle version this project was saved with no longer exists, so Fiddle loaded the "
              "latest version of " + quoted(in.branchName) + ". It may not sound the way the project "
              "did when it was saved.";
    break;
  case RestoreOutcome::byBranchName:
    kind = "byBranchName";
    title = "Loaded by branch name";
    message = "This project was saved before Fiddle recorded exact versions, so Fiddle loaded the latest "
              "version of " + quoted(in.branchName) + ". It may not sound the way the project did when "
              "it was saved.";
    break;
  case RestoreOutcome::notFound:
    kind = "notFound";
    title = "This project's Fiddle setup isn't on this Mac";
    message = "This project was saved with a Fiddle setup (" + quoted(in.savedBranchName) +
              ") that isn't in this Mac's Fiddle database. For now, Fiddle is playing it with " +
              quoted(in.branchName) + ".";
    option("Use current setup", "the project uses " + quoted(in.branchName) + " from now on.");
    chooseAnother();
    leaveIt("if the original setup may come back, for example from a backup of Fiddle's database. ");
    action = "useCurrentSetup";
    break;
  case RestoreOutcome::notSwitched:
    kind = "notSwitched";
    title = "Fiddle didn't switch to this project's setup";
    message = "Fiddle couldn't switch to the setup this project was saved with (" +
              quoted(in.savedBranchName) + ")" +
              (in.error.isNotEmpty() ? ": " + in.error : juce::String()) +
              ". For now, Fiddle is playing it with " + quoted(in.branchName) + ".";
    option("Load project setup", in.retryAfterEdits
                                     ? "try again once your changes in Fiddle are saved or undone."
                                     : "try loading the project's setup again.");
    chooseAnother();
    leaveIt({});
    action = "retry";
    break;
  }
  if (in.savedEditsBranch.isNotEmpty())
    message += " Your unsaved Fiddle changes were saved first, as a new version of " +
               quoted(in.savedEditsBranch) + ".";
  if (!options.isEmpty())
    message += " You can:";
  auto *report = new juce::DynamicObject();
  report->setProperty("kind", kind);
  report->setProperty("severity", severity);
  report->setProperty("title", title);
  report->setProperty("message", message);
  report->setProperty("action", action);
  report->setProperty("savedEdits", in.savedEditsBranch.isNotEmpty());
  report->setProperty("options", juce::var(options));
  juce::Array<juce::var> changes;
  for (const auto &change : in.changes) changes.add(change);
  report->setProperty("changes", changes);
  return juce::var(report);
}

/// The report with its list of changes emptied (after a Dorico save accepts
/// them). An exact restore without changes is plain news again.
inline juce::var withoutChanges(const juce::var &report) {
  auto *copy = report.getDynamicObject() ? report.getDynamicObject()->clone().release() : nullptr;
  if (!copy)
    return report;
  copy->setProperty("changes", juce::Array<juce::var>());
  if (copy->getProperty("kind").toString() == "exact") {
    copy->setProperty("severity", "info");
    copy->setProperty("title", kExactTitle);
  }
  return juce::var(copy);
}

/// The report as one line of text, choices included (for the Debug log).
inline juce::String restoreReportText(const juce::var &report) {
  auto text = report["message"].toString();
  if (const auto *options = report["options"].getArray())
    for (const auto &option : *options)
      text += " " + option["label"].toString() + ": " + option["text"].toString();
  if (const auto *changes = report["changes"].getArray())
    for (const auto &change : *changes)
      text += " " + change.toString();
  return text;
}

/// After the user chooses Use current setup for a project whose setup isn't here.
inline juce::var boundToCurrentReport(const juce::String &branchName) {
  auto *report = new juce::DynamicObject();
  report->setProperty("kind", "boundToCurrent");
  report->setProperty("severity", "info");
  report->setProperty("title", "Project now uses the current setup");
  report->setProperty("message", "This project now uses branch \"" + branchName +
                                     "\". Save the project in Dorico to keep that.");
  report->setProperty("action", juce::String());
  return juce::var(report);
}

} // namespace fiddle
