#include "DoricoConfigGenerator.h"
#include <iostream>

namespace fiddle {

DoricoConfigGenerator::DoricoConfigGenerator() {}

// ── Assignment Generation ──────────────────────────────────────────────────

std::vector<InstrumentAssignment> DoricoConfigGenerator::generateAssignments(
    const std::vector<int> &selectedIndices) {

  std::vector<InstrumentAssignment> assignments;
  int program = 1;
  int bankMSB = 0;
  int bankLSB = 0;

  for (int idx : selectedIndices) {
    InstrumentAssignment a;
    a.instrumentIndex = idx;
    a.program = program;
    a.bankMSB = bankMSB;
    a.bankLSB = bankLSB;
    assignments.push_back(a);

    // Advance to next slot
    ++program;
    if (program > 128) {
      program = 1;
      ++bankLSB;
      if (bankLSB > 127) {
        bankLSB = 0;
        ++bankMSB;
      }
    }
  }

  return assignments;
}

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

// ── Master Install ─────────────────────────────────────────────────────────

juce::Result DoricoConfigGenerator::generateAndInstallFiles(
    const std::vector<InstrumentAssignment> &assignments) {

  if (assignments.empty())
    return juce::Result::fail("No instruments selected.");

  juce::File baseDir = findDoricoBasePath();

  // Create required directories
  juce::File ptgDir = baseDir.getChildFile("PlaybackTemplateGenerators/Fiddle");
  juce::File pplDir = baseDir.getChildFile("PluginPresetLibraries/Fiddle");
  juce::File dlaDir = baseDir.getChildFile("DefaultLibraryAdditions");

  if (!ptgDir.createDirectory())
    return juce::Result::fail("Failed to create directory: " +
                              ptgDir.getFullPathName());

  if (!pplDir.createDirectory())
    return juce::Result::fail("Failed to create directory: " +
                              pplDir.getFullPathName());

  if (!dlaDir.createDirectory())
    return juce::Result::fail("Failed to create directory: " +
                              dlaDir.getFullPathName());

  // Write each file
  auto result = writePlaybackTemplateGen(ptgDir);
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

  std::cerr << "[DoricoConfig] All files installed to: "
            << baseDir.getFullPathName() << std::endl;

  return juce::Result::ok();
}

// ── playbacktemplategen.xml ────────────────────────────────────────────────

juce::Result
DoricoConfigGenerator::writePlaybackTemplateGen(const juce::File &dir) const {

  juce::File outFile = dir.getChildFile("playbacktemplategen.xml");
  backupExistingFile(outFile);

  auto root = std::make_unique<juce::XmlElement>("PlaybackTemplateGenerator");
  root->createNewChildElement("fileVersion")->addTextElement("1.2");
  root->createNewChildElement("specID")->addTextElement("Fiddle");
  root->createNewChildElement("name")->addTextElement("Fiddle");
  root->createNewChildElement("type")->addTextElement(
      "kMultiTimbralProgramChange");

  auto *pluginDef = root->createNewChildElement("singlePluginDefinition");
  pluginDef->createNewChildElement("pluginID")->addTextElement(kFiddlePluginId);
  pluginDef->createNewChildElement("pluginName")->addTextElement("Fiddle");
  pluginDef->createNewChildElement("pluginStateFile");
  pluginDef->createNewChildElement("pluginPresetLibraryID")
      ->addTextElement("Fiddle");
  pluginDef->createNewChildElement("numChannels")->addTextElement("16");
  pluginDef->createNewChildElement("audioOutputs")->addTextElement("16");
  pluginDef->createNewChildElement("audioOutputPerMIDIChannel")
      ->addTextElement("false");
  pluginDef->createNewChildElement("reverbLevel")->addTextElement("0");

  if (!root->writeTo(outFile))
    return juce::Result::fail("Failed to write: " + outFile.getFullPathName());

  return juce::Result::ok();
}

// ── presets.xml ────────────────────────────────────────────────────────────

juce::Result DoricoConfigGenerator::writePresetsXml(
    const juce::File &dir,
    const std::vector<InstrumentAssignment> &assignments) const {

  juce::File outFile = dir.getChildFile("presets.xml");
  backupExistingFile(outFile);

  auto instruments = getDefaultInstruments();
  auto root = std::make_unique<juce::XmlElement>("Presets");

  for (const auto &a : assignments) {
    const auto &instr = instruments[(size_t)a.instrumentIndex];

    auto *preset = root->createNewChildElement("Preset");
    preset->createNewChildElement("Name")->addTextElement(instr.presetName);
    preset->createNewChildElement("Category")
        ->addTextElement(categoryToString(instr.category));
    preset->createNewChildElement("ExpressionMap")
        ->addTextElement(kExpressionMapId);

    auto *address = preset->createNewChildElement("Address");
    address->createNewChildElement("Program")->addTextElement(
        juce::String(a.program));
    address->createNewChildElement("BankMSB")->addTextElement(
        juce::String(a.bankMSB));
    address->createNewChildElement("BankLSB")->addTextElement(
        juce::String(a.bankLSB));
    address->createNewChildElement("URI");
  }

  if (!root->writeTo(outFile))
    return juce::Result::fail("Failed to write: " + outFile.getFullPathName());

  return juce::Result::ok();
}

// ── presets_for_instruments.xml ────────────────────────────────────────────

juce::Result DoricoConfigGenerator::writePresetsForInstrumentsXml(
    const juce::File &dir,
    const std::vector<InstrumentAssignment> &assignments) const {

  juce::File outFile = dir.getChildFile("presets_for_instruments.xml");
  backupExistingFile(outFile);

  auto instruments = getDefaultInstruments();
  auto root = std::make_unique<juce::XmlElement>("PresetsForInstruments");

  for (const auto &a : assignments) {
    const auto &instr = instruments[(size_t)a.instrumentIndex];

    // Create one entry per Dorico entity ID
    for (const auto &entityId : instr.doricoEntityIds) {
      auto *pfi = root->createNewChildElement("PresetsForInstrument");
      pfi->createNewChildElement("Instrument")->addTextElement(entityId);

      auto *presets = pfi->createNewChildElement("Presets");
      presets->setAttribute("groupSize", "kSolo");
      auto *preset = presets->createNewChildElement("Preset");
      preset->setAttribute("preferred", "true");
      preset->addTextElement(instr.presetName);
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
