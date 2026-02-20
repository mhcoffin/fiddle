#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace fiddle {

/**
 * Persists the user's ensemble configuration as JSON.
 * This defines the "maximal ensemble" of instruments the plugin
 * advertises to Dorico via presets.xml and presets_for_instruments.xml.
 *
 * Each slot stores the Dorico entityID, display name, and how many
 * solo/section player slots are available for that instrument type.
 */
class MasterInstrumentList {
public:
  struct EnsembleSlot {
    juce::String entityID;        // e.g., "instrument.strings.violin"
    juce::String name;            // e.g., "Violin"
    juce::String musicXMLSoundID; // e.g., "strings.violin"
    juce::String family;          // e.g., "Strings" (category for presets.xml)
    int soloCount = 0;            // Number of solo player slots
    int sectionCount = 0;         // Number of section slots
  };

  MasterInstrumentList();

  /**
   * Load the instrument list from a JSON file.
   * @return true if loading succeeded.
   */
  bool load(const juce::File &file);

  /**
   * Save the instrument list to a JSON file.
   * @return true if saving succeeded.
   */
  bool save(const juce::File &file) const;

  /**
   * Load from the default save location.
   */
  bool loadDefault();

  /**
   * Save to the default save location.
   */
  bool saveDefault() const;

  /**
   * Get the default save file path.
   */
  static juce::File getDefaultFile();

  /**
   * Get the current list of ensemble slots.
   */
  const std::vector<EnsembleSlot> &getSlots() const;

  /**
   * Set the slots from a JSON string (received from the UI).
   * @param json JSON array of objects with entityID, name,
   * musicXMLSoundID, soloCount, sectionCount.
   * Backward-compatible: missing counts default to 1.
   * @return true if parsing succeeded.
   */
  bool setSlotsFromJson(const juce::String &json);

  /**
   * Get the slots as a JSON string for sending to the UI.
   */
  juce::String getSlotsAsJson() const;

  /**
   * Get just the entity IDs.
   */
  std::vector<juce::String> getEntityIds() const;

  /**
   * Check if the list is empty.
   */
  bool isEmpty() const;

  /**
   * Get the number of ensemble slots (instrument types).
   */
  int size() const;

  /**
   * Get the total number of channels needed (sum of all soloCount +
   * sectionCount).
   */
  int totalSlotCount() const;

  /**
   * Get a JSON array mapping flat index → {port, channel, name, family,
   * isSolo}. Same assignment order as DoricoConfigGenerator (index/16=port,
   * index%16=channel).
   */
  juce::String getChannelMapAsJson() const;

private:
  std::vector<EnsembleSlot> slots_;
};

} // namespace fiddle
