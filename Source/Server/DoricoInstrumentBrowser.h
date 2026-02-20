#pragma once

#include <juce_core/juce_core.h>
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
  juce::String cachedJson_; // Built once after parsing
};

} // namespace fiddle
