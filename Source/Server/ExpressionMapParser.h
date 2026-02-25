#pragma once

#include "ExpressionMapData.h"
#include <juce_core/juce_core.h>

namespace fiddle {

/// Lightweight metadata about an expression map (for building indexes).
struct ExpressionMapMeta {
  std::string name;
  std::string entityID;
  int version = 0;
  std::string creator;
};

/// Parse the first ExpressionMapDefinition in a .doricolib file.
bool parseExpressionMap(const juce::File &file, ExpressionMapData &result);

/// Parse ALL ExpressionMapDefinitions in a .doricolib file.
/// Some files (e.g. EWQL) contain multiple definitions.
std::vector<ExpressionMapData> parseAllExpressionMaps(const juce::File &file);

/// Parse from an XML string (useful for testing).
bool parseExpressionMapXml(const juce::String &xmlString,
                           ExpressionMapData &result);

/// Parse all definitions from an XML string (useful for testing).
std::vector<ExpressionMapData>
parseAllExpressionMapsXml(const juce::String &xmlString);

/// Lightweight scan: extract only name/entityID/version/creator metadata
/// from all definitions in a file, without parsing technique combinations.
std::vector<ExpressionMapMeta>
scanExpressionMapMetadata(const juce::File &file);

} // namespace fiddle
