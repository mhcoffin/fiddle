#pragma once

#include "DoricoInstrumentBrowser.h"
#include "MasterInstrumentList.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace fiddle {

/**
 * Represents a single "chair" in the ensemble — one instrument assigned
 * to a unique MIDI program address. Each EnsembleSlot with soloCount=2
 * and sectionCount=1 expands into 3 InstrumentAssignments.
 */
struct InstrumentAssignment {
  juce::String entityID; // Dorico instrument entity ID
  juce::String name;     // Display name (e.g., "Violin 1 (Solo)")
  juce::String musicXMLSoundID;
  juce::String category; // Category for presets.xml (e.g., "Strings")
  int program;           // MIDI Program number (1-128)
  int bankMSB = 0;       // Bank Select MSB (CC0)
  int bankLSB = 0;       // Bank Select LSB (CC32)
  bool isSolo;           // true = solo player, false = section player
};

/**
 * Generates Dorico configuration files for the Fiddle plugin.
 *
 * Uses the VE Pro-style endpoint config approach (not the template
 * generator approach, which has a hard 16-channel limit).
 *
 * Produces five files from a list of EnsembleSlots:
 *   EndpointConfigs/Fiddle/endpointconfig.xml
 *   PlaybackTemplateSpecs/Fiddle/playbacktemplatespec.xml
 *   PluginPresetLibraries/Fiddle/presets.xml
 *   PluginPresetLibraries/Fiddle/presets_for_instruments.xml
 *   DefaultLibraryAdditions/Fiddle_Universal.doricolib
 */
class DoricoConfigGenerator {
public:
  DoricoConfigGenerator();

  /**
   * Expand EnsembleSlots into individual InstrumentAssignments.
   * Each slot produces soloCount + sectionCount assignments with
   * sequential program numbers.
   */
  static std::vector<InstrumentAssignment>
  expandSlots(const std::vector<MasterInstrumentList::EnsembleSlot> &slots);

  /**
   * Generate all XML config files and install them to the Dorico
   * application support directory.
   *
   * @param assignments  The expanded instrument assignments
   * @param numChannels  Total channels to declare in playback template
   * @return Result indicating success or failure with details
   */
  juce::Result generateAndInstallFiles(
      const std::vector<InstrumentAssignment> &assignments, int numChannels,
      const std::vector<BrowsableInstrument> &browserInstruments);

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

  juce::Result writeEndpointConfigXml(
      const juce::File &baseDir,
      const std::vector<InstrumentAssignment> &assignments,
      const std::vector<BrowsableInstrument> &browserInstruments) const;

  juce::Result writePlaybackTemplateSpecXml(const juce::File &baseDir) const;

  juce::Result
  writePresetsXml(const juce::File &baseDir,
                  const std::vector<InstrumentAssignment> &assignments) const;

  juce::Result writePresetsForInstrumentsXml(
      const juce::File &baseDir,
      const std::vector<InstrumentAssignment> &assignments,
      const std::vector<BrowsableInstrument> &browserInstruments) const;

  juce::Result writeExpressionMapLib(const juce::File &baseDir) const;
};

} // namespace fiddle
