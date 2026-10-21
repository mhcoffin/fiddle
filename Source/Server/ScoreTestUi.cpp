#include "MainComponent.h"
#include "FiddleConfig.h"

namespace fiddle {
namespace {
// tests/score/generated, copied into the app bundle at build time.
juce::File scoreTestsFolder() {
  return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
      .getParentDirectory().getSiblingFile("Resources").getChildFile("ScoreTests");
}
juce::File scoreTestRunsFolder() {
  return FiddleConfig::getAppDataDir().getChildFile("ScoreTests").getChildFile("runs");
}
// Scores are opened from a copy outside the app bundle, which rebuilds replace.
juce::File scoreTestScoresFolder() {
  return FiddleConfig::getAppDataDir().getChildFile("ScoreTests").getChildFile("scores");
}
juce::File copyOfScore(const juce::String &testId) {
  const auto source = scoreTestsFolder().getChildFile(testId + ".musicxml");
  if (testId.isEmpty() || testId.containsAnyOf("/\\") || !source.existsAsFile()) return {};
  const auto copy = scoreTestScoresFolder().getChildFile(source.getFileName());
  copy.getParentDirectory().createDirectory();
  if (copy.loadFileAsString() != source.loadFileAsString() && !source.copyFileTo(copy)) return {};
  return copy;
}
// The newest installed Dorico, else whatever opens MusicXML by default.
bool openInDorico(const juce::File &score) {
  auto apps = juce::File("/Applications").findChildFiles(juce::File::findDirectories, false, "Dorico*.app");
  apps.sort();
  if (!apps.isEmpty()) {
    juce::ChildProcess open;
    if (open.start(juce::StringArray{"/usr/bin/open", "-a", apps.getLast().getFullPathName(), score.getFullPathName()}))
      return open.waitForProcessToFinish(5000) && open.getExitCode() == 0;
  }
  return score.startAsProcess();
}
const char *beaconName(BridgeDecoderState::Beacon beacon) {
  switch (beacon) {
  case BridgeDecoderState::Beacon::Unsupported: return "unsupported";
  case BridgeDecoderState::Beacon::Absent: return "absent";
  case BridgeDecoderState::Beacon::Matched: return "matched";
  case BridgeDecoderState::Beacon::Mismatched: return "mismatched";
  }
  return "";
}
} // namespace

void MainComponent::pushScoreTestState() {
  const auto folder = scoreTestsFolder();
  const auto index = juce::JSON::parse(folder.getChildFile("index.json"));
  juce::Array<juce::var> tests;
  if (const auto *list = index["tests"].getArray())
    for (const auto &test : *list) {
      // A copy, with the score's full path for opening it in Dorico.
      auto entry = juce::JSON::parse(juce::JSON::toString(test));
      if (auto *object = entry.getDynamicObject())
        object->setProperty("musicxml_path", folder.getChildFile(test["musicxml"].toString()).getFullPathName());
      tests.add(entry);
    }
  juce::DynamicObject::Ptr state = new juce::DynamicObject();
  state->setProperty("folder", folder.getFullPathName());
  state->setProperty("runs_folder", scoreTestRunsFolder().getFullPathName());
  state->setProperty("tests", tests);
  state->setProperty("armed", scoreTestRecorder_.armed());
  state->setProperty("armed_test", scoreTestRecorder_.armed() ? juce::var(scoreTestArmedId_) : juce::var());
  const auto [notes, discarded] = scoreTestRecorder_.progress();
  state->setProperty("recorded_notes", scoreTestRecorder_.armed() ? juce::var(notes) : juce::var());
  state->setProperty("discarded_takes", scoreTestRecorder_.armed() ? juce::var(discarded) : juce::var());
  state->setProperty("last_run", scoreTestLastRun_);
  state->setProperty("error", scoreTestError_);
  if (tests.isEmpty() && scoreTestError_.isEmpty())
    state->setProperty("error", "No score tests in " + folder.getFullPathName());
  broadcastMessage("scoreTestState", juce::var(state.get()));
}

void MainComponent::armScoreTest(const juce::String &testId) {
  scoreTestError_.clear();
  const auto file = scoreTestsFolder().getChildFile(testId + ".expected.json");
  const auto expected = juce::JSON::parse(file);
  if (testId.isEmpty() || !expected.isObject() || expected["id"].toString() != testId) {
    scoreTestError_ = "Unknown score test: " + testId;
    pushScoreTestState();
    return;
  }
  // Each chair's endpoint and Dorico instrument (as the playback template
  // routes them), so the comparison can tell which part is which.
  std::map<scoretest::Endpoint, juce::String> instruments;
  if (auto *repository = db_.getLibraryRoutingRepository())
    for (const auto &chair : repository->listChairs())
      if (chair.flatIndex > 0)
        instruments[{chair.flatIndex / 16, chair.flatIndex % 16 + 1}] = juce::String(chair.instrumentEntityId);
  scoreTestRecorder_.arm(testId, expected, std::move(instruments));
  scoreTestArmedId_ = testId;
  pushScoreTestState();
}

void MainComponent::finishScoreTest() {
  scoreTestError_.clear();
  auto take = scoreTestRecorder_.finish();
  if (!take) {
    scoreTestError_ = "No score test is armed";
    pushScoreTestState();
    return;
  }
  // What the decoder saw on each chair's endpoint: beacon and withheld notes
  // explain notes that never arrived.
  juce::Array<juce::var> bridges;
  for (const auto &[endpoint, instrument] : take->instruments) {
    const auto diagnostics = noteTracker.bridgeDiagnostics(endpoint.first, endpoint.second);
    juce::DynamicObject::Ptr b = new juce::DynamicObject();
    b->setProperty("port", endpoint.first + 1);
    b->setProperty("channel", endpoint.second);
    b->setProperty("instrument", instrument);
    b->setProperty("bridge", diagnostics.has_value());
    if (diagnostics) {
      b->setProperty("beacon", beaconName(diagnostics->beacon));
      b->setProperty("attested", diagnostics->attested);
      b->setProperty("invalid_commands", juce::int64(diagnostics->invalidCommands));
      juce::DynamicObject::Ptr withheld = new juce::DynamicObject();
      for (size_t i = 1; i < diagnostics->withheld.size(); ++i)
        if (diagnostics->withheld[i])
          withheld->setProperty(BridgeDecoderState::describe(BridgeDecoderState::Withheld(i)),
                                juce::int64(diagnostics->withheld[i]));
      b->setProperty("withheld_total", withheld.get());
    }
    bridges.add(b.get());
  }
  juce::DynamicObject::Ptr context = new juce::DynamicObject();
  context->setProperty("bridges", bridges);
  context->setProperty("note", "withheld_total counts every note withheld since the bridge maps were loaded");

  const auto runs = scoreTestRunsFolder();
  runs.createDirectory();
  auto id = scoretest::runId(take->testId, juce::Time::getCurrentTime());
  for (int n = 2; runs.getChildFile(id + ".json").exists(); ++n)
    id = scoretest::runId(take->testId, juce::Time::getCurrentTime()) + "-" + juce::String(n);
  const auto record = scoretest::runRecord(id, *take, juce::var(context.get()));
  const auto file = runs.getChildFile(id + ".json");
  if (!file.replaceWithText(juce::JSON::toString(record, false)))
    scoreTestError_ = "Could not write " + file.getFullPathName();

  juce::DynamicObject::Ptr last = new juce::DynamicObject();
  for (const auto *key : {"run_id", "test_id", "title", "pass", "summary", "finished_at"})
    last->setProperty(key, record[key]);
  last->setProperty("path", file.getFullPathName());
  juce::Array<juce::var> failures;
  if (const auto *list = record["failures"].getArray())
    for (int i = 0; i < std::min(list->size(), 50); ++i) failures.add((*list)[i]);
  last->setProperty("failures", failures);
  last->setProperty("more_failures", std::max(0, (record["failures"].getArray() ? record["failures"].size() : 0) - 50));
  scoreTestLastRun_ = juce::var(last.get());
  pushScoreTestState();
}

void MainComponent::setupScoreTestHandlers() {
  const auto argument = [](const juce::var &payload) {
    return payload.isArray() && payload.size() > 0 ? payload[0] : juce::var{};
  };
  jsRouter_.registerHandler("requestScoreTests", [this](const juce::var &) {
    safeCallAsync([this] { pushScoreTestState(); });
  });
  jsRouter_.registerHandler("armScoreTest", [this, argument](const juce::var &payload) {
    const auto id = argument(payload).toString();
    safeCallAsync([this, id] { armScoreTest(id); });
  });
  jsRouter_.registerHandler("finishScoreTest", [this](const juce::var &) {
    safeCallAsync([this] { finishScoreTest(); });
  });
  jsRouter_.registerHandler("openScoreTestScore", [this, argument](const juce::var &payload) {
    const auto id = argument(payload).toString();
    safeCallAsync([this, id] {
      const auto score = copyOfScore(id);
      scoreTestError_ = score == juce::File{} ? "No score for test " + id
                        : openInDorico(score) ? juce::String() : "Could not open " + score.getFullPathName();
      pushScoreTestState();
    });
  });
  jsRouter_.registerHandler("revealScoreTestScore", [this, argument](const juce::var &payload) {
    const auto id = argument(payload).toString();
    safeCallAsync([this, id] {
      const auto score = copyOfScore(id);
      if (score == juce::File{}) { scoreTestError_ = "No score for test " + id; pushScoreTestState(); return; }
      score.revealToUser();
    });
  });
  jsRouter_.registerHandler("revealScoreTestRun", [this, argument](const juce::var &payload) {
    const auto id = argument(payload).toString();
    safeCallAsync([this, id] {
      const auto runs = scoreTestRunsFolder();
      const auto file = runs.getChildFile(id + ".json");
      if (id.isNotEmpty() && !id.containsAnyOf("/\\") && file.existsAsFile()) file.revealToUser();
      else { runs.createDirectory(); runs.revealToUser(); }
    });
  });
  jsRouter_.registerHandler("copyScoreTestText", [argument](const juce::var &payload) {
    const auto text = argument(payload).toString();
    if (text.isNotEmpty()) juce::SystemClipboard::copyTextToClipboard(text);
  });
  jsRouter_.registerHandler("cancelScoreTest", [this](const juce::var &) {
    safeCallAsync([this] {
      scoreTestRecorder_.cancel();
      scoreTestError_.clear();
      pushScoreTestState();
    });
  });
}
} // namespace fiddle
