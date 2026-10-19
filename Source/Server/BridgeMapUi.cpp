#include "MainComponent.h"
#include "BridgeMapCommands.h"

namespace fiddle {
void MainComponent::pushBridgeMapState() {
  if (bridgeMapCommands_) broadcastMessage("setBridgeMapState", bridgeMapCommands_->status());
}

void MainComponent::setupBridgeMapHandlers() {
  bridgeMapCommands_ = std::make_unique<BridgeMapCommands>(mixer_, noteTracker, undoManager_,
      [this] { stateManager_.markDirty(); scheduleStateRebuild(); pushConfigStatus(); pushBridgeMapState(); },
      [this] { return isTransportStarted_.load(std::memory_order_acquire); });
  mixer_.onBridgeMapsChanged = [this](const std::string &json) {
    bridgeMapCommands_->restore(json); pushBridgeMapState(); broadcastTemplateDirty();
  };
  const auto argument = [](const juce::var &payload) {
    return payload.isArray() && payload.size() > 0 ? payload[0] : juce::var{};
  };
  jsRouter_.registerHandler("requestBridgeMaps", [this](const juce::var &) {
    safeCallAsync([this] { pushBridgeMapState(); });
  });
  jsRouter_.registerHandler("addBridgeTemplate", [this, argument](const juce::var &payload) {
    const auto section = argument(payload).toString();
    safeCallAsync([this, section] {
      const std::map<juce::String, juce::String> templates{
          {"Strings", "Fiddle-Orchestral-Strings-r3.doricolib"},
          {"Woodwinds", "Fiddle-Orchestral-Woodwinds-r1.doricolib"},
          {"Brass", "Fiddle-Orchestral-Brass-r1.doricolib"}};
      const auto it = templates.find(section);
      if (it == templates.end()) return;
      const auto file = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
          .getParentDirectory().getSiblingFile("Resources").getChildFile("BridgeMaps").getChildFile(it->second);
      if (!file.existsAsFile()) bridgeMapCommands_->reportError("Bridge template is missing from this app build");
      else bridgeMapCommands_->publish(file.loadFileAsString());
      pushBridgeMapState();
    });
  });
  jsRouter_.registerHandler("publishBridgeRevision", [this, argument](const juce::var &payload) {
    const auto data = argument(payload);
    safeCallAsync([this, data] {
      if (data["sourceXml"].isString() && data["low"].isInt() && data["high"].isInt())
        bridgeMapCommands_->publish(data["sourceXml"].toString(), data["parent"].toString(), int(data["low"]), int(data["high"]));
      pushBridgeMapState();
    });
  });
  jsRouter_.registerHandler("assignBridgeRevision", [this, argument](const juce::var &payload) {
    const auto data = argument(payload);
    safeCallAsync([this, data] {
      if (data["port"].isInt() && data["channel"].isInt())
        bridgeMapCommands_->assign(int(data["port"]), int(data["channel"]), data["hash"].toString());
      pushBridgeMapState();
    });
  });
  for (const juce::String command : {"importBridgeMap", "verifyBridgeAssignment"}) {
    jsRouter_.registerHandler(command, [this, argument, command](const juce::var &payload) {
      const auto data = argument(payload);
      safeCallAsync([this, data, command] {
        const bool verify = command == "verifyBridgeAssignment";
        const auto expectedProject = mixer_.projectSettings().bridgeMapsJson;
        auto chooser = std::make_shared<juce::FileChooser>(
            verify ? "Select an export of the map assigned to this channel in Dorico" : "Import bridge map or project bundle",
            juce::File{}, verify ? "*.doricolib" : "*.doricolib;*.fiddlebridge");
        juce::Component::SafePointer<MainComponent> safeThis(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis, chooser, verify, data, expectedProject](const juce::FileChooser &fc) {
              if (!safeThis || fc.getResult() == juce::File{}) return;
              auto &commands = *safeThis->bridgeMapCommands_;
              if (expectedProject != safeThis->mixer_.projectSettings().bridgeMapsJson)
                commands.reportError("The project changed while choosing a file; select the assignment again");
              else if (fc.getResult().getSize() > 16 * 1024 * 1024)
                commands.reportError("Bridge file exceeds 16 MiB");
              else if (verify && data["port"].isInt() && data["channel"].isInt())
                commands.verify(int(data["port"]), int(data["channel"]), fc.getResult().loadFileAsString());
              else if (fc.getResult().hasFileExtension("fiddlebridge"))
                commands.importBundle(fc.getResult().loadFileAsString());
              else commands.publish(fc.getResult().loadFileAsString());
              safeThis->pushBridgeMapState();
            });
      });
    });
  }
  jsRouter_.registerHandler("exportBridgeMap", [this, argument](const juce::var &payload) {
    const auto hash = argument(payload).toString();
    safeCallAsync([this, hash] {
      const auto content = hash.isEmpty() ? bridgeMapCommands_->exportBundle() : bridgeMapCommands_->exportXml(hash);
      if (content.isEmpty()) return;
      const auto extension = hash.isEmpty() ? ".fiddlebridge" : ".doricolib";
      auto chooser = std::make_shared<juce::FileChooser>("Export bridge artifact",
          juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Fiddle-Bridge" + juce::String(extension)), "*" + juce::String(extension));
      juce::Component::SafePointer<MainComponent> safeThis(this);
      chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
          [safeThis, chooser, content](const juce::FileChooser &fc) {
            if (!safeThis || fc.getResult() == juce::File{}) return;
            if (!fc.getResult().replaceWithText(content)) safeThis->bridgeMapCommands_->reportError("Could not write bridge artifact");
            safeThis->pushBridgeMapState();
          });
    });
  });
}
} // namespace fiddle
