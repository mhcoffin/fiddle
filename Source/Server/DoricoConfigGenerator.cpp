#include "DoricoConfigGenerator.h"
#include "DoricoInstruments.h"
#include <iostream>

namespace fiddle {

DoricoConfigGenerator::DoricoConfigGenerator() {}

// ── Path Resolution ────────────────────────────────────────────────────────

juce::File DoricoConfigGenerator::findDoricoBasePath() const {
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
    const std::vector<InstrumentAssignment> &assignments, int numChannels) {

  if (assignments.empty())
    return juce::Result::fail("No instruments selected.");

  juce::File baseDir = findDoricoBasePath();

  // Create required directories (VE Pro-style layout)
  juce::File ecDir = baseDir.getChildFile("EndpointConfigs/Fiddle");
  juce::File ptsDir = baseDir.getChildFile("PlaybackTemplateSpecs/Fiddle");
  juce::File pplDir = baseDir.getChildFile("PluginPresetLibraries/Fiddle");
  juce::File dlaDir = baseDir.getChildFile("DefaultLibraryAdditions");

  for (auto *dir : {&ecDir, &ptsDir, &pplDir, &dlaDir}) {
    if (!dir->createDirectory())
      return juce::Result::fail("Failed to create directory: " +
                                dir->getFullPathName());
  }

  // Remove old template generator if it exists (we no longer use it)
  juce::File oldPtgDir =
      baseDir.getChildFile("PlaybackTemplateGenerators/Fiddle");
  if (oldPtgDir.isDirectory())
    oldPtgDir.deleteRecursively();

  // Write each file
  auto result = writeEndpointConfigXml(ecDir, assignments);
  if (result.failed())
    return result;

  result = writePlaybackTemplateSpecXml(ptsDir);
  if (result.failed())
    return result;

  result = writePresetsXml(pplDir, assignments);
  if (result.failed())
    return result;

  result = writePresetsForInstrumentsXml(pplDir, assignments);
  if (result.failed())
    return result;

  result = writeExpressionMapLib(dlaDir);
  if (result.failed())
    return result;

  std::cerr << "[DoricoConfig] All files installed (" << assignments.size()
            << " instruments across " << ((int)assignments.size() + 15) / 16
            << " ports) to: " << baseDir.getFullPathName() << std::endl;

  return juce::Result::ok();
}

// ── endpointconfig.xml (VE Pro-style) ─────────────────────────────────────
//
// Maps each instrument to a specific port/channel slot. Dorico reads this
// file directly, bypassing the template generator's 16-channel limit.

juce::Result DoricoConfigGenerator::writeEndpointConfigXml(
    const juce::File &dir,
    const std::vector<InstrumentAssignment> &assignments) const {

  juce::File outFile = dir.getChildFile("endpointconfig.xml");
  backupExistingFile(outFile);

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
    int portIndex = i / 16;   // 0-based port
    int channelRel0 = i % 16; // 0-based channel within port

    auto *entry = entries->createNewChildElement("entry");
    entry->createNewChildElement("portIndex")
        ->addTextElement(juce::String(portIndex));
    entry->createNewChildElement("channelNumberRel0")
        ->addTextElement(juce::String(channelRel0));
    entry->createNewChildElement("programName")
        ->addTextElement(assignments[i].name);
    entry->createNewChildElement("collectionName");
    entry->createNewChildElement("expressionMapID")
        ->addTextElement(kExpressionMapId);
    entry->createNewChildElement("drumkitNoteMapID");
    entry->createNewChildElement("flags")->addTextElement("0");
  }

  // ── Instrument-to-entry mapping ──
  // This maps each Dorico instrument entity ID to the correct entry
  // (port/channel slot). The <endpoints> value is the flat index in
  // the entries array. <index> disambiguates multiple instruments of
  // the same type (e.g., Violin 1 vs Violin 2).
  auto *instruments = root->createNewChildElement("instruments");
  instruments->setAttribute("array", "true");

  // Build lookup from entity ID → instrument metadata for all variants
  auto instrumentList = getDefaultInstruments();
  std::map<juce::String, const DoricoInstrument *> entityToInstrument;
  for (const auto &instr : instrumentList) {
    for (const auto &eid : instr.doricoEntityIds) {
      entityToInstrument[eid] = &instr;
    }
  }

  // Track how many times each (entityID, playerType) pair has appeared.
  // Dorico uses <index> to match the Nth player of a given instrument
  // type + player type. Solo and section are counted separately.
  std::map<juce::String, int> entityCount;

  for (int i = 0; i < (int)assignments.size(); ++i) {
    const auto &a = assignments[i];

    // Get all variant entity IDs for this instrument
    std::vector<juce::String> entityIds;
    auto it = entityToInstrument.find(a.entityID);
    if (it != entityToInstrument.end()) {
      for (const auto &eid : it->second->doricoEntityIds)
        entityIds.push_back(eid);
    } else {
      entityIds.push_back(a.entityID);
    }

    // Key includes player type so solo/section are counted separately
    juce::String countKey = a.entityID + (a.isSolo ? ":solo" : ":section");
    int idx = entityCount[countKey]++;

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
          ->addTextElement(juce::String(i));
    }
  }

  if (!root->writeTo(outFile))
    return juce::Result::fail("Failed to write: " + outFile.getFullPathName());

  return juce::Result::ok();
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
        ->addTextElement(kExpressionMapId);

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
    const juce::File &dir,
    const std::vector<InstrumentAssignment> &assignments) const {

  juce::File outFile = dir.getChildFile("presets_for_instruments.xml");
  backupExistingFile(outFile);

  auto root = std::make_unique<juce::XmlElement>("PresetsForInstruments");

  // Build a lookup from any entity ID → all entity IDs for that instrument.
  // This maps, e.g., "instrument.brass.trumpet.a" to the full list of
  // trumpet variants so that ALL variants get a preset entry.
  auto instruments = getDefaultInstruments();
  std::map<juce::String, const DoricoInstrument *> entityToInstrument;
  for (const auto &instr : instruments) {
    for (const auto &eid : instr.doricoEntityIds) {
      entityToInstrument[eid] = &instr;
    }
  }

  // Each assignment produces PresetsForInstrument entries.
  // We emit one entry per variant entity ID so that Dorico matches
  // regardless of which variant the user chose (e.g., Trumpet in Bb vs C).
  for (const auto &a : assignments) {
    // Look up the instrument to get all variant entity IDs
    auto it = entityToInstrument.find(a.entityID);
    if (it != entityToInstrument.end()) {
      // Emit one block per variant
      for (const auto &variantId : it->second->doricoEntityIds) {
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
