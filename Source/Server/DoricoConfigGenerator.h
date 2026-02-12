#pragma once

#include "DoricoInstruments.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace fiddle {

/**
 * Tracks the MIDI bank/program assignment for a selected instrument.
 */
struct InstrumentAssignment {
  int instrumentIndex; // Index into getDefaultInstruments()
  int program;         // MIDI Program Change (1-128)
  int bankMSB;         // MIDI Bank Select MSB (CC0)
  int bankLSB;         // MIDI Bank Select LSB (CC32)
};

/**
 * Generates and installs Dorico playback template configuration files
 * so that Dorico can automatically discover and route instruments
 * through the Fiddle VST3 plugin.
 *
 * Output files:
 *   PlaybackTemplateGenerators/Fiddle/playbacktemplategen.xml
 *   PluginPresetLibraries/Fiddle/presets.xml
 *   PluginPresetLibraries/Fiddle/presets_for_instruments.xml
 *   DefaultLibraryAdditions/Fiddle_Universal.doricolib
 */
class DoricoConfigGenerator {
public:
  DoricoConfigGenerator();

  /**
   * Generate MIDI Bank/Program assignments for the selected instruments.
   * Programs are assigned sequentially: 1, 2, 3, ..., 128, then
   * BankLSB increments and programs restart at 1.
   *
   * @param selectedIndices  Indices into getDefaultInstruments()
   * @return Vector of assignments with bank/program values
   */
  std::vector<InstrumentAssignment>
  generateAssignments(const std::vector<int> &selectedIndices);

  /**
   * Generate all XML config files and install them to the Dorico
   * application support directory.
   *
   * @param assignments  The instrument assignments from generateAssignments()
   * @return Result indicating success or failure with details
   */
  juce::Result
  generateAndInstallFiles(const std::vector<InstrumentAssignment> &assignments);

  /**
   * Get the resolved Dorico application support base directory.
   * Returns empty File if no Dorico installation was found.
   */
  juce::File getDoricoBasePath() const;

private:
  // Placeholder UUID for the Fiddle VST3 plugin.
  // TODO: Replace with the actual UUID from the compiled VST3 binary.
  static constexpr const char *kFiddlePluginId =
      "ABCDEF019182FAEB4D616E754669646C";

  static constexpr const char *kExpressionMapId = "xmap.user.Fiddle.1";

  juce::File findDoricoBasePath() const;
  void backupExistingFile(const juce::File &file) const;

  juce::Result writePlaybackTemplateGen(const juce::File &baseDir) const;

  juce::Result
  writePresetsXml(const juce::File &baseDir,
                  const std::vector<InstrumentAssignment> &assignments) const;

  juce::Result writePresetsForInstrumentsXml(
      const juce::File &baseDir,
      const std::vector<InstrumentAssignment> &assignments) const;

  juce::Result writeExpressionMapLib(const juce::File &baseDir) const;
};

} // namespace fiddle
