#include "ExpressionMapLibrary.h"
#include <algorithm>
#include <iostream>

namespace fiddle {

void ExpressionMapLibrary::scanDefaultDirectories() {
  auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

  // Scan Dorico 5 and 6 DefaultLibraryAdditions
  for (int ver = 5; ver <= 6; ++ver) {
    auto dir =
        home.getChildFile("Library/Application Support/Steinberg/Dorico " +
                          juce::String(ver) + "/DefaultLibraryAdditions");
    if (dir.isDirectory()) {
      std::cerr << "[ExpressionMapLibrary] Scanning: " << dir.getFullPathName()
                << std::endl;
      scanDirectory(dir);
    }
  }

  std::cerr << "[ExpressionMapLibrary] Total: " << size()
            << " expression maps indexed" << std::endl;
}

void ExpressionMapLibrary::scanDirectory(const juce::File &dir) {
  if (!dir.isDirectory())
    return;

  auto files = dir.findChildFiles(juce::File::findFiles, true, "*.doricolib");

  for (auto &file : files)
    addFile(file);
}

void ExpressionMapLibrary::addFile(const juce::File &file) {
  auto metas = scanExpressionMapMetadata(file);

  std::lock_guard<std::mutex> lock(mutex_);
  for (int i = 0; i < (int)metas.size(); ++i) {
    auto &meta = metas[i];
    auto it = index_.find(meta.entityID);
    if (it != index_.end()) {
      // Keep higher version
      if (meta.version <= it->second.version)
        continue;
    }

    ExpressionMapEntry entry;
    entry.name = meta.name;
    entry.entityID = meta.entityID;
    entry.version = meta.version;
    entry.creator = meta.creator;
    entry.sourceFile = file;
    entry.definitionIndex = i;

    index_[meta.entityID] = std::move(entry);
  }
}

std::vector<ExpressionMapEntry> ExpressionMapLibrary::getEntries() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ExpressionMapEntry> result;
  result.reserve(index_.size());
  for (auto &[id, entry] : index_)
    result.push_back(entry);

  // Sort by name (case-insensitive)
  std::sort(result.begin(), result.end(),
            [](const ExpressionMapEntry &a, const ExpressionMapEntry &b) {
              return a.name < b.name;
            });
  return result;
}

std::shared_ptr<ExpressionMapData>
ExpressionMapLibrary::load(const std::string &entityID) {
  juce::File file;
  int defIndex = 0;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = index_.find(entityID);
    if (it == index_.end())
      return nullptr;
    file = it->second.sourceFile;
    defIndex = it->second.definitionIndex;
  }

  auto allDefs = parseAllExpressionMaps(file);
  if (defIndex < 0 || defIndex >= (int)allDefs.size())
    return nullptr;

  return std::make_shared<ExpressionMapData>(std::move(allDefs[defIndex]));
}

juce::String ExpressionMapLibrary::toJson() const {
  auto entries = getEntries();

  juce::Array<juce::var> arr;
  for (auto &e : entries) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("name", juce::String(e.name));
    obj->setProperty("entityID", juce::String(e.entityID));
    obj->setProperty("version", e.version);
    obj->setProperty("creator", juce::String(e.creator));
    arr.add(juce::var(obj));
  }

  return juce::JSON::toString(juce::var(arr), true);
}

int ExpressionMapLibrary::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return (int)index_.size();
}

} // namespace fiddle
