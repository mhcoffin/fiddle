#include "../ExpressionMapCommands.h"
#include "../ExpressionMapJsHandlers.h"
#include "../MessageRouter.h"

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

class FakeExpressionMapCommands final : public fiddle::ExpressionMapCommands {
public:
  bool result = true;
  juce::var catalogValue = juce::JSON::parse(R"([{"entityID":"map-1"}])");
  std::vector<std::string> calls;
  std::vector<juce::String> stripIds;
  juce::String stripId;
  juce::String entityId;
  juce::File file;

  juce::var catalog() const override { return catalogValue; }

  bool assign(const juce::String &id,
              const juce::String &targetEntityId) override {
    calls.emplace_back("assign");
    stripId = id;
    entityId = targetEntityId;
    return result;
  }

  bool clear(const juce::String &id) override {
    calls.emplace_back("clear");
    stripId = id;
    return result;
  }

  bool assignGroup(const std::vector<juce::String> &ids,
                   const juce::String &targetEntityId) override {
    calls.emplace_back("assign-group");
    stripIds = ids;
    entityId = targetEntityId;
    return result;
  }

  bool importFile(const juce::String &id,
                  const juce::File &targetFile) override {
    calls.emplace_back("import-file");
    stripId = id;
    file = targetFile;
    return result;
  }
};

void testCatalogAndIndividualAssignments() {
  fiddle::MessageRouter router;
  FakeExpressionMapCommands commands;
  std::vector<fiddle::ExpressionMapJsHandlers::Task> pending;
  std::vector<juce::var> catalogs;
  int changed = 0;
  fiddle::ExpressionMapJsHandlers handlers(
      router, commands,
      {[&](fiddle::ExpressionMapJsHandlers::Task task) {
         pending.push_back(std::move(task));
       },
       {}, [&](const juce::var &catalog) { catalogs.push_back(catalog); },
       [&] { ++changed; }});
  handlers.registerHandlers();

  CHECK(!router.handleMessage("notAnExpressionMapCommand", {}));
  CHECK(router.handleMessage("requestExpressionMaps", {}));
  CHECK(pending.size() == 1);
  pending.back()();
  CHECK(catalogs.size() == 1);
  CHECK(catalogs.back().getArray()->size() == 1);

  CHECK(router.handleMessage("loadExpressionMap",
                             payload({"strip-a", "map-1"})));
  CHECK(pending.size() == 2);
  pending.back()();
  CHECK(commands.calls.back() == "assign");
  CHECK(commands.stripId == "strip-a");
  CHECK(commands.entityId == "map-1");
  CHECK(changed == 1);

  CHECK(router.handleMessage("clearExpressionMap", payload({"strip-a"})));
  pending.back()();
  CHECK(commands.calls.back() == "clear");
  CHECK(changed == 2);

  commands.result = false;
  CHECK(router.handleMessage("clearExpressionMap", payload({"missing"})));
  pending.back()();
  CHECK(changed == 2);
}

void testGroupAssignmentAndClearing() {
  fiddle::MessageRouter router;
  FakeExpressionMapCommands commands;
  int dispatched = 0;
  int changed = 0;
  fiddle::ExpressionMapJsHandlers handlers(
      router, commands,
      {[&](fiddle::ExpressionMapJsHandlers::Task task) {
         ++dispatched;
         task();
       },
       {}, {}, [&] { ++changed; }});
  handlers.registerHandlers();

  const juce::String idsJson = R"(["strip-a","strip-b"])";
  CHECK(router.handleMessage("setGroupExpressionMap",
                             payload({idsJson, "map-2"})));
  CHECK(commands.calls.back() == "assign-group");
  CHECK(commands.stripIds.size() == 2);
  CHECK(commands.stripIds[1] == "strip-b");
  CHECK(commands.entityId == "map-2");
  CHECK(changed == 1);

  CHECK(router.handleMessage("setGroupExpressionMap",
                             payload({idsJson, ""})));
  CHECK(commands.calls.back() == "assign-group");
  CHECK(commands.entityId.isEmpty());
  CHECK(changed == 2);

  const auto dispatchCount = dispatched;
  CHECK(router.handleMessage("setGroupExpressionMap",
                             payload({"not-json", "map-2"})));
  CHECK(router.handleMessage("setGroupExpressionMap", payload({"[]", ""})));
  CHECK(dispatched == dispatchCount);
}

void testFileSelectionAndMalformedPayloads() {
  fiddle::MessageRouter router;
  FakeExpressionMapCommands commands;
  fiddle::ExpressionMapJsHandlers::FileSelection fileSelection;
  int dispatched = 0;
  int changed = 0;
  fiddle::ExpressionMapJsHandlers handlers(
      router, commands,
      {[&](fiddle::ExpressionMapJsHandlers::Task task) {
         ++dispatched;
         task();
       },
       [&](fiddle::ExpressionMapJsHandlers::FileSelection selection) {
         fileSelection = std::move(selection);
       },
       {}, [&] { ++changed; }});
  handlers.registerHandlers();

  CHECK(router.handleMessage("loadExpressionMapFromFile",
                             payload({"strip-a"})));
  CHECK(static_cast<bool>(fileSelection));
  CHECK(dispatched == 0);

  fileSelection(juce::File{});
  CHECK(commands.calls.empty());

  const juce::File selectedFile("/tmp/Test Map.doricolib");
  fileSelection(selectedFile);
  CHECK(dispatched == 1);
  CHECK(commands.calls.back() == "import-file");
  CHECK(commands.stripId == "strip-a");
  CHECK(commands.file == selectedFile);
  CHECK(changed == 1);

  const auto dispatchCount = dispatched;
  CHECK(router.handleMessage("loadExpressionMap", payload({"strip-a"})));
  CHECK(router.handleMessage("clearExpressionMap", "not-an-array"));
  CHECK(router.handleMessage("loadExpressionMapFromFile", payload({})));
  CHECK(dispatched == dispatchCount);
}

} // namespace

int main() {
  std::cout << "===== Expression Map JavaScript Handler Tests ====="
            << std::endl;

  testCatalogAndIndividualAssignments();
  testGroupAssignmentAndClearing();
  testFileSelectionAndMalformedPayloads();

  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
