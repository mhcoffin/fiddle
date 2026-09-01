#include "../MessageRouter.h"
#include "../PluginCommands.h"
#include "../PluginJsHandlers.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition) {                                                           \
      ++passed;                                                                \
    } else {                                                                   \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__                   \
                << "]: " #condition << std::endl;                            \
    }                                                                          \
  } while (false)

juce::var payload(std::initializer_list<juce::var> values) {
  juce::Array<juce::var> arguments;
  for (const auto &value : values)
    arguments.add(value);
  return juce::var(arguments);
}

class FakePluginCommands final : public fiddle::PluginCommands {
public:
  fiddle::PluginCatalogState state;
  bool scanAccepted = true;
  bool commandResult = true;
  std::vector<std::string> calls;
  std::vector<juce::String> stripIds;
  juce::String stripId;
  juce::String libraryId;
  juce::String entityId;
  int pluginUid = 0;
  int instanceNumber = 0;
  ScanCompletion scanCompletion;
  LoadCompletion loadCompletion;

  fiddle::PluginCatalogState catalogState() const override { return state; }

  bool isPluginAvailable(int uid) const override {
    return uid == 0 || uid == 42;
  }

  bool beginScan(bool fullRescan, ScanCompletion completion) override {
    calls.emplace_back(fullRescan ? "rescan" : "scan");
    if (!scanAccepted)
      return false;
    scanCompletion = std::move(completion);
    return true;
  }

  bool setPlugin(const juce::String &id, int uid,
                 LoadCompletion completion) override {
    calls.emplace_back("set-plugin");
    stripId = id;
    pluginUid = uid;
    loadCompletion = std::move(completion);
    return commandResult;
  }

  bool setGroupPlugin(const std::vector<juce::String> &ids, int uid,
                      LoadCompletion completion) override {
    calls.emplace_back("set-group-plugin");
    stripIds = ids;
    pluginUid = uid;
    loadCompletion = std::move(completion);
    return commandResult;
  }

  bool showEditor(const juce::String &id) override {
    calls.emplace_back("show-editor");
    stripId = id;
    return commandResult;
  }

  bool restoreLibraryState(const juce::String &id,
                           const juce::String &targetLibraryId,
                           const juce::String &targetEntityId,
                           int targetInstanceNumber) override {
    calls.emplace_back("restore-state");
    stripId = id;
    libraryId = targetLibraryId;
    entityId = targetEntityId;
    instanceNumber = targetInstanceNumber;
    return commandResult;
  }
};

void testScanAndCatalogMessages() {
  fiddle::MessageRouter router;
  FakePluginCommands commands;
  std::vector<juce::String> logs;
  std::vector<bool> scanningStates;
  std::vector<juce::var> pluginLists;
  fiddle::PluginJsHandlers handlers(
      router, commands,
      {{}, [&](const juce::String &message) { logs.push_back(message); },
       [&](bool scanning) { scanningStates.push_back(scanning); },
       [&](const juce::var &plugins) { pluginLists.push_back(plugins); }, {}});
  handlers.registerHandlers();

  CHECK(!router.handleMessage("notAPluginCommand", {}));
  CHECK(router.handleMessage("scanPlugins", {}));
  CHECK(commands.calls.back() == "scan");
  CHECK(logs.size() == 1);
  CHECK(logs.back().contains("incremental"));
  CHECK(static_cast<bool>(commands.scanCompletion));

  fiddle::PluginCatalogState completed;
  completed.count = 2;
  completed.plugins = juce::JSON::parse(R"([{"uid":1},{"uid":2}])");
  commands.scanCompletion(completed);
  CHECK(logs.size() == 2);
  CHECK(logs.back().contains("2 plugins found"));
  CHECK(pluginLists.size() == 1);
  CHECK(pluginLists.back().getArray()->size() == 2);

  CHECK(router.handleMessage("rescanPlugins", {}));
  CHECK(commands.calls.back() == "rescan");
  CHECK(logs.back().contains("Full rescan"));

  const auto logCount = logs.size();
  commands.scanAccepted = false;
  CHECK(router.handleMessage("scanPlugins", {}));
  CHECK(logs.size() == logCount);

  commands.state.scanning = true;
  commands.state.count = 1;
  commands.state.plugins = juce::JSON::parse(R"([{"uid":42}])");
  CHECK(router.handleMessage("requestPluginsState", {}));
  CHECK(scanningStates == std::vector<bool>({true}));
  CHECK(pluginLists.size() == 2);

  commands.state.count = 0;
  commands.state.plugins = juce::var();
  CHECK(router.handleMessage("requestPluginsState", {}));
  CHECK(scanningStates.size() == 2);
  CHECK(pluginLists.size() == 2);
}

void testAssignmentAndCompletionMessages() {
  fiddle::MessageRouter router;
  FakePluginCommands commands;
  int changed = 0;
  int dispatched = 0;
  fiddle::PluginJsHandlers handlers(
      router, commands,
      {[&](fiddle::PluginJsHandlers::Task task) {
         ++dispatched;
         task();
       },
       {}, {}, {}, [&] { ++changed; }});
  handlers.registerHandlers();

  CHECK(router.handleMessage("setStripPlugin", payload({"strip-a", 999})));
  CHECK(dispatched == 0);
  CHECK(commands.calls.empty());

  CHECK(router.handleMessage("setStripPlugin", payload({"strip-a", 42})));
  CHECK(dispatched == 1);
  CHECK(commands.calls.back() == "set-plugin");
  CHECK(commands.stripId == "strip-a");
  CHECK(commands.pluginUid == 42);
  CHECK(changed == 0);
  CHECK(static_cast<bool>(commands.loadCompletion));
  commands.loadCompletion();
  CHECK(changed == 1);

  CHECK(router.handleMessage("setStripPlugin", payload({"strip-a", 0})));
  CHECK(commands.pluginUid == 0);

  const juce::String idsJson = R"(["strip-a","strip-b"])";
  CHECK(router.handleMessage("setGroupPlugin", payload({idsJson, 42})));
  CHECK(commands.calls.back() == "set-group-plugin");
  CHECK(commands.stripIds.size() == 2);
  CHECK(commands.stripIds[1] == "strip-b");
  CHECK(commands.pluginUid == 42);

  CHECK(router.handleMessage("setGroupPlugin", payload({idsJson, 0})));
  CHECK(commands.calls.back() == "set-group-plugin");
  CHECK(commands.pluginUid == 0);

  const auto callCount = commands.calls.size();
  CHECK(router.handleMessage("setGroupPlugin", payload({idsJson, 999})));
  CHECK(commands.calls.size() == callCount);
}

void testEditorRestoreAndPayloadValidation() {
  fiddle::MessageRouter router;
  FakePluginCommands commands;
  std::vector<fiddle::PluginJsHandlers::Task> pending;
  fiddle::PluginJsHandlers handlers(
      router, commands,
      {[&](fiddle::PluginJsHandlers::Task task) {
         pending.push_back(std::move(task));
       },
       {}, {}, {}, {}});
  handlers.registerHandlers();

  CHECK(router.handleMessage("showStripEditor", payload({"strip-a"})));
  CHECK(commands.calls.empty());
  CHECK(pending.size() == 1);
  pending.back()();
  CHECK(commands.calls.back() == "show-editor");
  CHECK(commands.stripId == "strip-a");

  CHECK(router.handleMessage(
      "restoreLibraryPluginState",
      payload({"strip-b", "library-1", "entity-2", 3})));
  CHECK(pending.size() == 2);
  pending.back()();
  CHECK(commands.calls.back() == "restore-state");
  CHECK(commands.stripId == "strip-b");
  CHECK(commands.libraryId == "library-1");
  CHECK(commands.entityId == "entity-2");
  CHECK(commands.instanceNumber == 3);

  const auto pendingCount = pending.size();
  CHECK(router.handleMessage("showStripEditor", payload({})));
  CHECK(router.handleMessage("setStripPlugin", "not-an-array"));
  CHECK(router.handleMessage("setGroupPlugin", payload({"not-json", 42})));
  CHECK(router.handleMessage("restoreLibraryPluginState",
                             payload({"strip-a", "library"})));
  CHECK(pending.size() == pendingCount);
}

} // namespace

int main() {
  std::cout << "===== Plugin JavaScript Handler Tests =====" << std::endl;

  testScanAndCatalogMessages();
  testAssignmentAndCompletionMessages();
  testEditorRestoreAndPayloadValidation();

  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
