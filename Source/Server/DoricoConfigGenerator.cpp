#include "DoricoConfigGenerator.h"
#include "BridgeMapDocument.h"
#include <algorithm>
#include <limits>
#include <iostream>

namespace fiddle {

DoricoConfigGenerator::DoricoConfigGenerator(juce::File destination)
    : destination_(std::move(destination)) {}

// ── Path Resolution ────────────────────────────────────────────────────────

juce::File DoricoConfigGenerator::findDoricoBasePath() const {
  if (destination_ != juce::File{}) return destination_;
  // Always target the latest Dorico version.
  // Directories will be created as needed during installation.
  juce::File home =
      juce::File::getSpecialLocation(juce::File::userHomeDirectory);
  return home.getChildFile("Library/Application Support/Steinberg/Dorico 6");
}

juce::File DoricoConfigGenerator::getDoricoBasePath() const {
  return findDoricoBasePath();
}

// ── Backup ─────────────────────────────────────────────────────────────────

void DoricoConfigGenerator::backupExistingFile(const juce::File &file) const {
  if (file.existsAsFile()) {
    juce::File backup =
        file.withFileExtension(file.getFileExtension() + ".bak");
    file.copyFileTo(backup);
    std::cerr << "[DoricoConfig] Backed up: " << file.getFullPathName()
              << std::endl;
  }
}

// ── Expand EnsembleSlots → InstrumentAssignments ──────────────────────────

std::vector<InstrumentAssignment> DoricoConfigGenerator::expandSlots(
    const std::vector<MasterInstrumentList::EnsembleSlot> &slots) {

  std::vector<InstrumentAssignment> assignments;
  int program = 1;
  int bankMSB = 0;
  int bankLSB = 0;

  auto nextProgram = [&]() {
    ++program;
    if (program > 128) {
      program = 1;
      ++bankLSB;
      if (bankLSB > 127) {
        bankLSB = 0;
        ++bankMSB;
      }
    }
  };

  for (const auto &slot : slots) {
    // Solo slots
    for (int i = 0; i < slot.soloCount; ++i) {
      InstrumentAssignment a;
      a.entityID = slot.entityID;
      a.musicXMLSoundID = slot.musicXMLSoundID;
      a.category = slot.family;
      a.isSolo = true;
      a.program = program;
      a.bankMSB = bankMSB;
      a.bankLSB = bankLSB;
      a.flatIndex = static_cast<int>(assignments.size());

      if (slot.soloCount == 1)
        a.name = slot.name + " (Solo)";
      else
        a.name = slot.name + " " + juce::String(i + 1) + " (Solo)";

      assignments.push_back(a);
      nextProgram();
    }

    // Section slots
    for (int i = 0; i < slot.sectionCount; ++i) {
      InstrumentAssignment a;
      a.entityID = slot.entityID;
      a.musicXMLSoundID = slot.musicXMLSoundID;
      a.category = slot.family;
      a.isSolo = false;
      a.program = program;
      a.bankMSB = bankMSB;
      a.bankLSB = bankLSB;
      a.flatIndex = static_cast<int>(assignments.size());

      if (slot.sectionCount == 1)
        a.name = slot.name + " (Section)";
      else
        a.name = slot.name + " Section " + juce::String(i + 1);

      assignments.push_back(a);
      nextProgram();
    }
  }

  return assignments;
}

// ── Master Install ─────────────────────────────────────────────────────────

juce::Result DoricoConfigGenerator::generateAndInstallFiles(
    const std::vector<InstrumentAssignment> &assignments, int numChannels,
    const std::vector<BrowsableInstrument> &browserInstruments,
    const juce::String &bridgeMapsJson) {

  if (assignments.empty())
    return juce::Result::fail("No instruments selected.");

  juce::File baseDir = findDoricoBasePath();

  // Validate the pinned sources and every destination before changing files.
  // Resolve by physical port/channel, never by chair order, name, or family.
  BridgeMapProject bridges;
  juce::String error;
  if (!BridgeMapProject::parse(bridgeMapsJson, bridges, error))
    return juce::Result::fail("Invalid project bridge maps: " + error);
  auto resolved = assignments;
  std::set<BridgeMapProject::Endpoint> endpoints;
  std::map<juce::String, std::shared_ptr<const BridgeMapDocument>> used;
  bool needsLegacy = false;
  for (size_t i = 0; i < resolved.size(); ++i) {
    auto &a = resolved[i];
    const int flat = a.flatIndex >= 0 ? a.flatIndex : int(i);
    const BridgeMapProject::Endpoint endpoint{flat / 16, flat % 16 + 1};
    if (flat < 0 || flat >= 256 || !endpoints.insert(endpoint).second)
      return juce::Result::fail("Duplicate or invalid MIDI destination in playback template");
    const auto pin = bridges.assignments.find(endpoint);
    a.expressionMapID = kExpressionMapId;
    if (pin == bridges.assignments.end()) needsLegacy = true;
    else {
      const auto doc = bridges.documents.at(pin->second);
      a.expressionMapID = doc->entityId;
      used[pin->second] = doc;
    }
  }
  for (const auto &[endpoint, hash] : bridges.assignments)
    if (!endpoints.count(endpoint))
      return juce::Result::fail("Bridge assignment at port " + juce::String(endpoint.first + 1)
          + ", channel " + juce::String(endpoint.second)
          + " has no chair. Remove or reassign it in Bridge maps before installing.");
  if (numChannels < 1 || numChannels > 256
      || (endpoints.rbegin()->first * 16 + endpoints.rbegin()->second) > numChannels)
    return juce::Result::fail("Playback template does not declare all assigned MIDI destinations");

  // Keep immutable maps from older installs. Reject identity collisions rather
  // than letting Dorico choose between different definitions with one map ID.
  const auto libraryDir = baseDir.getChildFile("DefaultLibraryAdditions");
  for (const auto &file : libraryDir.findChildFiles(juce::File::findFiles, false, "Fiddle-Bridge-*.doricolib")) {
    const auto xml = juce::parseXML(file);
    const auto *defs = xml ? xml->getChildByName("expressionMapDefinitions") : nullptr;
    const auto *entities = defs ? defs->getChildByName("entities") : nullptr;
    const auto *map = entities ? entities->getFirstChildElement() : nullptr;
    if (!map) return juce::Result::fail("Invalid installed bridge file: " + file.getFileName());
    for (const auto &[hash, doc] : used) {
      if (map->getChildElementAllSubText("entityID", {}) != doc->entityId) continue;
      auto existing = BridgeMapDocument::compile(file.loadFileAsString(), error, doc->musicalLow, doc->musicalHigh);
      if (!existing || existing->contentHash != hash)
        return juce::Result::fail("An installed bridge has different content under Dorico ID " + doc->entityId
            + ". Publish a new revision in Bridge maps before installing.");
    }
  }

  // Build a complete staged package first, including the legacy resource when
  // any input is unassigned. No partial template is published on validation or
  // generation failure.
  const auto stage = juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getChildFile("fiddle-dorico-" + juce::Uuid().toString());
  struct Cleanup { juce::File dir; ~Cleanup() { dir.deleteRecursively(); } } cleanup{stage};
  const auto stageEc = stage.getChildFile("EndpointConfigs/Fiddle");
  const auto stagePts = stage.getChildFile("PlaybackTemplateSpecs/Fiddle");
  const auto stagePpl = stage.getChildFile("PluginPresetLibraries/Fiddle");
  const auto stageLib = stage.getChildFile("DefaultLibraryAdditions");
  for (const auto &dir : {stageEc, stagePts, stagePpl, stageLib})
    if (!dir.createDirectory()) return juce::Result::fail("Could not stage playback template");
  auto result = writeEndpointConfigXml(stageEc, resolved, browserInstruments);
  if (result.wasOk()) result = writePlaybackTemplateSpecXml(stagePts);
  if (result.wasOk()) result = writePresetsXml(stagePpl, resolved);
  if (result.wasOk()) result = writePresetsForInstrumentsXml(stagePpl, resolved, browserInstruments);
  if (result.wasOk() && needsLegacy) result = writeExpressionMapLib(stageLib);
  if (result.failed()) return result;
  for (const auto &[hash, doc] : used) {
    const auto file = stageLib.getChildFile("Fiddle-Bridge-" + hash + ".doricolib");
    const auto installed = libraryDir.getChildFile(file.getFileName());
    if (installed.existsAsFile() && installed.loadFileAsString() != doc->sourceXml)
      return juce::Result::fail("Immutable bridge file has changed: " + installed.getFileName());
    if (!file.replaceWithData(doc->sourceXml.toRawUTF8(), doc->sourceXml.getNumBytesAsUTF8()))
      return juce::Result::fail("Could not stage bridge source");
  }

  // Publish maps before endpoint references, rolling back replaced files if a
  // write fails. This is a filesystem operation, not proof of Dorico assignment.
  struct Previous { juce::File file; bool existed; juce::MemoryBlock data; };
  std::vector<Previous> previous;
  auto files = stageLib.findChildFiles(juce::File::findFiles, false);
  for (const auto &file : stage.findChildFiles(juce::File::findFiles, true))
    if (file.getParentDirectory() != stageLib) files.add(file);
  for (const auto &file : files) {
    const auto target = baseDir.getChildFile(file.getRelativePathFrom(stage));
    if (file.getParentDirectory() == stageLib && target.existsAsFile()
        && file.hasIdenticalContentTo(target)) continue;
    Previous before{target, target.existsAsFile(), {}};
    if ((target.exists() && !before.existed)
        || (before.existed && !target.loadFileAsData(before.data)) || !target.getParentDirectory().createDirectory())
      result = juce::Result::fail("Could not prepare installation file: " + target.getFullPathName());
    else {
      previous.push_back(std::move(before));
      if (!file.copyFileTo(target)) result = juce::Result::fail("Could not install: " + target.getFullPathName());
    }
    if (result.failed()) {
      bool restored = true;
      for (auto it = previous.rbegin(); it != previous.rend(); ++it)
        restored = (it->existed ? it->file.replaceWithData(it->data.getData(), it->data.getSize()) : it->file.deleteFile()) && restored;
      return restored ? result : juce::Result::fail(result.getErrorMessage() + "; rollback incomplete, check Dorico configuration files");
    }
  }
  for (const auto &before : previous)
    if (before.existed)
      before.file.withFileExtension(before.file.getFileExtension() + ".bak")
          .replaceWithData(before.data.getData(), before.data.getSize());

  // Remove old template generator if it exists (we no longer use it)
  juce::File oldPtgDir =
      baseDir.getChildFile("PlaybackTemplateGenerators/Fiddle");
  if (oldPtgDir.isDirectory())
    oldPtgDir.deleteRecursively();

  std::cerr << "[DoricoConfig] All files installed (" << assignments.size()
            << " instruments across " << endpoints.rbegin()->first + 1
            << " ports) to: " << baseDir.getFullPathName() << std::endl;

  return juce::Result::ok();
}

// ── endpointconfig.xml (VE Pro-style) ─────────────────────────────────────
//
// Maps each instrument to a specific port/channel slot. Dorico reads this
// file directly, bypassing the template generator's 16-channel limit.

juce::Result DoricoConfigGenerator::writeEndpointConfigXml(
    const juce::File &dir, const std::vector<InstrumentAssignment> &assignments,
    const std::vector<BrowsableInstrument> &browserInstruments) const {

  juce::File outFile = dir.getChildFile("endpointconfig.xml");
  backupExistingFile(outFile);

  auto root = buildEndpointConfigXml(assignments, browserInstruments);
  if (!root->writeTo(outFile))
    return juce::Result::fail("Failed to write: " + outFile.getFullPathName());

  return juce::Result::ok();
}

std::unique_ptr<juce::XmlElement>
DoricoConfigGenerator::buildEndpointConfigXml(
    const std::vector<InstrumentAssignment> &assignments,
    const std::vector<BrowsableInstrument> &browserInstruments) {

  auto root = std::make_unique<juce::XmlElement>("endpointConfig");
  root->createNewChildElement("fileVersion")->addTextElement("1.1416");
  root->createNewChildElement("version")->addTextElement("1");
  root->createNewChildElement("name")->addTextElement("Fiddle");
  root->createNewChildElement("configID")
      ->addTextElement("endpointconfig.user.fiddle");

  // ── Slots (plugin instance definition) ──
  auto *slots = root->createNewChildElement("slots");
  slots->setAttribute("array", "true");

  auto *slotData = slots->createNewChildElement("slotData");
  slotData->createNewChildElement("numAudioOutputs")->addTextElement("1");

  auto *instanceData = slotData->createNewChildElement("instanceData");
  instanceData->createNewChildElement("slotID")->addTextElement("1");
  instanceData->createNewChildElement("pluginID")
      ->addTextElement(kFiddlePluginId);
  instanceData->createNewChildElement("pluginName")->addTextElement("Fiddle");
  instanceData->createNewChildElement("pluginPresetLibraryID")
      ->addTextElement("Fiddle");
  instanceData->createNewChildElement("pluginPresetLibraryIDs");
  instanceData->createNewChildElement("enabled")->addTextElement("true");
  instanceData->createNewChildElement("flags")->addTextElement("0");
  instanceData->createNewChildElement("endpointConfigID");
  instanceData->createNewChildElement("endpointConfigSlotIndex")
      ->addTextElement("0");

  // ── Program contents: one entry per assigned instrument ──
  auto *programContents =
      instanceData->createNewChildElement("programContents");
  auto *entries = programContents->createNewChildElement("entries");
  entries->setAttribute("array", "true");

  for (int i = 0; i < (int)assignments.size(); ++i) {
    const int midiIndex =
        assignments[i].flatIndex >= 0 ? assignments[i].flatIndex : i;
    int portIndex = midiIndex / 16;   // 0-based port
    int channelRel0 = midiIndex % 16; // 0-based channel within port

    auto *entry = entries->createNewChildElement("entry");
    entry->createNewChildElement("portIndex")
        ->addTextElement(juce::String(portIndex));
    entry->createNewChildElement("channelNumberRel0")
        ->addTextElement(juce::String(channelRel0));
    entry->createNewChildElement("programName")
        ->addTextElement(assignments[i].name);
    entry->createNewChildElement("collectionName");
    entry->createNewChildElement("expressionMapID")
        ->addTextElement(assignments[i].expressionMapID);
    entry->createNewChildElement("drumkitNoteMapID");
    entry->createNewChildElement("flags")->addTextElement("0");
  }

  // ── Instrument-to-entry mapping ──
  // This maps each Dorico instrument entity ID to the correct MIDI endpoint.
  // The <endpoints> value is the physical flattened MIDI destination, not the
  // position in this (potentially sparse) entries array. <index>
  // disambiguates multiple instruments of the same type (e.g., Violin 1 vs
  // Violin 2).
  auto *instruments = root->createNewChildElement("instruments");
  instruments->setAttribute("array", "true");

  // Build lookup from entity ID → all variant entity IDs.
  // Group by musicXMLSoundID: all instruments sharing the same soundID
  // are variants of the same instrument.
  std::map<juce::String, std::vector<juce::String>> soundIdToEntities;
  for (const auto &bi : browserInstruments) {
    soundIdToEntities[bi.musicXMLSoundID].push_back(bi.entityID);
  }
  // Now build entityID → all sibling entity IDs
  std::map<juce::String, std::vector<juce::String>> entityToVariants;
  for (const auto &bi : browserInstruments) {
    entityToVariants[bi.entityID] = soundIdToEntities[bi.musicXMLSoundID];
  }

  // Dorico uses <index> to match the Nth player of a given instrument
  // type + player type. Solo and section are counted separately.
  const auto playerIndex = playerIndices(assignments);

  for (int i = 0; i < (int)assignments.size(); ++i) {
    const auto &a = assignments[i];
    const int endpointIndex = a.flatIndex >= 0 ? a.flatIndex : i;

    // Get all variant entity IDs for this instrument
    std::vector<juce::String> entityIds;
    auto it = entityToVariants.find(a.entityID);
    if (it != entityToVariants.end()) {
      entityIds = it->second;
    } else {
      entityIds.push_back(a.entityID);
    }

    const int idx = playerIndex[size_t(i)];

    // Emit one instrumentData block per variant entity ID
    for (const auto &eid : entityIds) {
      auto *instrData = instruments->createNewChildElement("instrumentData");
      instrData->createNewChildElement("entityID")->addTextElement(eid);
      instrData->createNewChildElement("index")->addTextElement(
          juce::String(idx));
      instrData->createNewChildElement("irvIndex")->addTextElement("0");
      instrData->createNewChildElement("playerType")
          ->addTextElement(a.isSolo ? "kSoloPlayer" : "kSectionPlayer");
      instrData->createNewChildElement("endpoints")
          ->addTextElement(juce::String(endpointIndex));
    }
  }

  return root;
}

std::vector<int> DoricoConfigGenerator::playerIndices(
    const std::vector<InstrumentAssignment> &assignments) {
  std::map<juce::String, std::vector<size_t>> groups;
  for (size_t i = 0; i < assignments.size(); ++i)
    groups[assignments[i].entityID + (assignments[i].isSolo ? ":solo" : ":section")].push_back(i);
  std::vector<int> result(assignments.size(), 0);
  for (auto &[key, members] : groups) {
    std::stable_sort(members.begin(), members.end(), [&](size_t a, size_t b) {
      const auto rank = [&](size_t i) {
        return assignments[i].ordinal >= 0 ? assignments[i].ordinal : std::numeric_limits<int>::max();
      };
      return rank(a) < rank(b);
    });
    for (size_t n = 0; n < members.size(); ++n) result[members[n]] = int(n);
  }
  return result;
}

// ── playbacktemplatespec.xml ──────────────────────────────────────────────
//
// References the endpoint config by ID. This is what appears in Dorico's
// "Playback Template" dropdown.

juce::Result DoricoConfigGenerator::writePlaybackTemplateSpecXml(
    const juce::File &dir) const {

  juce::File outFile = dir.getChildFile("playbacktemplatespec.xml");
  backupExistingFile(outFile);

  auto root = std::make_unique<juce::XmlElement>("playbackTemplateSpec");
  root->createNewChildElement("fileVersion")->addTextElement("1.1416");
  // Use "playbacktemplate.fiddle" to match the ID that Dorico generated
  // from the old template generator (specID="Fiddle"). This ensures
  // existing projects find the template without a migration warning.
  root->createNewChildElement("playbackTemplateSpecID")
      ->addTextElement("playbacktemplate.fiddle");
  root->createNewChildElement("name")->addTextElement("Fiddle");
  root->createNewChildElement("creator");
  root->createNewChildElement("description");
  root->createNewChildElement("version")->addTextElement("1");
  root->createNewChildElement("associatedSpaceTemplateID");

  auto *entries = root->createNewChildElement("entries");
  entries->setAttribute("array", "true");

  auto *entry = entries->createNewChildElement("entry");
  entry->createNewChildElement("instrumentFamilies");
  entry->createNewChildElement("instruments");
  auto *ecRef = entry->createNewChildElement("endpointConfig");
  ecRef->createNewChildElement("configID")
      ->addTextElement("endpointconfig.user.fiddle");

  if (!root->writeTo(outFile))
    return juce::Result::fail("Failed to write: " + outFile.getFullPathName());

  return juce::Result::ok();
}

// ── presets.xml ────────────────────────────────────────────────────────────
//
// Each assignment produces one <Preset> element. The <Category> element
// is required by Dorico's parser (matches NotePerformer's format).

juce::Result DoricoConfigGenerator::writePresetsXml(
    const juce::File &dir,
    const std::vector<InstrumentAssignment> &assignments) const {

  juce::File outFile = dir.getChildFile("presets.xml");
  backupExistingFile(outFile);

  auto root = std::make_unique<juce::XmlElement>("Presets");

  for (const auto &a : assignments) {
    auto *preset = root->createNewChildElement("Preset");
    preset->createNewChildElement("Name")->addTextElement(a.name);
    preset->createNewChildElement("Category")
        ->addTextElement(a.category.isNotEmpty() ? a.category : "Other");
    preset->createNewChildElement("ExpressionMap")
        ->addTextElement(a.expressionMapID);

    auto *addr = preset->createNewChildElement("Address");
    addr->createNewChildElement("Program")->addTextElement(
        juce::String(a.program));
    addr->createNewChildElement("BankMSB")->addTextElement(
        juce::String(a.bankMSB));
    addr->createNewChildElement("BankLSB")->addTextElement(
        juce::String(a.bankLSB));
    addr->createNewChildElement("URI");
  }

  if (!root->writeTo(outFile))
    return juce::Result::fail("Failed to write: " + outFile.getFullPathName());

  return juce::Result::ok();
}

// ── presets_for_instruments.xml ────────────────────────────────────────────
//
// Maps each Dorico instrument entity to TWO presets — one for solo players
// (groupSize="kSolo") and one for section players (groupSize="kSection").

juce::Result DoricoConfigGenerator::writePresetsForInstrumentsXml(
    const juce::File &dir, const std::vector<InstrumentAssignment> &assignments,
    const std::vector<BrowsableInstrument> &browserInstruments) const {

  juce::File outFile = dir.getChildFile("presets_for_instruments.xml");
  backupExistingFile(outFile);

  auto root = std::make_unique<juce::XmlElement>("PresetsForInstruments");

  // Build entity ID → all variant entity IDs via musicXMLSoundID grouping.
  std::map<juce::String, std::vector<juce::String>> soundIdToEntities;
  for (const auto &bi : browserInstruments) {
    soundIdToEntities[bi.musicXMLSoundID].push_back(bi.entityID);
  }
  std::map<juce::String, std::vector<juce::String>> entityToVariants;
  for (const auto &bi : browserInstruments) {
    entityToVariants[bi.entityID] = soundIdToEntities[bi.musicXMLSoundID];
  }

  // Each assignment produces PresetsForInstrument entries.
  // We emit one entry per variant entity ID so that Dorico matches
  // regardless of which variant the user chose (e.g., Trumpet in Bb vs C).
  for (const auto &a : assignments) {
    // Look up the instrument to get all variant entity IDs
    auto it = entityToVariants.find(a.entityID);
    if (it != entityToVariants.end()) {
      // Emit one block per variant
      for (const auto &variantId : it->second) {
        auto *pfi = root->createNewChildElement("PresetsForInstrument");
        pfi->createNewChildElement("Instrument")->addTextElement(variantId);

        auto *presets = pfi->createNewChildElement("Presets");
        presets->setAttribute("groupSize", a.isSolo ? "kSolo" : "kSection");
        auto *preset = presets->createNewChildElement("Preset");
        preset->setAttribute("preferred", "true");
        preset->addTextElement(a.name);
      }
    } else {
      // Fallback: emit just the stored entity ID (unknown instrument)
      auto *pfi = root->createNewChildElement("PresetsForInstrument");
      pfi->createNewChildElement("Instrument")->addTextElement(a.entityID);

      auto *presets = pfi->createNewChildElement("Presets");
      presets->setAttribute("groupSize", a.isSolo ? "kSolo" : "kSection");
      auto *preset = presets->createNewChildElement("Preset");
      preset->setAttribute("preferred", "true");
      preset->addTextElement(a.name);
    }
  }

  if (!root->writeTo(outFile))
    return juce::Result::fail("Failed to write: " + outFile.getFullPathName());

  return juce::Result::ok();
}

// ── Fiddle_Universal.doricolib (Expression Map) ───────────────────────────

juce::Result
DoricoConfigGenerator::writeExpressionMapLib(const juce::File &dir) const {

  juce::File outFile = dir.getChildFile("Fiddle_Universal.doricolib");
  backupExistingFile(outFile);

  // Locate the bundled .doricolib resource file.
  // Search order:
  //   1. resources/ next to the executable (development builds)
  //   2. ../Resources/ in the .app bundle (packaged macOS app)
  juce::File exeDir =
      juce::File::getSpecialLocation(juce::File::currentExecutableFile)
          .getParentDirectory();

  juce::File sourceFile =
      exeDir.getChildFile("resources/Fiddle_Universal.doricolib");
  if (!sourceFile.existsAsFile())
    sourceFile = exeDir.getChildFile("../Resources/Fiddle_Universal.doricolib");
  if (!sourceFile.existsAsFile()) {
    // Also try relative to project root (when running from build dir)
    sourceFile =
        exeDir.getChildFile("../../resources/Fiddle_Universal.doricolib");
  }
  if (!sourceFile.existsAsFile()) {
    // Final fallback: check working directory
    sourceFile = juce::File::getCurrentWorkingDirectory().getChildFile(
        "resources/Fiddle_Universal.doricolib");
  }

  if (!sourceFile.existsAsFile())
    return juce::Result::fail(
        "Could not find Fiddle_Universal.doricolib resource file. "
        "Expected in resources/ directory next to the application.");

  if (!sourceFile.copyFileTo(outFile))
    return juce::Result::fail("Failed to copy expression map to: " +
                              outFile.getFullPathName());

  std::cerr << "[DoricoConfig] Installed expression map from: "
            << sourceFile.getFullPathName() << std::endl;

  return juce::Result::ok();
}

} // namespace fiddle
