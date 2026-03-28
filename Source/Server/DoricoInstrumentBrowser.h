#pragma once

#include <juce_core/juce_core.h>
#include <unordered_map>
#include <vector>

namespace fiddle {

/**
 * Represents a single instrument parsed from Dorico's instruments.xml.
 */
struct BrowsableInstrument {
  juce::String name;            // e.g., "Violin"
  juce::String entityID;        // e.g., "instrument.strings.violin"
  juce::String musicXMLSoundID; // e.g., "strings.violin"
  juce::String family;          // Top-level family derived from musicXMLSoundID
  bool isDefault = true;        // true if no parentEntityID (this is the root variant)
};

/**
 * Parses Dorico's instruments.xml to provide a browsable list of
 * all available instruments. Used by the Setup tab to let users
 * choose which instruments to support.
 */
class DoricoInstrumentBrowser {
public:
  DoricoInstrumentBrowser();

  /**
   * Find the instruments.xml file from the latest Dorico installation.
   * Scans /Applications/Dorico *.app for the highest version.
   * @return Path to instruments.xml, or empty File if not found.
   */
  juce::File findInstrumentsXml() const;

  /**
   * Parse the given instruments.xml file.
   * @return true if parsing succeeded and at least one instrument was found.
   */
  bool parseInstrumentsXml(const juce::File &file);

  /**
   * Parse from the auto-detected Dorico installation.
   * Convenience method that calls findInstrumentsXml() then
   * parseInstrumentsXml().
   */
  bool loadFromDorico();

  /**
   * Parse the score order XML file (instrumentScoreOrders.xml).
   * Extracts the "Orchestral" score order mapping entityID → position.
   * @return true if parsing succeeded.
   */
  bool parseScoreOrders(const juce::File& file);

  /**
   * Get the "Orchestral" score order map: entityID → position.
   * Lower position = higher in the orchestral score.
   */
  const std::unordered_map<juce::String, int>& getScoreOrder() const;

  /**
   * Get all parsed instruments.
   */
  const std::vector<BrowsableInstrument> &getInstruments() const;

  /**
   * Get the full instrument list as a JSON string for sending to the WebView.
   * Returns a JSON array of objects with: name, entityID, musicXMLSoundID,
   * family.
   */
  juce::String getInstrumentsAsJson() const { return cachedJson_; }

  /**
   * Get the unique set of instrument families found.
   */
  std::vector<juce::String> getFamilies() const;

private:
  void buildJsonCache();
  std::vector<BrowsableInstrument> instruments_;
  std::unordered_map<juce::String, int> scoreOrder_; // entityID → orchestral position
  juce::String cachedJson_; // Built once after parsing
};

} // namespace fiddle
