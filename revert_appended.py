import sys

with open("Source/Server/MainComponent.cpp", "r") as f:
    text = f.read()

# The file should end with "} // namespace fiddle". But since I appended stuff, it ends with that appended block.
# Let's find "} // namespace fiddle"
idx = text.rfind("} // namespace fiddle")
if idx != -1:
    # Keep only up to the end of namespace fiddle
    text = text[:idx + len("} // namespace fiddle\n")]

# Now let's inject pushBranches, pushDagHistory, and broadcastJavascript just before "} // namespace fiddle"
methods = """
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

text = text.replace("} // namespace fiddle\n", methods + "\n} // namespace fiddle\n")

# Need to ensure duplicates aren't left behind. Let's do a quick naive check.
# If they are already in the file, it might duplicate. But since ld said undefined earlier, they shouldn't exist.
with open("Source/Server/MainComponent.cpp", "w") as f:
    f.write(text)
