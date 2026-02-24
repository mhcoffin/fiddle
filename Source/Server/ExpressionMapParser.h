#pragma once

#include "ExpressionMapData.h"
#include <juce_core/juce_core.h>

namespace fiddle {

/// Parses a .doricolib expression map file into an ExpressionMapData structure.
/// Returns true on success.
bool parseExpressionMap(const juce::File &file, ExpressionMapData &result);

/// Parse from an XML string (useful for testing).
bool parseExpressionMapXml(const juce::String &xmlString,
                           ExpressionMapData &result);

} // namespace fiddle
