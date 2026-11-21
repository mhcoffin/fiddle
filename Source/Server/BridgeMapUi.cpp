#include "MainComponent.h"
#include "BridgeMapCommands.h"
#include "DoricoConfigGenerator.h"
#include "FileChooserLaunch.h"

namespace fiddle {
void MainComponent::pushBridgeMapState() {
  if (bridgeMapCommands_) broadcastMessage("setBridgeMapState", bridgeMapCommands_->status());
}

void MainComponent::refreshBridgeArchive() {
  if (!bridgeMapCommands_) return;
  // Every revision Fiddle installed is still in Dorico's library: installs
  // never delete them, and they're the maps a project can hold.
  auto archive = std::make_shared<BridgeRevisionArchive>();
  const auto library = DoricoConfigGenerator().getDoricoBasePath().getChildFile("DefaultLibraryAdditions");
  for (const auto &file : library.findChildFiles(juce::File::findFiles, false, "Fiddle-Bridge-*.doricolib")) {
    // The files don't record a musical range; every built-in map uses the
    // default 21..114 (a custom range isn't offered in the UI).
    juce::String error;
    if (const auto doc = BridgeMapDocument::compile(file.loadFileAsString(), error))
      archive->add(doc->compiled);
  }
  bridgeMapCommands_->setArchive(archive);
}

void MainComponent::setupBridgeMapHandlers() {
  bridgeMapCommands_ = std::make_unique<BridgeMapCommands>(mixer_, noteTracker, undoManager_,
      [this] { stateManager_.markDirty(); scheduleStateRebuild(); pushConfigStatus(); pushBridgeMapState(); },
      [this] { return isTransportStarted_.load(std::memory_order_acquire); });
  const auto templates = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
      .getParentDirectory().getSiblingFile("Resources").getChildFile("BridgeMaps");
  bridgeMapCommands_->setBuiltIns([templates](const BridgeSectionInfo &info) {
    const auto file = templates.getChildFile(info.templateFile);
    return file.existsAsFile() ? file.loadFileAsString() : juce::String{};
  });
  refreshBridgeArchive();
  mixer_.onBridgeMapsChanged = [this](const std::string &json) {
    bridgeMapCommands_->restore(json); pushBridgeMapState(); broadcastTemplateDirty();
  };
  const auto argument = [](const juce::var &payload) {
    return payload.isArray() && payload.size() > 0 ? payload[0] : juce::var{};
  };
  jsRouter_.registerHandler("requestBridgeMaps", [this](const juce::var &) {
    safeCallAsync([this] { pushBridgeMapState(); });
  });
  // One section's key, or none for every section that differs.
  jsRouter_.registerHandler("useBuiltInBridgeMaps", [this, argument](const juce::var &payload) {
    const auto key = argument(payload).toString();
    safeCallAsync([this, key] {
      bridgeMapCommands_->useBuiltInSections(key.isEmpty() ? std::vector<juce::String>{} : std::vector<juce::String>{key});
      pushBridgeMapState();
    });
  });
  jsRouter_.registerHandler("exportBridgeMap", [this, argument](const juce::var &payload) {
    const auto hash = argument(payload).toString();
    safeCallAsync([this, hash] {
      const auto content = hash.isEmpty() ? bridgeMapCommands_->exportBundle() : bridgeMapCommands_->exportXml(hash);
      if (content.isEmpty()) return;
      const auto extension = hash.isEmpty() ? ".fiddlebridge" : ".doricolib";
      auto chooser = std::make_shared<juce::FileChooser>("Export bridge artifact",
          juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Fiddle-Bridge" + juce::String(extension)), "*" + juce::String(extension));
      juce::Component::SafePointer<MainComponent> safeThis(this);
      fiddle::launchFileChooser(*chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
          [safeThis, chooser, content](const juce::FileChooser &fc) {
            if (!safeThis || fc.getResult() == juce::File{}) return;
            if (!fc.getResult().replaceWithText(content)) safeThis->bridgeMapCommands_->reportError("Could not write bridge artifact");
            safeThis->pushBridgeMapState();
          });
    });
  });
}
} // namespace fiddle
