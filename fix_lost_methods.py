import sys

with open("Source/Server/MainComponent.cpp", "r") as f:
    text = f.read()

# Replace the original loadConfigByName with the updated version
old_load_config = """void MainComponent::loadConfigByName(const juce::String &name) {
  mixer_.clear();
  undoManager_.clear();

  if (db_.loadConfig(name)) {
    loadStripsFromDB();
  }

  configName_ = name;
  configVersion_ = {};
  stateManager_.setConfigName(configName_);
  stateManager_.setConfigVersion(configVersion_);
  if (onConfigChanged)
    onConfigChanged(configName_, configVersion_);

  pushMixerState(false);
}"""

new_load_config = """void MainComponent::loadConfigByName(const juce::String &name) {
  std::cerr << "[BranchCheckout] Attempting to checkout branch: '" << name
            << "'" << std::endl;
  if (!versionStore_) {
    std::cerr << "[BranchCheckout] FAILED: versionStore_ is null" << std::endl;
    return;
  }

  std::string nameUtf8 = name.toStdString();
  auto bIdOpt = versionStore_->getStorage().findBranchByName(nameUtf8);
  if (!bIdOpt) {
    std::cerr << "[BranchCheckout] FAILED: Branch '" << name
              << "' not found in DB." << std::endl;
    pushLogMessage("<b>[Config]</b> Branch '" + name + "' not found.", true);
    return;
  }

  auto headOpt = versionStore_->getBranchHead(*bIdOpt);
  if (!headOpt) {
    std::cerr << "[BranchCheckout] FAILED: Branch '" << name
              << "' has no head hash." << std::endl;
    return;
  }

  auto verOpt = versionStore_->getVersion(*headOpt);
  if (!verOpt) {
    std::cerr
        << "[BranchCheckout] FAILED: Could not load version for head hash: "
        << *headOpt << std::endl;
    return;
  }

  auto stateOpt = versionStore_->getState(verOpt->stateHash);
  if (!stateOpt) {
    std::cerr
        << "[BranchCheckout] FAILED: Could not load state for state hash: "
        << verOpt->stateHash << std::endl;
    return;
  }

  std::cerr << "[BranchCheckout] SUCCESS: Loading "
            << stateOpt->stripHashes.size() << " strips from state "
            << verOpt->stateHash << " (version: " << *headOpt << ")"
            << std::endl;

  mixer_.clear();
  undoManager_.clear();
  configName_ = name;
  configVersion_ = juce::String(*headOpt);

  for (const auto &sh : stateOpt->stripHashes) {
    auto sbOpt = versionStore_->getStripBlob(sh);
    if (!sbOpt)
      continue;

    juce::String newId = mixer_.addStrip();
    if (auto *strip = mixer_.getStrip(newId)) {
      strip->id = sbOpt->uuid;
      strip->library = sbOpt->library;
      strip->family = sbOpt->family;
      strip->isSolo = sbOpt->isSolo;
      strip->inputPort = sbOpt->inputPort;
      strip->inputChannel = sbOpt->inputChannel;
      strip->pluginUid = sbOpt->pluginUid;
      strip->gainDb.store(sbOpt->gainDb, std::memory_order_relaxed);

      if (!sbOpt->expressionMapEntityId.empty()) {
        strip->expressionMap = xmapLibrary_.load(sbOpt->expressionMapEntityId);
      }

      if (!sbOpt->pluginState.empty()) {
        strip->cachedPluginState_.replaceWith(sbOpt->pluginState.data(),
                                              sbOpt->pluginState.size());
      }
    }
  }

  stateManager_.setConfigName(configName_);
  stateManager_.setConfigVersion(configVersion_);
  saveAllStripsToDB(); // Sync the legacy schema mirror

  if (onConfigChanged)
    onConfigChanged(configName_, configVersion_);

  mixer_.syncStripsToInstruments(masterList_);
  pushMixerState(false);
  scheduleStateRebuild();
  pushLogMessage("<b>[Config]</b> Checked out branch '" + name + "' at " +
                 configVersion_.substring(0, 8));
  pushConfigStatus();
}

void MainComponent::pushBranches() {
  if (!versionStore_)
    return;
  auto branches = versionStore_->getStorage().listBranches();

  juce::Array<juce::var> arr;
  for (const auto &b : branches) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(std::get<0>(b)));
    obj->setProperty("name", juce::String(std::get<1>(b)));
    obj->setProperty("headHash", juce::String(std::get<2>(b)));
    arr.add(juce::var(obj));
  }

  juce::String json = juce::JSON::toString(juce::var(arr), true);
  juce::String call = "if (window.setBranches) window.setBranches('" +
                      escapeForJS(json) + "');";
  broadcastJavascript(call);
  pushToDebugWindow(call);
}

void MainComponent::pushDagHistory() {
  if (!versionStore_)
    return;
  auto versions = versionStore_->listAllVersions();

  juce::Array<juce::var> arr;
  for (const auto &vPair : versions) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("hash", juce::String(vPair.first));

    const auto &ver = vPair.second;
    obj->setProperty("stateHash", juce::String(ver.stateHash));
    obj->setProperty("branchId", juce::String(ver.branchId));
    if (!ver.parentHash.empty()) {
      obj->setProperty("parentHash", juce::String(ver.parentHash));
    }
    if (!ver.mergeParentHash.empty()) {
      obj->setProperty("mergeParentHash", juce::String(ver.mergeParentHash));
    }

    arr.add(juce::var(obj));
  }

  juce::String json = juce::JSON::toString(juce::var(arr), true);
  juce::String call = "if (window.setDagHistory) window.setDagHistory('" +
                      escapeForJS(json) + "');";
  broadcastJavascript(call);
  pushToDebugWindow(call);
}

void MainComponent::broadcastJavascript(const juce::String &js) {
  if (webViewLoaded) {
    webComponent.evaluateJavascript(js);
  }
  if (historyWindow_ && historyWindowLoaded_) {
    historyWindow_->getWebView().evaluateJavascript(js);
  }
}
"""

if old_load_config in text:
    text = text.replace(old_load_config, new_load_config)
    with open("Source/Server/MainComponent.cpp", "w") as f:
        f.write(text)
    print("Methods restored.")
else:
    print("Could not find old loadConfigByName. Appending definitions to end of file.")
    with open("Source/Server/MainComponent.cpp", "a") as f:
        f.write(new_load_config)
